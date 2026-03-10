#include "HttpServer.h"

// --- HttpRequest Impl ---
bool HttpRequest::parse(const char* start, const char* end) {
    const char* cur = start;
    while (cur < end && state_ != ProcessState::FINISH && state_ != ProcessState::ERROR) {
        if (state_ == ProcessState::REQUEST_LINE) {
            const char* crlf = std::search(cur, end, "\r\n", "\r\n" + 2);
            if (crlf == end) break;
            const char* space1 = std::find(cur, crlf, ' ');
            if (space1 == crlf) { state_ = ProcessState::ERROR; return false; }
            std::string method(cur, space1);
            if (method == "GET") method_ = HttpMethod::GET;
            
            const char* space2 = std::find(space1 + 1, crlf, ' ');
            if (space2 == crlf) { state_ = ProcessState::ERROR; return false; }
            path_ = std::string(space1 + 1, space2);
            if (path_ == "/") path_ = "/index.html";
            
            cur = crlf + 2;
            state_ = ProcessState::HEADERS;
        } else if (state_ == ProcessState::HEADERS) {
            const char* crlf = std::search(cur, end, "\r\n", "\r\n" + 2);
            if (crlf == end) break;
            if (crlf == cur) {
                cur = crlf + 2;
                state_ = ProcessState::FINISH;
            } else {
                const char* colon = std::find(cur, crlf, ':');
                if (colon != crlf) {
                    std::string key(cur, colon);
                    const char* val = colon + 1;
                    while (val < crlf && *val == ' ') val++;
                    headers_[key] = std::string(val, crlf);
                }
                cur = crlf + 2;
            }
        }
    }
    return true;
}

// --- Channel Impl ---
Channel::Channel(EventLoop* loop, int fd) : loop_(loop), fd_(fd), events_(0) {}
Channel::~Channel() { /* loop_->removeChannel(this); 由 Loop 管理更安全，这里简化 */ }

void Channel::enableReading() { events_ |= EPOLLIN; update(); }
void Channel::enableWriting() { events_ |= EPOLLOUT; update(); }
void Channel::disableWriting() { events_ &= ~EPOLLOUT; update(); }

void Channel::update() { loop_->updateChannel(this); }

void Channel::handleEvent(int revents) {
    if (revents & (EPOLLIN | EPOLLPRI)) { if (readCb_) readCb_(); }
    if (revents & EPOLLOUT) { if (writeCb_) writeCb_(); }
    if (revents & (EPOLLHUP | EPOLLERR)) { if (closeCb_) closeCb_(); }
}

// --- EventLoop Impl ---
EventLoop::EventLoop() : epFd_(epoll_create1(EPOLL_CLOEXEC)), looping_(false), quit_(false) {
    events_.resize(1024);
    if (epFd_ < 0) { perror("epoll_create"); exit(-1); }
}
EventLoop::~EventLoop() { close(epFd_); }

void EventLoop::loop() {
    looping_ = true;
    while (!quit_) {
        int n = epoll_wait(epFd_, events_.data(), events_.size(), -1);
        if (n < 0) {
            if (errno == EINTR) continue;
            perror("epoll_wait");
            break;
        }
        for (int i = 0; i < n; ++i) {
            Channel* ch = static_cast<Channel*>(events_[i].data.ptr);
            ch->handleEvent(events_[i].events);
        }
    }
}
void EventLoop::quit() { quit_ = true; }

void EventLoop::updateChannel(Channel* ch) {
    epoll_event ev{};
    //ev.events = ch->fd() >= 0 ? (ch->events_ | EPOLLET) : 0; // 使用边缘触发 ET 效率更高，需配合循环读写
    // 注意：上面的代码为了简单兼容 LT，我们改回 LT (去掉 EPOLLET)，因为上面的读写逻辑没写 while 循环处理 EAGAIN
    
    ev.events = ch->fd() >= 0 ? ch->events() : 0; 
    
    ev.data.ptr = ch;
    
    int fd = ch->fd();
    if (channels_.find(fd) == channels_.end()) {
        channels_[fd] = ch;
        epoll_ctl(epFd_, EPOLL_CTL_ADD, fd, &ev);
    } else {
        epoll_ctl(epFd_, EPOLL_CTL_MOD, fd, &ev);
    }
}

