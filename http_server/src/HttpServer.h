#pragma once
#include <sys/epoll.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/sendfile.h>
#include <sys/stat.h>
#include <cstring>
#include <iostream>
#include <string>
#include <memory>
#include <functional>
#include <unordered_map>
#include <vector>
#include <algorithm>

inline void setNonBlocking(int fd) {
    int oldFlags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, oldFlags | O_NONBLOCK);
}

enum class ProcessState { REQUEST_LINE, HEADERS, BODY, FINISH, ERROR };
enum class HttpMethod { GET, POST, INVALID };

// 连接处理状态
enum class ConnectionState { PROCESSING, WRITING, CLOSING };

class HttpRequest {
public:
    bool parse(const char* start, const char* end);
    bool isFinish() const { return state_ == ProcessState::FINISH; }
    bool hasError() const { return state_ == ProcessState::ERROR; }
    std::string getPath() const { return path_; }
    std::string getMethodStr() const { return method_ == HttpMethod::GET ? "GET" : "OTHER"; }
    void reset() {
        state_ = ProcessState::REQUEST_LINE;
        method_ = HttpMethod::INVALID;
        path_.clear();
        headers_.clear();
    }
private:
    ProcessState state_ = ProcessState::REQUEST_LINE;
    HttpMethod method_ = HttpMethod::INVALID;
    std::string path_;
    std::unordered_map<std::string, std::string> headers_;
};

class EventLoop;
class Channel {
public:
    using Callback = std::function<void()>;
    Channel(EventLoop* loop, int fd);
    ~Channel();
    void setReadCallback(Callback cb) { readCb_ = cb; }
    void setWriteCallback(Callback cb) { writeCb_ = cb; }
    void setCloseCallback(Callback cb) { closeCb_ = cb; }
    
    void enableReading();
    void enableWriting();
    void disableWriting();
    void handleEvent(int revents);
    int fd() const { return fd_; }

    int events() const { return events_; } 

private:
    EventLoop* loop_;
    int fd_;
    int events_;
    Callback readCb_;
    Callback writeCb_;
    Callback closeCb_;
    void update();
};

class EventLoop {
public:
    EventLoop();
    ~EventLoop();
    void loop();
    void quit();
    void updateChannel(Channel* ch);
    void removeChannel(Channel* ch);
private:
    int epFd_;
    bool looping_;
    bool quit_;
    std::vector<epoll_event> events_;
    std::unordered_map<int, Channel*> channels_;
};

class HttpConnection : public std::enable_shared_from_this<HttpConnection> {
public:
    HttpConnection(EventLoop* loop, int fd);
    ~HttpConnection();
    
    void onRead();
    void onWrite(); // 新增：处理写入事件
    void handleClose();

private:
    EventLoop* loop_;
    int fd_;
    std::unique_ptr<Channel> channel_;
    std::unique_ptr<HttpRequest> request_;
    
    std::string inBuffer_;
    std::string outBuffer_; // 输出缓冲区
    
    ConnectionState state_ = ConnectionState::PROCESSING;
    int fileFd_ = -1;     // 当前正在发送的文件描述符
    off_t fileOffset_ = 0; // 文件发送偏移量
    size_t fileSize_ = 0;  // 文件总大小

    void processRequest();
    void prepareResponse(const std::string& body, const std::string& contentType);
    void prepareFileResponse(const std::string& path);
    void sendError(int code, const std::string& msg);
    
    void doWrite(); // 执行实际的写入操作
};

class HttpServer {
public:
    HttpServer(int port);
    void start();
private:
    EventLoop loop_;
    int serverFd_;
    std::unique_ptr<Channel> acceptChannel_;
    void onAccept();
};