cmake_minimum_required(VERSION 3.10)
project(reall_http)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 编译选项
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -O2 -pthread")

# 包含目录
include_directories(include)

# 源文件
set(SOURCE_FILES
    src/main.cpp
    src/server.cpp
    src/epoll.cpp
    src/thread_pool.cpp
    src/http_parser.cpp
    src/timer.cpp
    src/log.cpp
)

# 头文件
set(HEADER_FILES
    include/server.h
    include/epoll.h
    include/thread_pool.h
    include/http_parser.h
    include/timer.h
    include/log.h
    include/queue.h
)

# 构建可执行文件
add_executable(reall_http ${SOURCE_FILES} ${HEADER_FILES})

# 链接库
if(UNIX)
    target_link_libraries(reall_http pthread)
endif()
