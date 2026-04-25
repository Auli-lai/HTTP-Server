#ifndef SERVER_H
#define SERVER_H

#include <string>
#include <cstdint>

class Server {
private:
    int listen_fd;
    uint16_t port;
    std::string ip;
    int thread_num;

public:
    Server(uint16_t port, const std::string& ip = "0.0.0.0", int thread_num = 4);
    ~Server();
    void start();
    void stop();
};

#endif // SERVER_H
