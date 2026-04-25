#include "http_parser.h"
#include "log.h"
#include <unistd.h>
#include <cstring>
#include <string.h>
#include <fstream>
#include <sstream>
#include <sys/stat.h>
#include <stdlib.h>

HttpParser::HttpParser(int client_fd) : client_fd(client_fd) {
}

HttpParser::~HttpParser() {
}

HttpParser::ParseResult HttpParser::parse() {
    if (!read_request()) {
        return PARSE_ERROR;
    }

    // 检查是否有完整的请求行
    size_t space1 = buffer.find(' ');
    if (space1 == std::string::npos) {
        return PARSE_AGAIN;
    }
    
    size_t space2 = buffer.find(' ', space1 + 1);
    if (space2 == std::string::npos) {
        return PARSE_AGAIN;
    }
    
    // 检查是否有完整的头部
    size_t header_end = buffer.find("\r\n\r\n");
    if (header_end == std::string::npos) {
        return PARSE_AGAIN;
    }

    std::istringstream iss(buffer);

    // 解析请求行
    iss >> method >> path >> version;
    if (method.empty() || path.empty() || version.empty()) {
        Log::error("Invalid request line");
        return PARSE_ERROR;
    }

    // 解析头部
    std::string line;
    while (std::getline(iss, line) && line != "\r") {
        if (line.empty()) continue;
        size_t colon_pos = line.find(":");
        if (colon_pos != std::string::npos) {
            std::string key = line.substr(0, colon_pos);
            std::string value = line.substr(colon_pos + 2); // 跳过冒号和空格
            if (!value.empty() && value.back() == '\r') {
                value.pop_back();
            }
            headers[key] = value;
        }
    }

    Log::info("Request: %s %s %s", method.c_str(), path.c_str(), version.c_str());
    return PARSE_OK;
}

bool HttpParser::read_request() {
    char buf[4096];
    while (true) {
        ssize_t n = read(client_fd, buf, sizeof(buf));
        if (n == -1) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                return true; // 非阻塞读，当前数据已读完
            }
            Log::error("Failed to read request");
            return false;
        }

        if (n == 0) {
            Log::info("Connection closed by peer");
            return false;
        }

        buffer.append(buf, n);
        
        // 循环读取直到返回EAGAIN，确保边缘触发下一次事件把内核缓冲区数据全部读上来
        // 这里可以后续优化成循环读取，配合EPOLLET降低事件触发频率
    }
    return true;
}

void HttpParser::handle_request() {
    if (method != "GET") {
        send_response(405, "Method Not Allowed", "<html><body><h1>405 Method Not Allowed</h1></body></html>");
        return;
    }

    // 处理静态文件请求
    std::string file_path = "." + path;
    if (file_path == "./") {
        file_path = "./index.html";
    }

    // 路径安全检查：防止 ../ 遍历攻击
    // 这里可以后续使用 realpath() 来完全规范化路径
    if (file_path.find("..") != std::string::npos) {
        send_response(403, "Forbidden", "<html><body><h1>403 Forbidden</h1></body></html>");
        return;
    }

    handle_static_file(file_path);
}

void HttpParser::handle_static_file(const std::string& file_path) {
    struct stat st;
    if (stat(file_path.c_str(), &st) == -1) {
        send_response(404, "Not Found", "<html><body><h1>404 Not Found</h1></body></html>");
        return;
    }

    if (!S_ISREG(st.st_mode)) {
        send_response(403, "Forbidden", "<html><body><h1>403 Forbidden</h1></body></html>");
        return;
    }

    std::ifstream file(file_path, std::ios::binary);
    if (!file) {
        send_response(500, "Internal Server Error", "<html><body><h1>500 Internal Server Error</h1></body></html>");
        return;
    }

    std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    file.close();

    // 确定内容类型
    std::string content_type = "text/plain";
    if (file_path.size() >= 5 && file_path.compare(file_path.size() - 5, 5, ".html") == 0) {
        content_type = "text/html";
    } else if (file_path.size() >= 4 && file_path.compare(file_path.size() - 4, 4, ".css") == 0) {
        content_type = "text/css";
    } else if (file_path.size() >= 3 && file_path.compare(file_path.size() - 3, 3, ".js") == 0) {
        content_type = "application/javascript";
    } else if (file_path.size() >= 4 && file_path.compare(file_path.size() - 4, 4, ".jpg") == 0) {
        content_type = "image/jpeg";
    } else if (file_path.size() >= 5 && file_path.compare(file_path.size() - 5, 5, ".jpeg") == 0) {
        content_type = "image/jpeg";
    } else if (file_path.size() >= 4 && file_path.compare(file_path.size() - 4, 4, ".png") == 0) {
        content_type = "image/png";
    } else if (file_path.size() >= 4 && file_path.compare(file_path.size() - 4, 4, ".gif") == 0) {
        content_type = "image/gif";
    }

    send_response(200, "OK", content, content_type);
}

void HttpParser::send_response(int status_code, const std::string& status_msg, const std::string& content, const std::string& content_type) {
    std::ostringstream response;
    response << version << " " << status_code << " " << status_msg << "\r\n";
    response << "Content-Type: " << content_type << "\r\n";
    response << "Content-Length: " << content.size() << "\r\n";
    response << "Connection: close\r\n";
    response << "\r\n";
    response << content;

    response_buffer = response.str();

    Log::info("Response: %d %s", status_code, status_msg.c_str());
}

bool HttpParser::send_response_data() {
    if (response_buffer.empty()) {
        return true;
    }

    ssize_t n = write(client_fd, response_buffer.c_str(), response_buffer.size());
    if (n == -1) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return false; // 需要再次发送
        }
        Log::error("Failed to send response");
        return true; // 发送失败，关闭连接
    }

    if (n == (ssize_t)response_buffer.size()) {
        return true; // 发送完成
    }

    // 部分发送，更新缓冲区
    response_buffer = response_buffer.substr(n);
    return false;
}
