#pragma once
#include <string>
#include <unordered_map>
#include <enum>

enum class ProcessState {
    REQUEST_LINE,
    HEADERS,
    BODY,
    FINISH,
    ERROR
};

enum class HttpMethod {
    GET,
    POST,
    INVALID
};

class HttpRequest {
public:
    bool parseRequest(const char* start, const char* end);
    bool isFinish() const { return state_ == ProcessState::FINISH; }
    bool hasError() const { return state_ == ProcessState::ERROR; }
    
    std::string getMethod() const {
        if (method_ == HttpMethod::GET) return "GET";
        if (method_ == HttpMethod::POST) return "POST";
        return "INVALID";
    }
    std::string getPath() const { return path_; }
    std::string getHeader(const std::string& key) const {
        auto it = headers_.find(key);
        return it != headers_.end() ? it->second : "";
    }

    void reset() {
        state_ = ProcessState::REQUEST_LINE;
        method_ = HttpMethod::INVALID;
        path_.clear();
        headers_.clear();
        body_.clear();
    }

private:
    ProcessState state_ = ProcessState::REQUEST_LINE;
    HttpMethod method_ = HttpMethod::INVALID;
    std::string path_;
    std::unordered_map<std::string, std::string> headers_;
    std::string body_;
    
    // 辅助解析函数
    bool parseRequestLine(const char* start, const char* end);
    bool parseHeaders(const char* start, const char* end);
};