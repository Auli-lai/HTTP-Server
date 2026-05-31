#include "http_parser.h"
#include "log.h"
#include <unistd.h>
#include <cstring>
#include <string.h>
#include <fstream>
#include <sstream>
#include <sys/stat.h>
#include <stdlib.h>
#include <climits>
#include <ctime>
#include <mutex>
#include <algorithm>
#include <cctype>

// ══════════════════════════════════════════════════════════════════
//  API 内存存储 + 文件持久化
// ══════════════════════════════════════════════════════════════════
static std::map<int, std::string> api_store;
static std::mutex api_mutex;
static int api_next_id = 1;
static const char* API_STORE_FILE = "api_data.json";

void api_store_load() {
    std::ifstream file(API_STORE_FILE);
    if (!file) return; // 文件不存在, 首次启动

    std::lock_guard<std::mutex> lock(api_mutex);
    std::string line;
    int max_id = 0;
    while (std::getline(file, line)) {
        if (line.empty()) continue;
        // 格式: id|json_data
        size_t sep = line.find('|');
        if (sep == std::string::npos) continue;
        int id = std::stoi(line.substr(0, sep));
        std::string data = line.substr(sep + 1);
        api_store[id] = data;
        if (id > max_id) max_id = id;
    }
    api_next_id = max_id + 1;
    Log::info("API store loaded: %zu items, next_id=%d", api_store.size(), api_next_id);
}

void api_store_save() {
    std::ofstream file(API_STORE_FILE, std::ios::trunc);
    if (!file) {
        Log::error("Failed to save API store to %s", API_STORE_FILE);
        return;
    }
    // 不加锁（调用者已持有 api_mutex）
    for (const auto& kv : api_store) {
        file << kv.first << "|" << kv.second << "\n";
    }
    file.close();
}

// ══════════════════════════════════════════════════════════════════
//  构造 / 析构 / 重置
// ══════════════════════════════════════════════════════════════════
HttpParser::HttpParser(int client_fd)
    : client_fd(client_fd), is_head_response(false),
      keep_alive(true), request_count(0) {
}

HttpParser::~HttpParser() {
}

void HttpParser::reset() {
    method.clear();
    path.clear();
    version.clear();
    headers.clear();
    body.clear();
    buffer.clear();
    response_buffer.clear();
    is_head_response = false;
    keep_alive = true;
    request_count++;
}

bool HttpParser::want_keep_alive() const {
    return keep_alive && request_count < MAX_KEEPALIVE_REQUESTS;
}

// ══════════════════════════════════════════════════════════════════
//  工具函数
// ══════════════════════════════════════════════════════════════════

// 大小写不敏感获取 header 值
std::string HttpParser::get_header(const std::string& name) const {
    std::string name_lower = name;
    for (char& c : name_lower) c = static_cast<char>(tolower(c));
    for (const auto& h : headers) {
        std::string key_lower = h.first;
        for (char& c : key_lower) c = static_cast<char>(tolower(c));
        if (key_lower == name_lower) {
            return h.second;
        }
    }
    return "";
}

std::string HttpParser::url_decode(const std::string& str) {
    std::string result;
    result.reserve(str.size());
    for (size_t i = 0; i < str.size(); ++i) {
        if (str[i] == '%' && i + 2 < str.size()) {
            char hex[3] = {str[i + 1], str[i + 2], '\0'};
            char* end = nullptr;
            long val = strtol(hex, &end, 16);
            if (end == hex + 2) {
                result += static_cast<char>(val);
                i += 2;
            } else {
                result += str[i];
            }
        } else if (str[i] == '+') {
            result += ' ';
        } else {
            result += str[i];
        }
    }
    return result;
}

std::map<std::string, std::string> HttpParser::parse_urlencoded(const std::string& data) {
    std::map<std::string, std::string> params;
    if (data.empty()) return params;
    std::istringstream iss(data);
    std::string pair;
    while (std::getline(iss, pair, '&')) {
        if (pair.empty()) continue;
        size_t eq = pair.find('=');
        if (eq != std::string::npos) {
            std::string key   = url_decode(pair.substr(0, eq));
            std::string value = url_decode(pair.substr(eq + 1));
            if (!key.empty()) params[key] = value;
        }
    }
    return params;
}