void EventLoop::removeChannel(Channel* ch) {
    int fd = ch->fd();
    if (channels_.count(fd)) {
        epoll_ctl(epFd_, EPOLL_CTL_DEL, fd, nullptr);
        channels_.erase(fd);
    }
}

// --- HttpConnection Impl ---

HttpConnection::HttpConnection(EventLoop* loop, int fd) 
    : loop_(loop), fd_(fd), request_(std::make_unique<HttpRequest>()) {
    channel_ = std::make_unique<Channel>(loop, fd);
    channel_->setReadCallback([this]() { onRead(); });
    channel_->setWriteCallback([this]() { onWrite(); });
    channel_->setCloseCallback([this]() { handleClose(); });
    channel_->enableReading();
}

HttpConnection::~HttpConnection() {
    if (fileFd_ >= 0) close(fileFd_);
    if (fd_ >= 0) close(fd_);
}

void HttpConnection::onRead() {
    char buf[1024];
    ssize_t n = read(fd_, buf, sizeof(buf));
    if (n > 0) {
        inBuffer_.append(buf, n);
        if (request_->parse(inBuffer_.data(), inBuffer_.data() + inBuffer_.size())) {
            if (request_->isFinish()) {
                processRequest();
                inBuffer_.clear();
                request_->reset();
            }
        } else if (request_->hasError()) {
            sendError(400, "Bad Request");
            state_ = ConnectionState::WRITING; // 标记为需要发送错误信息
            channel_->enableWriting();
        }
    } else if (n == 0 || (n < 0 && errno != EAGAIN)) {
        handleClose();
    }
}

void HttpConnection::onWrite() {
    if (state_ == ConnectionState::CLOSING) {
        handleClose();
        return;
    }
    doWrite();
}

void HttpConnection::doWrite() {
    // 1. 先发送 outBuffer_ 中的内容 (Header 或 小 Body)
    if (!outBuffer_.empty()) {
        ssize_t n = write(fd_, outBuffer_.data(), outBuffer_.size());
        if (n > 0) {
            outBuffer_.erase(0, n);
            if (outBuffer_.empty() && fileFd_ == -1) {
                // 缓冲区空了，且没有文件要发 -> 完成
                state_ = ConnectionState::CLOSING;
                channel_->disableWriting();
                // 触发一次写事件以便关闭，或者直接在这里关闭
                // 为了逻辑统一，我们让 onWrite 再次被调用或直接 handleClose
                // 这里直接关闭比较快，但为了安全，我们禁用写监听，并在下次事件或手动关闭
                handleClose(); 
                return;
            }
        } else if (n < 0 && errno != EAGAIN) {
            handleClose();
            return;
        }
        // 如果 n < 0 (EAGAIN) 或 outBuffer_ 还有剩余，保持 EPOLLOUT 监听
    }

    // 2. 如果有文件要发 (fileFd_ != -1)
    if (fileFd_ >= 0 && outBuffer_.empty()) {
        off_t remaining = fileSize_ - fileOffset_;
        if (remaining <= 0) {
            // 文件发完了
            close(fileFd_);
            fileFd_ = -1;
            state_ = ConnectionState::CLOSING;
            channel_->disableWriting();
            handleClose();
            return;
        }

        ssize_t n = sendfile(fd_, fileFd_, &fileOffset_, remaining);
        if (n > 0) {
            // 成功发送一部分，继续
            if (fileOffset_ >= fileSize_) {
                close(fileFd_);
                fileFd_ = -1;
                state_ = ConnectionState::CLOSING;
                channel_->disableWriting();
                handleClose();
            }
        } else if (n < 0 && errno != EAGAIN) {
            perror("sendfile");
            handleClose();
        }
        // 如果 EAGAIN，保持 EPOLLOUT，下次继续
    }
}

void HttpConnection::processRequest() {
    std::string path = request_->getPath();
    if (path == "/hello") {
        prepareResponse("Hello World from C++ Server!!\n", "text/plain");
    } else {
        prepareFileResponse("./web" + path);
    }
    
    // 切换到写状态
    state_ = ConnectionState::WRITING;
    channel_->enableWriting();
    
    // 尝试立即发送一次，减少一次 epoll 唤醒
    doWrite();
}

