#ifndef HTTP_PARSER_H
#define HTTP_PARSER_H

#include <string>
#include <map>
#include <vector>

class HttpParser {
public:
    enum ParseResult {
        PARSE_OK,
        PARSE_AGAIN,
        PARSE_ERROR
    };

    HttpParser(int client_fd);
    ~HttpParser();
    ParseResult parse();
    void handle_request();
    bool send_response_data();
    void reset();                    // Keep-Alive: 重置状态以处理下一个请求
    bool want_keep_alive() const;    // 是否保持连接

private:
    int client_fd;
    std::string method;
    std::string path;
    std::string version;
    std::map<std::string, std::string> headers;
    std::string body;
    std::string buffer;
    std::string response_buffer;
    bool is_head_response;           // HEAD 请求标志
    bool keep_alive;                 // 是否使用长连接
    int request_count;               // 当前连接已处理请求数

    static const size_t MAX_BODY_SIZE = 1024 * 1024;       // 1MB
    static const size_t MAX_HEADER_SIZE = 64 * 1024;        // 64KB
    static const int MAX_KEEPALIVE_REQUESTS = 100;          // 单连接最大请求数

    // 大小写不敏感查找 header 值
    std::string get_header(const std::string& name) const;

    bool read_request();
    void send_response(int status_code, const std::string& status_msg,
                       const std::string& content,
                       const std::string& content_type = "text/html");
    void send_json_response(int status_code, const std::string& status_msg,
                            const std::string& json_body);

    // 静态文件 + MIME
    void handle_static_file(const std::string& file_path);
    std::string get_mime_type(const std::string& file_path);

    // 表单处理
    void handle_form_page();
    void handle_form_submit();
    std::map<std::string, std::string> parse_urlencoded(const std::string& data);
    std::string url_decode(const std::string& str);

    // RESTful API
    void handle_api_request();
    void handle_api_list();
    void handle_api_get(int id);
    void handle_api_create();
    void handle_api_update(int id);
    void handle_api_delete(int id);
    std::string escape_json(const std::string& s);
};

// ── API 数据持久化 ────────────────────────────────────────────
void api_store_load();               // 启动时从文件加载
void api_store_save();               // 每次修改后保存到文件

#endif // HTTP_PARSER_H