std::string HttpParser::escape_json(const std::string& s) {
    std::string r;
    r.reserve(s.size() + 16);
    for (char c : s) {
        switch (c) {
        case '"':  r += "\\\""; break;
        case '\\': r += "\\\\"; break;
        case '\n': r += "\\n";  break;
        case '\r': r += "\\r";  break;
        case '\t': r += "\\t";  break;
        default:   r += c;
        }
    }
    return r;
}

// ══════════════════════════════════════════════════════════════════
//  网络读取
// ══════════════════════════════════════════════════════════════════
bool HttpParser::read_request() {
    char buf[4096];
    while (true) {
        if (buffer.size() >= MAX_HEADER_SIZE + MAX_BODY_SIZE) {
            Log::error("Request too large (fd=%d)", client_fd);
            return false;
        }

        ssize_t n = read(client_fd, buf, sizeof(buf));
        if (n == -1) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                return true;
            }
            Log::error("Failed to read request (fd=%d)", client_fd);
            return false;
        }

        if (n == 0) {
            Log::info("Connection closed by peer (fd=%d)", client_fd);
            return false;
        }

        buffer.append(buf, n);
    }
    return true;
}

// ══════════════════════════════════════════════════════════════════
//  HTTP 解析
// ══════════════════════════════════════════════════════════════════
HttpParser::ParseResult HttpParser::parse() {
    if (!read_request()) {
        return PARSE_ERROR;
    }

    size_t header_end = buffer.find("\r\n\r\n");
    if (header_end == std::string::npos) {
        if (buffer.size() > MAX_HEADER_SIZE) {
            Log::error("Header too large (fd=%d)", client_fd);
            return PARSE_ERROR;
        }
        return PARSE_AGAIN;
    }

    // 解析请求行和头部
    std::string header_part = buffer.substr(0, header_end + 2);
    std::istringstream iss(header_part);

    iss >> method >> path >> version;
    if (method.empty() || path.empty() || version.empty()) {
        Log::error("Invalid request line (fd=%d)", client_fd);
        return PARSE_ERROR;
    }

    if (version.compare(0, 5, "HTTP/") != 0) {
        Log::error("Invalid HTTP version: %s (fd=%d)", version.c_str(), client_fd);
        return PARSE_ERROR;
    }

    // 解析头部
    std::string line;
    while (std::getline(iss, line) && line != "\r") {
        if (line.empty()) continue;
        size_t colon_pos = line.find(":");
        if (colon_pos != std::string::npos) {
            std::string key = line.substr(0, colon_pos);
            std::string value = line.substr(colon_pos + 1); // 跳过 ":"
            // 去除前导空格
            size_t start = 0;
            while (start < value.size() && (value[start] == ' ' || value[start] == '\t')) {
                start++;
            }
            value = value.substr(start);
            // 去除结尾 \r
            if (!value.empty() && value.back() == '\r') {
                value.pop_back();
            }
            if (!key.empty()) headers[key] = value;
        }
    }

    // ── 解析请求体 (大小写不敏感获取 Content-Length) ──
    body.clear();
    size_t body_start = header_end + 4;
    std::string cl_str = get_header("Content-Length");

    if (!cl_str.empty()) {
        long content_length = 0;
        try {
            content_length = std::stol(cl_str);
        } catch (const std::exception&) {
            Log::error("Invalid Content-Length value: '%s' (fd=%d)", cl_str.c_str(), client_fd);
            return PARSE_ERROR;
        }

        if (content_length < 0) {
            Log::error("Negative Content-Length (fd=%d)", client_fd);
            return PARSE_ERROR;
        }
        if (static_cast<size_t>(content_length) > MAX_BODY_SIZE) {
            Log::error("Body too large: %ld (fd=%d)", content_length, client_fd);
            return PARSE_ERROR;
        }

        if (buffer.size() - body_start < static_cast<size_t>(content_length)) {
            Log::info("Body not fully received: need %ld, have %zu (fd=%d)",
                      content_length, buffer.size() - body_start, client_fd);
            return PARSE_AGAIN;
        }

        body = buffer.substr(body_start, static_cast<size_t>(content_length));
        Log::info("Body parsed: %zu bytes (fd=%d)", body.size(), client_fd);
    } else if (method == "POST" || method == "PUT") {
        // 无 Content-Length 但有 body 数据: 使用所有可用数据
        if (buffer.size() > body_start) {
            body = buffer.substr(body_start);
            Log::info("Body (no Content-Length): %zu bytes (fd=%d)", body.size(), client_fd);
        } else {
            Log::info("POST/PUT with no body (fd=%d)", client_fd);
        }
    }

    // 检测客户端是否要求关闭连接
    std::string conn = get_header("Connection");
    if (!conn.empty()) {
        std::string conn_lower = conn;
        for (char& c : conn_lower) c = static_cast<char>(tolower(c));
        if (conn_lower == "close") {
            keep_alive = false;
        }
    }

    Log::info("Request: %s %s %s (fd=%d, req#%d)",
              method.c_str(), path.c_str(), version.c_str(),
              client_fd, request_count + 1);
    return PARSE_OK;
}

