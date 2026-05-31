# 轻量级高性能 HTTP 服务器

基于 C++11 从零构建的 HTTP/1.1 服务器。
使用 **epoll 边缘触发 + 线程池** 实现高并发 I/O，
**最小堆定时器** 管理空闲连接，
支持 **Keep-Alive 长连接**、**RESTful JSON API**、
**HTML 表单** 和 **静态文件服务**。

---

## 功能

- **I/O 多路复用** — epoll 边缘触发（EPOLLET）+ EPOLLONESHOT，安全分发连接到线程池
- **线程池** — 固定大小工作线程 + 线程安全阻塞队列 + 优雅关闭
- **全 HTTP/1.1 方法** — GET、HEAD、POST、PUT、DELETE、OPTIONS
- **Keep-Alive 长连接** — 单连接最多复用 100 次请求
- **RESTful JSON API** — `/api/data` 增删改查，数据持久化到文件
- **HTML 表单** — `/form` 页面 + POST `/submit` 提交（URL-encoded 解析）
- **静态文件** — 20+ 种 MIME 类型自动识别，路径遍历保护
- **最小堆定时器** — O(log n) 淘汰空闲连接（默认 60 秒）
- **信号处理** — SIGINT / SIGTERM 优雅退出
- **安全措施** — 请求大小限制（1MB body / 64KB header）、HTML 转义防 XSS

---

## 性能

```
wrk -t4 -c100 -d30s --latency http://127.0.0.1:8080/index.html

Running 30s test @ http://127.0.0.1:8080/index.html
  4 threads and 100 connections
  Thread Stats   Avg      Stdev     Max   +/- Stdev
    Latency    95.23ms   42.10ms  380.00ms   78.50%
    Req/Sec    268.50    102.30   450.00     62.30%
  Latency Distribution
     50%   85.20ms
     75%  120.30ms
     90%  155.40ms
     99%  280.10ms
  32200 requests in 30.02s, 39.86MB read
Requests/sec:   1072.58
Transfer/sec:   1.33MB
```

> **1072 req/s**，100 并发，普通硬件。Keep-Alive 避免了每次请求的 TCP 握手开销，
> 短连接模式下 QPS 约 421，提升超过 150%。

---

## 快速开始

```bash
# 1. 编译
cd reall_http
mkdir build && cd build
cmake .. && make -j$(nproc)

# 2. 启动（必须在项目根目录！）
cd ..
./build/reall_http 8080
```

**注意：** 服务器用相对路径查找静态文件，必须在包含 `index.html` 的目录启动。

---

## 路由一览

### 静态文件

| 方法 | 路径 | 说明 |
|------|------|------|
| GET | `/` | 重定向到 `/index.html` |
| GET | `/index.html` | 首页 |
| GET | `/<文件路径>` | 任意静态文件 |

支持的 MIME 类型：HTML、CSS、JS、JSON、XML、SVG、JPEG、PNG、GIF、ICO、
WebP、WOFF2、TTF、PDF、ZIP、MP4、MP3、WASM 等。

### 表单

| 方法 | 路径 | 说明 |
|------|------|------|
| GET | `/form` | 表单页面 |
| POST | `/submit` | 提交表单（URL-encoded） |

### REST API（`/api/data`）

| 方法 | 路径 | 说明 | 状态码 |
|------|------|------|--------|
| GET | `/api/data` | 列出全部 | 200 |
| GET | `/api/data/<id>` | 获取单条 | 200 / 404 |
| POST | `/api/data` | 创建（JSON body） | 201 / 400 / 415 |
| PUT | `/api/data/<id>` | 更新（JSON body） | 200 / 404 |
| DELETE | `/api/data/<id>` | 删除 | 200 / 404 |

所有响应均为 `application/json`，数据自动保存到 `api_data.json` 文件。

### 其他

| 方法 | 路径 | 说明 |
|------|------|------|
| HEAD | `/*` | 同 GET，只返回响应头 |
| OPTIONS | `/*` | 返回 `Allow` 头 |

---

## curl 测试

```bash
# 首页
curl -v http://localhost:8080/

# 提交表单
curl -X POST \
  -H "Content-Type: application/x-www-form-urlencoded" \
  -d "name=张三&email=test@test.com&message=你好" \
  http://localhost:8080/submit

# REST API 完整流程
curl -X POST -H "Content-Type: application/json" \
  -d '{"title":"笔记","content":"今天天气很好"}' \
  http://localhost:8080/api/data

curl http://localhost:8080/api/data              # 列表
curl http://localhost:8080/api/data/1             # 获取
curl -X PUT -H "Content-Type: application/json" \
  -d '{"title":"修改后的标题"}' \
  http://localhost:8080/api/data/1                # 更新
curl -X DELETE http://localhost:8080/api/data/1   # 删除

# HEAD
curl -I http://localhost:8080/

# OPTIONS
curl -X OPTIONS -v http://localhost:8080/

# 压测
wrk -t4 -c100 -d30s --latency http://127.0.0.1:8080/index.html
```

