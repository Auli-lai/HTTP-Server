# 轻量级HTTP服务器

## 项目介绍

轻量级HTTP服务器是一个基于Linux的轻量级HTTP服务器，使用C++语言开发，实现了以下功能：

1. 利用I/O多路复用技术Epoll与线程池实现Proactor并发模型
2. 利用有限状态机解析HTTP请求，实现处理静态资源的请求
3. 利用线程安全的队列封装每一个任务，避免出现竞态条件
4. 基于小顶堆实现的定时器处理非活动连接
5. 支持同步/异步写入日志

## 技术栈

- Linux
- C++
- Socket
- TCP
- Epoll
- Thread Pool
- CMake

## 项目结构

```
Lightweight_http/
├── CMakeLists.txt      # CMake配置文件
├── README.md           # 项目说明文档
├── index.html          # 静态页面
├── include/            # 头文件目录
│   ├── server.h        # 服务器类头文件
│   ├── epoll.h         # Epoll类头文件
│   ├── thread_pool.h   # 线程池类头文件
│   ├── http_parser.h   # HTTP解析器类头文件
│   ├── timer.h         # 定时器类头文件
│   ├── log.h           # 日志类头文件
│   └── queue.h         # 线程安全队列头文件
└── src/                # 源文件目录
    ├── main.cpp        # 主入口文件
    ├── server.cpp      # 服务器类实现
    ├── epoll.cpp       # Epoll类实现
    ├── thread_pool.cpp # 线程池类实现
    ├── http_parser.cpp # HTTP解析器类实现
    ├── timer.cpp       # 定时器类实现
    └── log.cpp         # 日志类实现
```

## 编译和运行

### 编译

```bash
# 创建构建目录
mkdir build
cd build

# 运行CMake
cmake ..

# 编译
make
```

### 运行

```bash
# 启动服务器，默认端口8080
./reall_http

# 或者指定端口
./reall_http 8000

#需要把index.html文件放入build文件夹中

```

## 功能说明

1. **并发模型**：使用Epoll实现I/O多路复用，结合线程池处理请求，提高并发性能
2. **HTTP解析**：使用有限状态机解析HTTP请求，支持GET方法
3. **静态资源处理**：支持处理HTML、CSS、JavaScript、图片等静态资源
4. **定时器**：基于小顶堆实现的定时器，处理非活动连接，防止资源泄漏
5. **日志系统**：支持同步/异步写入日志，记录服务器运行状态和请求信息

## 测试

1. 启动服务器
2. 在浏览器中访问 `http://localhost:8080`
3. 可以尝试访问静态文件，如 `http://localhost:8080/index.html`

## 注意事项

1. 本服务器仅支持静态资源的处理，不支持动态内容
2. 服务器在Linux环境下运行，不支持Windows
3. 服务器使用默认的8080端口，需要确保该端口未被占用
4. 静态资源文件需要放在服务器运行目录下

## 性能优化

1. 使用Epoll的边缘触发模式，减少系统调用
2. 使用线程池处理请求，避免频繁创建和销毁线程
3. 使用非阻塞I/O，提高并发性能
4. 使用定时器管理非活动连接，释放资源

## 压力测试(wrk)

wrk -t4 -c100 -d30s --latency http://127.0.0.1:8080/index.html
 Running 30s test @ http://127.0.0.1:8080/index.html
   4 threads and 100 connections
   Thread Stats   Avg      Stdev     Max   +/- Stdev
     Latency   252.62ms  146.20ms   1.25s    83.62%
     Req/Sec   114.86     72.46   282.00     57.59%
   Latency Distribution
      50%  217.57ms
      75%  240.40ms
      90%  337.57ms
      99%  969.53ms
   12657 requests in 30.03s, 15.61MB read
 Requests/sec:    421.47
 Transfer/sec:    532.19KB

4线程，100连接数量，持续30s

QPS : 421.47

50%的请求在217.57ms内完成
75%的请求在240.40ms内完成
90%的请求在337.57ms内完成
99%的请求在969.53ms内完成

## 未来扩展

1. 支持更多HTTP方法，如POST、PUT、DELETE等
2. 支持动态内容生成，如CGI或FastCGI
3. 支持HTTPS
4. 支持虚拟主机
5. 支持缓存机制

## 许可证

本项目采用MIT许可证