// ══════════════════════════════════════════════════════════════════
//  请求分发
// ══════════════════════════════════════════════════════════════════
void HttpParser::handle_request() {
    is_head_response = (method == "HEAD");

    if (is_head_response) {
        method = "GET";
    }

    // ── OPTIONS ──
    if (method == "OPTIONS") {
        std::string allow = "GET, HEAD, POST, PUT, DELETE, OPTIONS";
        time_t now = time(nullptr);
        char date_buf[128];
        struct tm tm_gmt;
        gmtime_r(&now, &tm_gmt);
        strftime(date_buf, sizeof(date_buf), "%a, %d %b %Y %H:%M:%S GMT", &tm_gmt);

        std::ostringstream resp;
        resp << version << " 204 No Content\r\n";
        resp << "Allow: " << allow << "\r\n";
        resp << "Date: " << date_buf << "\r\n";
        resp << "Server: reall_http/2.0\r\n";
        resp << "Content-Length: 0\r\n";
        resp << "Connection: keep-alive\r\n";
        resp << "\r\n";
        response_buffer = resp.str();
        return;
    }

    // ── RESTful API ──
    if (path.compare(0, 5, "/api/") == 0) {
        handle_api_request();
        return;
    }

    // ── 表单页面 ──
    if (path == "/form") {
        if (method == "GET") {
            handle_form_page();
            return;
        }
    }

    // ── 表单提交 ──
    if (path == "/submit" && method == "POST") {
        Log::info("Routing to handle_form_submit (path='%s', body='%.60s')",
                  path.c_str(), body.c_str());
        handle_form_submit();
        return;
    }

    // ── GET 静态文件 ──
    if (method == "GET") {
        std::string file_path = "." + path;
        if (file_path == "./") {
            file_path = "./index.html";
        }

        if (file_path.find("/../") != std::string::npos ||
            file_path.compare(0, 3, "../") == 0 ||
            file_path.find("/..") == file_path.size() - 3) {
            send_response(403, "Forbidden",
                "<html><body><h1>403 Forbidden</h1></body></html>");
            return;
        }

        handle_static_file(file_path);
        return;
    }

    // ── 通用 POST ──
    if (method == "POST") {
        std::string content_type = "text/plain";
        std::string ct_val = get_header("Content-Type");
        if (!ct_val.empty() && ct_val.find("application/json") != std::string::npos) {
            content_type = "application/json";
        }

        std::ostringstream info;
        info << "<html><body>\n";
        info << "<h1>POST Data Received</h1>\n";
        info << "<p>Path: " << path << "</p>\n";
        info << "<p>Content-Type: " << (ct_val.empty() ? "none" : ct_val) << "</p>\n";
        info << "<p>Content-Length: " << body.size() << "</p>\n";
        info << "<h2>Body:</h2>\n";
        info << "<pre>" << body << "</pre>\n";
        info << "<p><a href=\"/\">Back to Home</a> | "
             << "<a href=\"/form\">Go to Form</a></p>\n";
        info << "</body></html>\n";
        send_response(200, "OK", info.str());
        return;
    }

    // ── PUT / DELETE 对于非 API 路径 ──
    send_response(405, "Method Not Allowed",
        "<html><body><h1>405 Method Not Allowed</h1></body></html>");
}

