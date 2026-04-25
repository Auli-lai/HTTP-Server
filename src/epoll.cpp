#include "epoll.h"
#include "thread_pool.h"
#include "http_parser.h"
#include "timer.h"
#include "log.h"
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <cstring>
#include <iostream>
#include <unordered_map>

Epoll::Epoll() {
    epoll_fd = epoll_create1(0);
    if (epoll_fd == -1) {
        Log::error("Failed to create epoll");
    }
    events.resize(1024);
}

Epoll::~Epoll() {
    if (epoll_fd != -1) {
        close(epoll_fd);
    }
    std::lock_guard<std::mutex> lock(mtx);
    for (auto& pair : parsers) {
        delete pair.second;
    }
}

int Epoll::add(int fd, uint32_t events) {
    struct epoll_event event;
    event.data.fd = fd;
    event.events = events;
    return epoll_ctl(epoll_fd, EPOLL_CTL_ADD, fd, &event);
}

int Epoll::mod(int fd, uint32_t events) {
    struct epoll_event event;
    event.data.fd = fd;
    event.events = events;
    return epoll_ctl(epoll_fd, EPOLL_CTL_MOD, fd, &event);
}

int Epoll::del(int fd) {
    return epoll_ctl(epoll_fd, EPOLL_CTL_DEL, fd, nullptr);
}

int Epoll::wait(int timeout) {
    return epoll_wait(epoll_fd, events.data(), events.size(), timeout);
}

void Epoll::mark_timed_out(int fd) {
    std::lock_guard<std::mutex> lock(mtx);
    timed_out_fds.insert(fd);
}

bool Epoll::is_timed_out(int fd) {
    std::lock_guard<std::mutex> lock(mtx);
    return timed_out_fds.find(fd) != timed_out_fds.end();
}

void Epoll::remove_timed_out(int fd) {
    std::lock_guard<std::mutex> lock(mtx);
    timed_out_fds.erase(fd);
}

void Epoll::handle_events(int listen_fd, ThreadPool* pool, int nfds) {
    for (int i = 0; i < nfds; ++i) {
        int fd = events[i].data.fd;
        uint32_t event = events[i].events;

        if (fd == listen_fd) {
            struct sockaddr_in client_addr;
            socklen_t client_len = sizeof(client_addr);
            int client_fd = accept(listen_fd, (struct sockaddr*)&client_addr, &client_len);
            if (client_fd == -1) {
                Log::error("Failed to accept connection");
                continue;
            }

            int flags = fcntl(client_fd, F_GETFL, 0);
            fcntl(client_fd, F_SETFL, flags | O_NONBLOCK);

            add(client_fd, EPOLLIN | EPOLLET | EPOLLONESHOT);
            Timer::add(client_fd, 60);

            Log::info("New connection from %s:%d", 
                inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port));
        } else if (event & EPOLLIN) {
            pool->add_task([fd, this]() {
                if (is_timed_out(fd)) {
                    std::lock_guard<std::mutex> lock(mtx);
                    auto it = parsers.find(fd);
                    if (it != parsers.end()) {
                        delete it->second;
                        parsers.erase(it);
                    }
                    del(fd);
                    close(fd);
                    Timer::remove(fd);
                    remove_timed_out(fd);
                    return;
                }

                HttpParser* parser = nullptr;
                {
                    std::lock_guard<std::mutex> lock(mtx);
                    auto it = parsers.find(fd);
                    if (it != parsers.end()) {
                        parser = it->second;
                    } else {
                        parser = new HttpParser(fd);
                        parsers[fd] = parser;
                    }
                }
                
                HttpParser::ParseResult result = parser->parse();
                if (result == HttpParser::PARSE_OK) {  
                    parser->handle_request();
                    mod(fd, EPOLLOUT | EPOLLET | EPOLLONESHOT);
                } else if (result == HttpParser::PARSE_AGAIN) {
                    mod(fd, EPOLLIN | EPOLLET | EPOLLONESHOT);
                } else {
                    std::lock_guard<std::mutex> lock(mtx);
                    auto it = parsers.find(fd);
                    if (it != parsers.end()) {
                        delete it->second;
                        parsers.erase(it);
                    }
                    del(fd);
                    close(fd);
                    Timer::remove(fd);
                }
            });
        } else if (event & EPOLLOUT) {
            pool->add_task([fd, this]() {
                if (is_timed_out(fd)) {
                    std::lock_guard<std::mutex> lock(mtx);
                    auto it = parsers.find(fd);
                    if (it != parsers.end()) {
                        delete it->second;
                        parsers.erase(it);
                    }
                    del(fd);
                    close(fd);
                    Timer::remove(fd);
                    remove_timed_out(fd);
                    return;
                }

                HttpParser* parser = nullptr;
                {
                    std::lock_guard<std::mutex> lock(mtx);
                    auto it = parsers.find(fd);
                    if (it != parsers.end()) {
                        parser = it->second;
                    } else {
                        del(fd);
                        close(fd);
                        Timer::remove(fd);
                        return;
                    }
                }

                if (parser->send_response_data()) {
                    std::lock_guard<std::mutex> lock(mtx);
                    auto it = parsers.find(fd);
                    if (it != parsers.end()) {
                        delete it->second;
                        parsers.erase(it);
                    }
                    del(fd);
                    close(fd);
                    Timer::remove(fd);
                } else {
                    mod(fd, EPOLLOUT | EPOLLET | EPOLLONESHOT);
                }
            });
        } else if (event & EPOLLERR || event & EPOLLHUP) {
            pool->add_task([fd, this]() {
                std::lock_guard<std::mutex> lock(mtx);
                auto it = parsers.find(fd);
                if (it != parsers.end()) {
                    delete it->second;
                    parsers.erase(it);
                }
                del(fd);
                close(fd);
                Timer::remove(fd);
                timed_out_fds.erase(fd);
            });
        }
    }
}
