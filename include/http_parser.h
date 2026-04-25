#ifndef HTTP_PARSER_H
#define HTTP_PARSER_H

#include <string>
#include <map>

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
    bool send_response_data(); // 发送响应数据

private:
    int client_fd;
    std::string method;
    std::string path;
    std::string version;
    std::map<std::string, std::string> headers;
    std::string body;
    std::string buffer; // 应用层缓冲区
    std::string response_buffer; // 响应缓冲区

    enum ParseState {
        STATE_METHOD,
        STATE_PATH,
        STATE_VERSION,
        STATE_HEADERS,
        STATE_BODY
    };

    bool read_request();
    void send_response(int status_code, const std::string& status_msg, const std::string& content, const std::string& content_type = "text/html");
    void handle_static_file(const std::string& file_path);
};

#endif // HTTP_PARSER_H