// ══════════════════════════════════════════════════════════════════
//  静态文件
// ══════════════════════════════════════════════════════════════════
void HttpParser::handle_static_file(const std::string& file_path) {
    struct stat st;
    if (stat(file_path.c_str(), &st) == -1) {
        send_response(404, "Not Found",
            "<html><body><h1>404 Not Found</h1><p>The requested file was not found.</p></body></html>");
        return;
    }

    if (!S_ISREG(st.st_mode)) {
        send_response(403, "Forbidden",
            "<html><body><h1>403 Forbidden</h1></body></html>");
        return;
    }

    // realpath 二次校验
    char resolved[PATH_MAX];
    if (realpath(file_path.c_str(), resolved) != nullptr) {
        std::string resolved_str(resolved);
        if (resolved_str.find("/../") != std::string::npos) {
            send_response(403, "Forbidden",
                "<html><body><h1>403 Forbidden</h1></body></html>");
            return;
        }
    }

    std::ifstream file(file_path, std::ios::binary);
    if (!file) {
        send_response(500, "Internal Server Error",
            "<html><body><h1>500 Internal Server Error</h1></body></html>");
        return;
    }

    std::string content((std::istreambuf_iterator<char>(file)),
                        std::istreambuf_iterator<char>());
    file.close();

    send_response(200, "OK", content, get_mime_type(file_path));
}

// ══════════════════════════════════════════════════════════════════
//  MIME 类型
// ══════════════════════════════════════════════════════════════════
std::string HttpParser::get_mime_type(const std::string& file_path) {
    auto ends_with = [&](const std::string& ext) -> bool {
        if (file_path.size() < ext.size()) return false;
        return file_path.compare(file_path.size() - ext.size(), ext.size(), ext) == 0;
    };

    if (ends_with(".html") || ends_with(".htm"))  return "text/html; charset=utf-8";
    if (ends_with(".css"))                         return "text/css; charset=utf-8";
    if (ends_with(".js") || ends_with(".mjs"))     return "application/javascript; charset=utf-8";
    if (ends_with(".json"))                        return "application/json; charset=utf-8";
    if (ends_with(".xml"))                         return "application/xml; charset=utf-8";
    if (ends_with(".svg"))                         return "image/svg+xml";
    if (ends_with(".jpeg") || ends_with(".jpg"))   return "image/jpeg";
    if (ends_with(".png"))                         return "image/png";
    if (ends_with(".gif"))                         return "image/gif";
    if (ends_with(".ico"))                         return "image/x-icon";
    if (ends_with(".webp"))                        return "image/webp";
    if (ends_with(".woff2"))                       return "font/woff2";
    if (ends_with(".woff"))                        return "font/woff";
    if (ends_with(".ttf"))                         return "font/ttf";
    if (ends_with(".txt"))                         return "text/plain; charset=utf-8";
    if (ends_with(".md"))                          return "text/markdown; charset=utf-8";
    if (ends_with(".pdf"))                         return "application/pdf";
    if (ends_with(".zip"))                         return "application/zip";
    if (ends_with(".mp4"))                         return "video/mp4";
    if (ends_with(".mp3"))                         return "audio/mpeg";
    if (ends_with(".wasm"))                        return "application/wasm";

    return "application/octet-stream";
}

