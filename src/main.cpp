#include "server.h"
#include "http_parser.h"
#include "log.h"
#include <iostream>

int main(int argc, char* argv[]) {
    // 初始化日志
    Log::init("server.log", false);

    // 加载持久化的 API 数据
    api_store_load();

    // 默认端口8080
    uint16_t port = 8080;
    if (argc > 1) {
        port = std::stoi(argv[1]);
    }

    // 创建并启动服务器
    Server server(port, "0.0.0.0", 4);
    Log::info("Starting reall_http/2.0 on port %d (Keep-Alive enabled)", port);
    Log::info("Endpoints: http://localhost:%d/ | http://localhost:%d/form | http://localhost:%d/api/data",
              port, port, port);

    server.start();

    Log::close();
    return 0;
}
