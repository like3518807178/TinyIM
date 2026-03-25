#include "EventLoop.h"

#include <cerrno>
#include <cstring>

#include <sys/epoll.h>

namespace {
constexpr int kMaxEvents = 16;
}

EventLoop::EventLoop(Logger& logger) : logger_(logger) {}

bool EventLoop::Init() {
    int epoll_fd = epoll_create1(0);
    if (epoll_fd < 0) {
        logger_.Error("epoll_create1() failed: " + std::string(std::strerror(errno)));
        return false;
    }

    epoll_fd_.Reset(epoll_fd);
    return true;
}

bool EventLoop::Add(int fd, std::uint32_t events) {
    epoll_event event{};
    event.events = events;
    event.data.fd = fd;

    if (epoll_ctl(epoll_fd_.Get(), EPOLL_CTL_ADD, fd, &event) < 0) {
        logger_.Error("epoll_ctl(ADD) failed for fd=" + std::to_string(fd) +
                      ": " + std::string(std::strerror(errno)));
        return false;
    }

    return true;
}

bool EventLoop::Modify(int fd, std::uint32_t events) {
    epoll_event event{};
    event.events = events;
    event.data.fd = fd;

    if (epoll_ctl(epoll_fd_.Get(), EPOLL_CTL_MOD, fd, &event) < 0) {
        logger_.Error("epoll_ctl(MOD) failed for fd=" + std::to_string(fd) +
                      ": " + std::string(std::strerror(errno)));
        return false;
    }

    return true;
}

bool EventLoop::Remove(int fd) {
    if (epoll_ctl(epoll_fd_.Get(), EPOLL_CTL_DEL, fd, nullptr) < 0) {
        logger_.Error("epoll_ctl(DEL) failed for fd=" + std::to_string(fd) +
                      ": " + std::string(std::strerror(errno)));
        return false;
    }

    return true;
}

std::vector<EventLoop::ReadyEvent> EventLoop::Wait(int timeout_ms) {
    std::vector<epoll_event> events(kMaxEvents);

    while (true) {
        int ready = epoll_wait(epoll_fd_.Get(), events.data(), kMaxEvents, timeout_ms);
        if (ready > 0) {
            std::vector<ReadyEvent> ready_events;
            ready_events.reserve(static_cast<std::size_t>(ready));
            for (int i = 0; i < ready; ++i) {
                std::uint32_t mask = events[i].events;
                ready_events.push_back(ReadyEvent{
                    events[i].data.fd,
                    (mask & EPOLLIN) != 0,
                    (mask & EPOLLOUT) != 0,
                    (mask & EPOLLERR) != 0,
                    (mask & (EPOLLHUP | EPOLLRDHUP)) != 0
                });
            }
            return ready_events;
        }

        if (ready == 0) {
            return {};
        }

        if (errno == EINTR) {
            continue;
        }

        logger_.Error("epoll_wait() failed: " + std::string(std::strerror(errno)));
        return {};
    }
}
