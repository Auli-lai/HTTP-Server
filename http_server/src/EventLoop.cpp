#include "EventLoop.h"
#include "Channel.h"
#include <sys/epoll.h>
#include <unistd.h>
#include <cstring>
#include <iostream>

EventLoop::EventLoop() 
    : epollFd_(epoll_create1(EPOLL_CLOEXEC)), looping_(false), quit_(false) {
    if (epollFd_ < 0) {
        perror("epoll_create");
        exit(-1);
    }
    activeChannels_.resize(MAX_EVENTS);
}

EventLoop::~EventLoop() {
    close(epollFd_);
}

void EventLoop::loop() {
    looping_ = true;
    quit_ = false;
    while (!quit_) {
        int numEvents = epoll_wait(epollFd_, activeChannels_.data(), MAX_EVENTS, -1);
        if (numEvents < 0) {
            if (errno == EINTR) continue;
            perror("epoll_wait");
            break;
        }
        
        for (int i = 0; i < numEvents; ++i) {
            Channel* ch = static_cast<Channel*>(activeChannels_[i].data.ptr);
            ch->setRevents(activeChannels_[i].events);
            ch->handleEvent();
        }
    }
    looping_ = false;
}

void EventLoop::quit() { quit_ = true; }

void EventLoop::updateChannel(Channel* channel) {
    int fd = channel->fd();
    struct epoll_event ev;
    memset(&ev, 0, sizeof(ev));
    ev.events = channel->events();
    ev.data.ptr = channel;

    if (channels_.find(fd) == channels_.end()) {
        channels_[fd] = channel;
        epoll_ctl(epollFd_, EPOLL_CTL_ADD, fd, &ev);
    } else {
        epoll_ctl(epollFd_, EPOLL_CTL_MOD, fd, &ev);
    }
}

void EventLoop::removeChannel(Channel* channel) {
    int fd = channel->fd();
    if (channels_.find(fd) != channels_.end()) {
        epoll_ctl(epollFd_, EPOLL_CTL_DEL, fd, nullptr);
        channels_.erase(fd);
    }
}