// ══════════════════════════════════════════════════════════════════
//  HTTP 响应
// ══════════════════════════════════════════════════════════════════
void HttpParser::send_response(int status_code, const std::string& status_msg,
                               const std::string& content, const std::string& content_type) {
    std::ostringstream response;
    response << version << " " << status_code << " " << status_msg << "\r\n";
    response << "Content-Type: " << content_type << "\r\n";
    response << "Content-Length: " << content.size() << "\r\n";

    // Keep-Alive 控制
    if (keep_alive && request_count < MAX_KEEPALIVE_REQUESTS) {
        response << "Connection: keep-alive\r\n";
        response << "Keep-Alive: timeout=60, max=" << (MAX_KEEPALIVE_REQUESTS - request_count) << "\r\n";
    } else {
        response << "Connection: close\r\n";
    }

    response << "Server: reall_http/2.0\r\n";

    time_t now = time(nullptr);
    char date_buf[128];
    struct tm tm_gmt;
    gmtime_r(&now, &tm_gmt);
    strftime(date_buf, sizeof(date_buf), "%a, %d %b %Y %H:%M:%S GMT", &tm_gmt);
    response << "Date: " << date_buf << "\r\n";

    response << "\r\n";
    response << content;

    response_buffer = response.str();

    // HEAD 请求: 只保留头部
    if (is_head_response) {
        size_t body_sep = response_buffer.find("\r\n\r\n");
        if (body_sep != std::string::npos) {
            response_buffer = response_buffer.substr(0, body_sep + 4);
        }
    }

    Log::info("Response: %d %s %zub (fd=%d, keepalive=%d)",
              status_code, status_msg.c_str(), content.size(),
              client_fd, keep_alive ? 1 : 0);
}

void HttpParser::send_json_response(int status_code, const std::string& status_msg,
                                     const std::string& json_body) {
    send_response(status_code, status_msg, json_body, "application/json; charset=utf-8");
}

bool HttpParser::send_response_data() {
    if (response_buffer.empty()) {
        return true;
    }

    ssize_t n = write(client_fd, response_buffer.c_str(), response_buffer.size());
    if (n == -1) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return false;
        }
        Log::error("Failed to send response (fd=%d)", client_fd);
        return true;
    }

    if (n == (ssize_t)response_buffer.size()) {
        return true;
    }

    response_buffer = response_buffer.substr(n);
    return false;
}

// ══════════════════════════════════════════════════════════════════
//  表单处理
// ══════════════════════════════════════════════════════════════════

