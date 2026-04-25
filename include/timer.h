#ifndef TIMER_H
#define TIMER_H

#include <unordered_map>
#include <vector>
#include <functional>
#include <ctime>
#include <mutex>

class Timer {
private:
    struct TimerNode {
        int fd;
        time_t expire;
        bool operator<(const TimerNode& other) const {
            return expire > other.expire; // 小顶堆
        }
    };

    static std::vector<TimerNode> heap;
    static std::unordered_map<int, size_t> fd_to_heap;
    static std::mutex mtx;

public:
    static void add(int fd, int timeout);
    static void remove(int fd);
    static std::vector<int> tick();

private:
    static void heapify(size_t i);
    static void sift_up(size_t i);
    static void sift_down(size_t i);
    static void swap(size_t i, size_t j);
};

#endif // TIMER_H
