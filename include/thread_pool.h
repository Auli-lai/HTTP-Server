#ifndef THREAD_POOL_H
#define THREAD_POOL_H

#include <vector>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <functional>
#include "queue.h"

class ThreadPool {
private:
    int thread_num;
    std::vector<std::thread> threads;
    ThreadSafeQueue<std::function<void()>> task_queue;
    std::mutex mtx;
    std::condition_variable cv;
    bool running;

public:
    ThreadPool(int thread_num);
    ~ThreadPool();
    void start();
    void stop();
    void add_task(std::function<void()> task);
};

#endif // THREAD_POOL_H