void HttpParser::handle_form_page() {
    std::string html = R"(<!DOCTYPE html>
<html lang="zh-CN">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Form Demo — reall_http</title>
<style>
  * { box-sizing: border-box; margin: 0; padding: 0; }
  body { font-family: system-ui, sans-serif; background: #f5f5f5;
         display: flex; justify-content: center; padding: 40px 16px; }
  .card { background: #fff; border-radius: 8px; box-shadow: 0 2px 12px rgba(0,0,0,.08);
          max-width: 480px; width: 100%; padding: 32px; }
  h1 { font-size: 1.5rem; margin-bottom: 24px; color: #333; }
  label { display: block; font-size: .875rem; font-weight: 600;
          color: #555; margin-bottom: 4px; }
  input, textarea { width: 100%; padding: 10px 12px; margin-bottom: 16px;
                    border: 1px solid #ddd; border-radius: 6px;
                    font-size: .95rem; font-family: inherit; }
  input:focus, textarea:focus { outline: none; border-color: #4f7cff;
                                box-shadow: 0 0 0 3px rgba(79,124,255,.15); }
  textarea { resize: vertical; min-height: 100px; }
  button { background: #4f7cff; color: #fff; border: none; padding: 12px 24px;
           border-radius: 6px; font-size: 1rem; font-weight: 600; cursor: pointer; }
  button:hover { background: #3b5fd9; }
  .back { display: inline-block; margin-top: 16px; color: #4f7cff; text-decoration: none; }
</style>
</head>
<body>
<div class="card">
  <h1>📬 联系我们</h1>
  <form method="POST" action="/submit">
    <label for="name">姓名</label>
    <input id="name" name="name" type="text" placeholder="张三" required>

    <label for="email">邮箱</label>
    <input id="email" name="email" type="email" placeholder="zhangsan@example.com" required>

    <label for="message">留言</label>
    <textarea id="message" name="message" placeholder="请输入您的留言…" required></textarea>

    <button type="submit">发送</button>
  </form>
  <a class="back" href="/">← 返回首页</a>
</div>
</body>
</html>)";
    send_response(200, "OK", html);
}

void HttpParser::handle_form_submit() {
    Log::info("FORM SUBMIT: body_len=%zu body='%.100s'", body.size(), body.c_str());

    auto params = parse_urlencoded(body);

    std::string name    = params.count("name")    ? params["name"]    : "(not provided)";
    std::string email   = params.count("email")   ? params["email"]   : "(not provided)";
    std::string message = params.count("message") ? params["message"] : "(not provided)";

    Log::info("FORM PARSED: name='%s' email='%s' msg='%.50s'",
              name.c_str(), email.c_str(), message.c_str());

    // 同时存入 API 存储 (作为记录保留)
    {
        std::lock_guard<std::mutex> lock(api_mutex);
        int id = api_next_id++;
        std::ostringstream json;
        json << "{\"name\":\"" << escape_json(name)
             << "\",\"email\":\"" << escape_json(email)
             << "\",\"message\":\"" << escape_json(message) << "\"}";
        api_store[id] = json.str();
        api_store_save();
    }

    auto html_escape = [](const std::string& s) -> std::string {
        std::string r;
        r.reserve(s.size() + 16);
        for (char c : s) {
            switch (c) {
            case '&':  r += "&amp;";  break;
            case '<':  r += "&lt;";   break;
            case '>':  r += "&gt;";   break;
            case '"':  r += "&quot;"; break;
            case '\'': r += "&#39;";  break;
            default:   r += c;
            }
        }
        return r;
    };

    std::ostringstream html;
    html << "<!DOCTYPE html>\n"
         << "<html lang=\"zh-CN\">\n<head>\n"
         << "<meta charset=\"utf-8\">\n"
         << "<title>Submission Received</title>\n"
         << "<style>\n"
         << "  body { font-family: system-ui, sans-serif; background: #f5f5f5; "
         << "display: flex; justify-content: center; padding: 40px 16px; }\n"
         << "  .card { background: #fff; border-radius: 8px; box-shadow: 0 2px 12px rgba(0,0,0,.08); "
         << "max-width: 520px; width: 100%; padding: 32px; }\n"
         << "  h1 { color: #2e7d32; margin-bottom: 20px; }\n"
         << "  .field { margin-bottom: 16px; }\n"
         << "  .field dt { font-weight: 600; color: #555; font-size: .875rem; }\n"
         << "  .field dd { margin-top: 4px; color: #333; }\n"
         << "  a { color: #4f7cff; }\n"
         << "</style>\n</head>\n<body>\n"
         << "<div class=\"card\">\n"
         << "  <h1>✅ 提交成功！</h1>\n"
         << "  <dl>\n"
         << "    <div class=\"field\"><dt>姓名</dt><dd>" << html_escape(name) << "</dd></div>\n"
         << "    <div class=\"field\"><dt>邮箱</dt><dd>" << html_escape(email) << "</dd></div>\n"
         << "    <div class=\"field\"><dt>留言</dt><dd>" << html_escape(message) << "</dd></div>\n"
         << "  </dl>\n"
         << "  <p><a href=\"/form\">← 返回表单</a> &nbsp; "
         << "<a href=\"/\">返回首页</a></p>\n"
         << "</div>\n</body>\n</html>";

    send_response(200, "OK", html.str());
}

// ══════════════════════════════════════════════════════════════════
//  RESTful API (/api/data)
// ══════════════════════════════════════════════════════════════════

void HttpParser::handle_api_request() {
    std::string api_path = path.substr(4);

    if (api_path == "/data" || api_path == "/data/") {
        if (method == "GET") {
            handle_api_list();
        } else if (method == "POST") {
            handle_api_create();
        } else {
            send_json_response(405, "Method Not Allowed",
                "{\"error\":\"Method not allowed. Use GET or POST.\"}");
        }
        return;
    }

    if (api_path.compare(0, 6, "/data/") == 0) {
        std::string id_str = api_path.substr(6);
        if (id_str.empty() || !std::all_of(id_str.begin(), id_str.end(), ::isdigit)) {
            send_json_response(400, "Bad Request",
                "{\"error\":\"Invalid ID. Must be a positive integer.\"}");
            return;
        }
        int id = std::stoi(id_str);

        if (method == "GET") {
            handle_api_get(id);
        } else if (method == "PUT") {
            handle_api_update(id);
        } else if (method == "DELETE") {
            handle_api_delete(id);
        } else {
            send_json_response(405, "Method Not Allowed",
                "{\"error\":\"Method not allowed. Use GET, PUT, or DELETE.\"}");
        }
        return;
    }

    send_json_response(404, "Not Found",
        "{\"error\":\"API endpoint not found. Try /api/data.\"}");
}

void HttpParser::handle_api_list() {
    std::lock_guard<std::mutex> lock(api_mutex);

    std::ostringstream json;
    json << "{\"count\":" << api_store.size() << ",\"items\":[";
    bool first = true;
    for (const auto& kv : api_store) {
        if (!first) json << ",";
        first = false;
        json << "{\"id\":" << kv.first << ",\"data\":" << kv.second << "}";
    }
    json << "]}";

    send_json_response(200, "OK", json.str());
}

void HttpParser::handle_api_get(int id) {
    std::lock_guard<std::mutex> lock(api_mutex);

    auto it = api_store.find(id);
    if (it == api_store.end()) {
        send_json_response(404, "Not Found",
            "{\"error\":\"Item not found.\"}");
        return;
    }

    std::ostringstream json;
    json << "{\"id\":" << id << ",\"data\":" << it->second << "}";
    send_json_response(200, "OK", json.str());
}

void HttpParser::handle_api_create() {
    std::string ct_val = get_header("Content-Type");
    if (ct_val.empty() || ct_val.find("application/json") == std::string::npos) {
        send_json_response(415, "Unsupported Media Type",
            "{\"error\":\"Content-Type must be application/json.\"}");
        return;
    }

    if (body.empty()) {
        Log::info("API CREATE: empty body (Content-Type was '%s')", ct_val.c_str());
        send_json_response(400, "Bad Request",
            "{\"error\":\"Request body is empty.\"}");
        return;
    }

    Log::info("API CREATE: body='%.100s'", body.c_str());

    int id;
    {
        std::lock_guard<std::mutex> lock(api_mutex);
        id = api_next_id++;
        api_store[id] = body;
        api_store_save();
    }

    std::ostringstream json;
    json << "{\"id\":" << id << ",\"data\":" << body << "}";
    send_json_response(201, "Created", json.str());
}

void HttpParser::handle_api_update(int id) {
    std::string ct_val = get_header("Content-Type");
    if (ct_val.empty() || ct_val.find("application/json") == std::string::npos) {
        send_json_response(415, "Unsupported Media Type",
            "{\"error\":\"Content-Type must be application/json.\"}");
        return;
    }

    if (body.empty()) {
        send_json_response(400, "Bad Request",
            "{\"error\":\"Request body is empty.\"}");
        return;
    }

    {
        std::lock_guard<std::mutex> lock(api_mutex);
        auto it = api_store.find(id);
        if (it == api_store.end()) {
            send_json_response(404, "Not Found",
                "{\"error\":\"Item not found.\"}");
            return;
        }
        it->second = body;
        api_store_save();
    }

    std::ostringstream json;
    json << "{\"id\":" << id << ",\"data\":" << body << "}";
    send_json_response(200, "OK", json.str());
}

void HttpParser::handle_api_delete(int id) {
    {
        std::lock_guard<std::mutex> lock(api_mutex);
        auto it = api_store.find(id);
        if (it == api_store.end()) {
            send_json_response(404, "Not Found",
                "{\"error\":\"Item not found.\"}");
            return;
        }
        api_store.erase(it);
        api_store_save();
    }

    send_json_response(200, "OK", "{\"message\":\"Item deleted.\"}");
}
