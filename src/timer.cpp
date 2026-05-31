#include "timer.h"
#include "log.h"
#include <unistd.h>

std::vector<Timer::TimerNode> Timer::heap;
std::unordered_map<int, size_t> Timer::fd_to_heap;
std::mutex Timer::mtx;

void Timer::add(int fd, int timeout) {
    std::lock_guard<std::mutex> lock(mtx);
    time_t expire = time(nullptr) + timeout;
    heap.push_back({fd, expire});
    size_t i = heap.size() - 1;
    fd_to_heap[fd] = i;
    sift_up(i);
}

void Timer::refresh(int fd, int timeout) {
    std::lock_guard<std::mutex> lock(mtx);
    auto it = fd_to_heap.find(fd);
    if (it != fd_to_heap.end()) {
        size_t i = it->second;
        time_t new_expire = time(nullptr) + timeout;
        time_t old_expire = heap[i].expire;
        heap[i].expire = new_expire;
        // 根据新旧过期时间决定上浮或下沉
        if (new_expire < old_expire) {
            sift_up(i);
        } else {
            sift_down(i);
        }
    }
}

void Timer::remove(int fd) {
    std::lock_guard<std::mutex> lock(mtx);
    auto it = fd_to_heap.find(fd);
    if (it != fd_to_heap.end()) {
        size_t i = it->second;
        if (i < heap.size()) {
            swap(i, heap.size() - 1);
            heap.pop_back();
            fd_to_heap.erase(it);
            if (i < heap.size()) {
                sift_up(i);
                sift_down(i);
            }
        }
    }
}

std::vector<int> Timer::tick() {
    std::vector<int> timed_out_fds;
    std::lock_guard<std::mutex> lock(mtx);
    time_t now = time(nullptr);
    while (!heap.empty() && heap[0].expire <= now) {
        int fd = heap[0].fd;
        fd_to_heap.erase(fd);
        swap(0, heap.size() - 1);
        heap.pop_back();
        if (!heap.empty()) {
            sift_down(0);
        }
        timed_out_fds.push_back(fd);
        Log::info("Connection %d timed out", fd);
    }
    return timed_out_fds;
}

void Timer::heapify(size_t i) {
    sift_down(i);
}

void Timer::sift_up(size_t i) {
    while (i > 0) {
        size_t parent = (i - 1) / 2;
        if (heap[i] < heap[parent]) {
            swap(i, parent);
            i = parent;
        } else {
            break;
        }
    }
}

void Timer::sift_down(size_t i) {
    size_t n = heap.size();
    while (true) {
        size_t left = 2 * i + 1;
        size_t right = 2 * i + 2;
        size_t smallest = i;
        if (left < n && heap[left] < heap[smallest]) {
            smallest = left;
        }
        if (right < n && heap[right] < heap[smallest]) {
            smallest = right;
        }
        if (smallest != i) {
            swap(i, smallest);
            i = smallest;
        } else {
            break;
        }
    }
}

void Timer::swap(size_t i, size_t j) {
    std::swap(heap[i], heap[j]);
    fd_to_heap[heap[i].fd] = i;
    fd_to_heap[heap[j].fd] = j;
}