---

## 架构

```
┌─────────────────────────────────────────────────────────┐
│                      主线程（事件循环）                    │
│                                                          │
│  ┌──────────┐   ┌──────────┐   ┌──────────────────┐     │
│  │ epoll    │   │ Timer    │   │ ThreadPool       │     │
│  │ (ET)     │   │ (最小堆)  │   │ ┌────┐┌────┐    │     │
│  │          │   │          │   │ │ T1 ││ T2 │.. │     │
│  │ wait() ──┼───┤ tick()   │   │ └────┘└────┘    │     │
│  │          │   │          │   │  TaskQueue<λ>    │     │
│  └──────────┘   └──────────┘   └──────────────────┘     │
│       │                                                  │
│  ┌────┴──────────────────────────────────────┐           │
│  │ handle_events()                            │           │
│  │                                            │           │
│  │  listen_fd 就绪 → accept → 注册到 epoll     │           │
│  │  client EPOLLIN  → 丢给线程池解析请求        │           │
│  │  client EPOLLOUT → 丢给线程池写响应          │           │
│  │  client EPOLLERR → 清理关闭                  │           │
│  └────────────────────────────────────────────┘           │
└─────────────────────────────────────────────────────────┘

请求流程:
  1. accept 新连接 → 设置 O_NONBLOCK → epoll.add(EPOLLIN|ET|ONESHOT)
  2. EPOLLIN 触发 → 线程池处理 → HttpParser::parse()
  3. 解析完成 → handle_request() → 构造响应 → mod(EPOLLOUT)
  4. EPOLLOUT 触发 → send_response_data() → Keep-Alive: reset + 重新监听 EPOLLIN
  5. Timer::tick() 每秒驱动 → 标记超时 fd → 下次事件时关闭
```

---

## 项目结构

```
reall_http/
├── CMakeLists.txt            # CMake 构建配置（C++11, -O2, -pthread）
├── README.md                 # 项目说明
├── index.html                # 默认首页
├── include/
│   ├── server.h              # 服务器生命周期 & 事件循环
│   ├── epoll.h               # epoll 封装 + fd→parser 映射
│   ├── http_parser.h         # HTTP 解析器、路由、API 处理
│   ├── log.h                 # 线程安全日志（文件 + 控制台）
│   ├── queue.h               # ThreadSafeQueue<T>（头文件模板）
│   ├── thread_pool.h         # 固定大小线程池
│   └── timer.h               # 最小堆空闲连接定时器
└── src/
    ├── main.cpp              # 入口
    ├── server.cpp            # socket、信号处理、事件循环
    ├── epoll.cpp             # 事件分发到线程池
    ├── http_parser.cpp       # 解析、路由、静态文件、API
    ├── log.cpp               # 日志
    ├── thread_pool.cpp       # 线程池实现
    └── timer.cpp             # 最小堆定时器
```

---

## 技术要点

| 层面 | 实现 |
|------|------|
| **并发模型** | Reactor（epoll ET）+ 线程池（Proactor 风格） |
| **事件注册** | EPOLLONESHOT，每个 fd 同一时刻只被一个线程处理 |
| **HTTP 解析** | `std::istringstream` 分词 + 大小写不敏感 header 查找 |
| **Body 解析** | Content-Length 提取，1MB 上限，无 CL 时自动 fallback |
| **Keep-Alive** | 响应头 `Connection: keep-alive`，单连接最多 100 请求，超时刷新 |
| **定时器** | 最小堆，插入/删除 O(log n)，取最小 O(1)，默认 60 秒 TTL |
| **线程安全** | 每个子系统独立加锁：解析器映射、定时器堆、API 存储、日志 |
| **优雅关闭** | `sig_atomic_t` 标志 → 停止接受 → 排空线程池 → 关闭 socket |
| **安全** | 路径遍历防护（`/../` 检测 + `realpath`）、请求大小限制、HTML 转义 |

---

## 编译要求

- **系统:** Linux（kernel 2.6+，依赖 epoll）
- **编译器:** g++ 4.8+ 或 clang 3.3+（需支持 C++11）
- **工具:** CMake 3.10+、make
- **库:** pthread（系统自带）

---

## 未来方向

- [ ] 多 Reactor 架构（one loop per thread），进一步提升 QPS
- [ ] 静态文件内存缓存（避免每次 `stat` + `read`）
- [ ] 逐字节驱动的有限状态机 HTTP 解析器
- [ ] 异步日志（ring buffer + 后台线程批量刷盘）
- [ ] 配置文件支持（YAML/JSON）
- [ ] 单元测试（Google Test）
- [ ] HTTPS（OpenSSL）
- [ ] Chunked 传输编码
- [ ] 连接数限制 & 速率限制

---

## 许可证

MIT License
