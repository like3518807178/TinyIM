#ifndef EVENTLOOP_H
#define EVENTLOOP_H

#include "Logger.h"
#include "ScopedFd.h"

#include <vector>

class EventLoop {
    public:
        explicit EventLoop(Logger& logger);

        bool Init();
        bool AddReadable(int fd);
        std::vector<int> Wait(int timeout_ms);

    private:
        Logger& logger_;
        ScopedFd epoll_fd_;
};

#endif
