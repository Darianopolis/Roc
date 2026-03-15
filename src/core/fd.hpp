#pragma once

#include "object.hpp"
#include "enum.hpp"

// -----------------------------------------------------------------------------

namespace core::fd
{
    auto are_same(int fd0, int fd1) -> bool;
    auto dup_unsafe(int fd) -> int;
}

// -----------------------------------------------------------------------------

namespace core
{
    struct FdListener;
}

namespace core::fd
{
    void remove_listener(int fd);
    auto get_listener(   int fd) -> core::FdListener*;
    void set_listener(   int fd, core::FdListener*);
}

// -----------------------------------------------------------------------------

namespace core::fd
{
    static constexpr int max_fds = 1024;

    auto get_ref_count(int fd) -> u32;

    auto add_ref(   int fd) -> int;
    auto remove_ref(int fd) -> int;
}

namespace core
{
    struct Fd
    {
        int fd;

        Fd()
            : fd(-1)
        {}

        explicit Fd(int fd)
            : fd(fd)
        {
            core::fd::add_ref(fd);
        }

        Fd(const core::Fd& other)
            : fd(other.fd)
        {
            core::fd::add_ref(fd);
        }

        core::Fd& operator=(const core::Fd& other)
        {
            if (this != &other) {
                reset(other.fd);
            }
            return *this;
        }

        Fd(core::Fd&& other)
            : fd(std::exchange(other.fd, -1))
        {}

        core::Fd& operator=(core::Fd&& other)
        {
            if (this != &other) {
                core::fd::remove_ref(fd);
                fd = std::exchange(other.fd, -1);
            }
            return *this;
        }

        ~Fd()
        {
            core::fd::remove_ref(fd);
        }

        void reset(int new_fd = -1)
        {
            core::fd::remove_ref(fd);
            fd = core::fd::add_ref(new_fd);
        }

        core::Fd& operator=(std::nullptr_t)
        {
            reset();
            return *this;
        }

        int get() const noexcept { return fd; }

        explicit operator bool() const noexcept { return fd >= 0; }
    };
}

// -----------------------------------------------------------------------------

namespace core::fd
{
    auto adopt(    int fd) -> core::Fd;
    auto reference(int fd) -> core::Fd;
    auto dup(      int fd) -> core::Fd;
}
