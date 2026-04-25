#include "thread_pool.h"
#include "log.h"
#include <iostream>

ThreadPool::ThreadPool(int thread_num) : thread_num(thread_num), running(false) {
}

ThreadPool::~ThreadPool() {
    stop();
}

void ThreadPool::start() {
    running = true;
    for (int i = 0; i < thread_num; ++i) {
        threads.emplace_back([this]() {
            while (running) {
                std::function<void()> task;
                if (task_queue.pop(task)) {
                    try {
                        task();
                    } catch (const std::exception& e) {
                        Log::error("Task execution failed: %s", e.what());
                    }
                } else {
                    break; // 队列已关闭
                }
            }
        });
    }
    Log::info("Thread pool started with %d threads", thread_num);
}

void ThreadPool::stop() {
    running = false;
    task_queue.shutdown_queue();
    for (auto& thread : threads) {
        if (thread.joinable()) {
            thread.join();
        }
    }
    Log::info("Thread pool stopped");
}

void ThreadPool::add_task(std::function<void()> task) {
    task_queue.push(task);
}
