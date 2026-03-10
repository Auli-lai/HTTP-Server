# 🚀 C++ High-Performance HTTP Server

A high-performance, non-blocking HTTP server implemented in **Modern C++** based on the **Linux epoll** mechanism and the **Reactor pattern**. This project demonstrates a deep understanding of network programming core concepts, including event-driven architecture, finite state machine parsing, and zero-copy I/O.

## ✨ Key Features

- **🏗️ Reactor Pattern**: Implements a single-threaded event loop (`EventLoop`) to efficiently handle high concurrency without thread overhead per connection.
- **⚡ Non-Blocking I/O**: Utilizes Linux `epoll` (supporting both Level-Triggered and Edge-Triggered modes) to monitor socket events without blocking.
- **🔄 Finite State Machine (FSM)**: A custom handwritten HTTP request parser that accurately processes request lines, headers, and bodies.
- **📦 Zero-Copy Transmission**: Optimizes static file serving using the `sendfile` system call, minimizing context switches and memory copies between kernel and user space.
- **🔌 Connection Management**: Robust handling of connection lifecycle, including automatic cleanup and error handling.
- **🛠️ Modern C++ Standards**: Built with C++11/14 features, leveraging `std::function`, `std::bind`, and smart pointers (`std::shared_ptr`, `std::unique_ptr`) for safe resource management.

## 🛠️ Tech Stack

- **Language**: C++ (C++11/14)
- **Build System**: CMake, Make
- **System APIs**: `epoll`, `socket`, `bind`, `listen`, `accept`, `sendfile`, `fstat`, `non-blocking IO`
- **OS**: Linux (Ubuntu/Debian/CentOS)
- **Tools**: GCC/G++, GDB, Valgrind

## 📂 Project Structure

```text
.
├── CMakeLists.txt
├── src
│   ├── Channel.cpp
│   ├── Channel.h
│   ├── EventLoop.cpp
│   ├── EventLoop.h
│   ├── HttpRequest.h
│   ├── HttpServer.cpp
│   ├── HttpServer.h
│   └── main.cpp
└── web
    └── index.html
