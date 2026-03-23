#ifndef SCOPEDFD_H
#define SCOPEDFD_H

#include <unistd.h>

class ScopedFd {
    public:
        ScopedFd() = default;
        explicit ScopedFd(int fd) : fd_(fd) {}

        ~ScopedFd() {
            Reset();
        }

        ScopedFd(const ScopedFd&) = delete;
        ScopedFd& operator=(const ScopedFd&) = delete;

        ScopedFd(ScopedFd&& other) noexcept : fd_(other.Release()) {}

        ScopedFd& operator=(ScopedFd&& other) noexcept {
            if (this != &other) {
                Reset(other.Release());
            }
            return *this;
        }

        int Get() const {
            return fd_;
        }

        bool IsValid() const {
            return fd_ >= 0;
        }

        void Reset(int fd = -1) {
            if (fd_ >= 0) {
                close(fd_);
            }
            fd_ = fd;
        }

        int Release() {
            int fd = fd_;
            fd_ = -1;
            return fd;
        }

    private:
        int fd_ = -1;
};

#endif
