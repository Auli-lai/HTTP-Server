#ifndef EPOLL_H
#define EPOLL_H

#include <vector>
#include <sys/epoll.h>
#include <unordered_map>
#include <set>
#include <mutex>

class ThreadPool;
class HttpParser;

class Epoll {
private:
    int epoll_fd;
    std::vector<struct epoll_event> events;
    std::unordered_map<int, HttpParser*> parsers;
    std::set<int> timed_out_fds;
    std::mutex mtx;

public:
    Epoll();
    ~Epoll();
    int add(int fd, uint32_t events);
    int mod(int fd, uint32_t events);
    int del(int fd);
    int wait(int timeout = -1);
    void handle_events(int listen_fd, ThreadPool* pool, int nfds);
    void mark_timed_out(int fd);
    bool is_timed_out(int fd);
    void remove_timed_out(int fd);
};

#endif // EPOLL_H
