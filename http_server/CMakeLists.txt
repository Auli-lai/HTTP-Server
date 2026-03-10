cmake_minimum_required(VERSION 3.10)
project(HttpServer)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_BUILD_TYPE Release)

find_package(Threads REQUIRED)

# 修改这里：只包含实际存在的文件
add_executable(http_server
    src/main.cpp
    src/HttpServer.cpp
    # 移除了 src/HttpRequest.cpp
)

target_link_libraries(http_server Threads::Threads)