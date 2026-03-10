#include "Channel.h"
#include "EventLoop.h"
#include <sys/epoll.h>
#include <unistd.h>
#include <iostream>

Channel::Channel(EventLoop* loop, int fd)
    : loop_(loop), fd_(fd), events_(0), revents_(0), addedToLoop_(false) {}

Channel::~Channel() {
    if (addedToLoop_) {
        loop_->removeChannel(this);
    }
    // 注意：通常不在此处 close(fd)，由上层管理生命周期，或者在此处close
    // 为了安全，这里假设上层已关闭或此处关闭
    // close(fd_); 
}

void Channel::update() {
    loop_->updateChannel(this);
    addedToLoop_ = true;
}

void Channel::enableReading() {
    events_ |= EPOLLIN;
    update();
}

void Channel::disableReading() {
    events_ &= ~EPOLLIN;
    update();
}

void Channel::handleEvent() {
    if (revents_ & (EPOLLIN | EPOLLPRI)) {
        if (readCallback_) readCallback_();
    }
    if (revents_ & (EPOLLHUP | EPOLLERR)) {
        if (closeCallback_) closeCallback_();
    }
}