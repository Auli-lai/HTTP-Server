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
```

🚀 Quick Start
1. Prerequisites
Linux OS (Recommended: Ubuntu 20.04 or later)
GCC/G++ compiler with C++11 support
CMake (version 3.10 or higher)

2. Build Instructions

```
# Clone the repository (or navigate to your project folder)
# git clone <your-repo-url>
# cd http_server

# Create a build directory
mkdir build
cd build

# Generate Makefiles
cmake ..

# Compile the project
make

```
3. Run the Server
```

./http_server

```

4. Testing

```
# Test dynamic text response
curl -v http://localhost:8080/hello

# Test static file response (ensure 'web/index.html' exists)
curl -v http://localhost:8080/index.html

```

📄 License

This project is licensed under the MIT License. Feel free to fork, modify, and submit pull requests!
