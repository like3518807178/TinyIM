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

bool EventLoop::AddReadable(int fd) {
    epoll_event event{};
    event.events = EPOLLIN;
    event.data.fd = fd;

    if (epoll_ctl(epoll_fd_.Get(), EPOLL_CTL_ADD, fd, &event) < 0) {
        logger_.Error("epoll_ctl(ADD) failed for fd=" + std::to_string(fd) +
                      ": " + std::string(std::strerror(errno)));
        return false;
    }

    return true;
}

std::vector<int> EventLoop::Wait(int timeout_ms) {
    std::vector<epoll_event> events(kMaxEvents);

    while (true) {
        int ready = epoll_wait(epoll_fd_.Get(), events.data(), kMaxEvents, timeout_ms);
        if (ready > 0) {
            std::vector<int> ready_fds;
            ready_fds.reserve(static_cast<std::size_t>(ready));
            for (int i = 0; i < ready; ++i) {
                ready_fds.push_back(events[i].data.fd);
            }
            return ready_fds;
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
