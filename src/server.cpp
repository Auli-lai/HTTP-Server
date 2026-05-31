#include "server.h"
#include "epoll.h"
#include "thread_pool.h"
#include "timer.h"
#include "log.h"
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <cstring>
#include <iostream>
#include <csignal>

// 全局信号标志
static volatile sig_atomic_t g_running = 1;

static void signal_handler(int signo) {
    if (signo == SIGINT || signo == SIGTERM) {
        g_running = 0;
    }
}

Server::Server(uint16_t port, const std::string& ip, int thread_num) 
    : port(port), ip(ip), thread_num(thread_num), listen_fd(-1) {
}

Server::~Server() {
    if (listen_fd != -1) {
        close(listen_fd);
    }
}

void Server::start() {
    // 注册信号处理
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = signal_handler;
    sa.sa_flags = SA_RESTART;
    sigaction(SIGINT, &sa, nullptr);
    sigaction(SIGTERM, &sa, nullptr);
    signal(SIGPIPE, SIG_IGN); // 忽略 SIGPIPE，防止写入已关闭的连接导致进程退出

    // 创建监听套接字
    listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd == -1) {
        Log::error("Failed to create socket");
        return;
    }

    // 设置套接字选项
    int opt = 1;
    if (setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) == -1) {
        Log::error("Failed to set socket option");
        close(listen_fd);
        return;
    }

    // 绑定地址
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = inet_addr(ip.c_str());

    if (bind(listen_fd, (struct sockaddr*)&addr, sizeof(addr)) == -1) {
        Log::error("Failed to bind socket");
        close(listen_fd);
        return;
    }

    // 开始监听
    if (listen(listen_fd, 1024) == -1) {
        Log::error("Failed to listen");
        close(listen_fd);
        return;
    }

    Log::info("Server started on %s:%d", ip.c_str(), port);

    // 初始化线程池
    ThreadPool pool(thread_num);
    pool.start();

    // 初始化Epoll
    Epoll epoll;
    epoll.add(listen_fd, EPOLLIN | EPOLLET);

    // 主循环
    while (g_running) {
        int nfds = epoll.wait(1000); // 1秒超时，确保定时器能定期被驱动
        if (nfds == -1) {
            if (errno == EINTR) {
                continue; // 被信号中断，重新检查 g_running
            }
            Log::error("Epoll wait failed");
            continue;
        }

        // 驱动定时器，获取超时fd列表并标记
        std::vector<int> timed_out_fds = Timer::tick();
        for (int fd : timed_out_fds) {
            epoll.mark_timed_out(fd);
        }

        // 处理事件
        epoll.handle_events(listen_fd, &pool, nfds);
    }

    // 优雅关闭
    Log::info("Shutting down...");
    pool.stop();
    close(listen_fd);
    listen_fd = -1;
    Log::info("Server stopped");
}

void Server::stop() {
    if (listen_fd != -1) {
        close(listen_fd);
        listen_fd = -1;
    }
    Log::info("Server stopped");
}
