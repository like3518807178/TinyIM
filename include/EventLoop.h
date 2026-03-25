#ifndef EVENTLOOP_H
#define EVENTLOOP_H

#include "Logger.h"
#include "ScopedFd.h"

#include <cstdint>
#include <vector>

class EventLoop {
    public:
        struct ReadyEvent {
            int fd;
            bool readable;
            bool writable;
            bool error;
            bool hangup;
        };

        explicit EventLoop(Logger& logger);

        bool Init();
        bool Add(int fd, std::uint32_t events);
        bool Modify(int fd, std::uint32_t events);
        bool Remove(int fd);
        std::vector<ReadyEvent> Wait(int timeout_ms);

    private:
        Logger& logger_;
        ScopedFd epoll_fd_;
};

#endif