void HttpConnection::prepareResponse(const std::string& body, const std::string& contentType) {
    std::string header = "HTTP/1.1 200 OK\r\n";
    header += "Content-Type: " + contentType + "\r\n";
    header += "Content-Length: " + std::to_string(body.size()) + "\r\n";
    header += "Connection: close\r\n";
    header += "\r\n";
    outBuffer_ = header + body;
    fileFd_ = -1;

    std::cout << ">>> Sending Response:" << std::endl;
    std::cout << outBuffer_ << std::endl; 
    std::cout << "----------------------" << std::endl;


}

void HttpConnection::prepareFileResponse(const std::string& path) {
    fileFd_ = open(path.c_str(), O_RDONLY);
    if (fileFd_ < 0) {
        sendError(404, "Not Found");
        return;
    }
    
    struct stat st;
    if (fstat(fileFd_, &st) < 0) {
        close(fileFd_);
        fileFd_ = -1;
        sendError(500, "Stat Error");
        return;
    }
    
    fileSize_ = st.st_size;
    fileOffset_ = 0;
    
    std::string header = "HTTP/1.1 200 OK\r\n";
    header += "Content-Type: text/html\r\n"; // 简单起见，默认 html
    header += "Content-Length: " + std::to_string(fileSize_) + "\r\n";
    header += "Connection: close\r\n";
    header += "\r\n";
    
    outBuffer_ = header;

    std::cout << ">>> Sending File Response:" << std::endl;
    std::cout << outBuffer_;
    std::cout << "[Body: " << path << " (" << fileSize_ << " bytes)]" << std::endl;
    std::cout << "----------------------" << std::endl;


}

void HttpConnection::sendError(int code, const std::string& msg) {
    std::string body = "<h1>" + std::to_string(code) + " " + msg + "</h1>";
    std::string header = "HTTP/1.1 " + std::to_string(code) + " " + msg + "\r\n";
    header += "Content-Type: text/html\r\n";
    header += "Content-Length: " + std::to_string(body.size()) + "\r\n";
    header += "Connection: close\r\n";
    header += "\r\n";
    outBuffer_ = header + body;
    fileFd_ = -1;
}

void HttpConnection::handleClose() {
    // 确保不再触发写事件
    channel_->disableWriting();
    loop_->removeChannel(channel_.get());
    // shared_ptr 会自动销毁此对象
}

// --- HttpServer Impl ---
HttpServer::HttpServer(int port) {
    serverFd_ = socket(AF_INET, SOCK_STREAM, 0);
    if (serverFd_ < 0) { perror("socket"); exit(-1); }
    
    int opt = 1;
    setsockopt(serverFd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    setNonBlocking(serverFd_);
    
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = INADDR_ANY;
    
    if (bind(serverFd_, (sockaddr*)&addr, sizeof(addr)) < 0) { perror("bind"); exit(-1); }
    if (listen(serverFd_, 1024) < 0) { perror("listen"); exit(-1); }
    
    acceptChannel_ = std::make_unique<Channel>(&loop_, serverFd_);
    acceptChannel_->setReadCallback([this]() { onAccept(); });
    acceptChannel_->enableReading();
}

void HttpServer::start() {
    std::cout << "Server started on port 8080..." << std::endl;
    std::cout << "Try: curl http://localhost:8080/" << std::endl;
    std::cout << "Try: curl http://localhost:8080/hello" << std::endl;
    loop_.loop();
}

void HttpServer::onAccept() {
    sockaddr_in clientAddr{};
    socklen_t len = sizeof(clientAddr);
    while (true) {
        int connFd = accept4(serverFd_, (sockaddr*)&clientAddr, &len, SOCK_NONBLOCK);
        if (connFd < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) break;
            perror("accept");
            break;
        }
        std::cout << "New connection: " << inet_ntoa(clientAddr.sin_addr) << ":" << ntohs(clientAddr.sin_port) << std::endl;
        auto conn = std::make_shared<HttpConnection>(&loop_, connFd);
        // conn 生命周期由 shared_ptr 管理
    }
}