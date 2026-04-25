#include "server.h"
#include "log.h"
#include <iostream>

int main(int argc, char* argv[]) {
    // 初始化日志（异步写，避免阻塞工作线程）
    Log::init("server.log", true);

    // 默认端口8080
    uint16_t port = 8080;
    if (argc > 1) {
        port = std::stoi(argv[1]);
    }

    // 创建服务器
    Server server(port, "0.0.0.0", 4);
    Log::info("Starting server on port %d", port);

    // 启动服务器
    server.start();

    // 关闭日志
    Log::close();

    return 0;
}
