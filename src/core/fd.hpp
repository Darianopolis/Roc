#pragma once

#include "types.hpp"

// -----------------------------------------------------------------------------

static constexpr int fd_limit = 1024;

// -----------------------------------------------------------------------------

auto fd_are_same(int fd0, int fd1) -> bool;
auto fd_dup_unsafe(int fd) -> int;

// -----------------------------------------------------------------------------

inline
auto fd_is_valid(int fd) -> bool
{
    return fd >= 0 && fd < fd_limit;
}

auto fd_get_ref_count(int fd) -> u32;

auto fd_add_ref(   int fd) -> int;
auto fd_remove_ref(int fd) -> int;

auto fd_extract(int fd) -> int;

struct Fd
{
    int fd;

    Fd()
        : fd(-1)
    {}

    explicit Fd(int fd)
        : fd(fd)
    {
        fd_add_ref(fd);
    }

    Fd(const Fd& other)
        : fd(other.fd)
    {
        fd_add_ref(fd);
    }

    auto& operator=(const Fd& other)
    {
        if (this != &other) {
            reset(other.fd);
        }
        return *this;
    }

    Fd(Fd&& other)
        : fd(std::exchange(other.fd, -1))
    {}

    auto& operator=(Fd&& other)
    {
        if (this != &other) {
            fd_remove_ref(fd);
            fd = std::exchange(other.fd, -1);
        }
        return *this;
    }

    ~Fd()
    {
        fd_remove_ref(fd);
    }

    void reset(int new_fd = -1)
    {
        fd_remove_ref(fd);
        fd = fd_add_ref(new_fd);
    }

    auto& operator=(std::nullptr_t)
    {
        reset();
        return *this;
    }

    auto get() const noexcept -> int { return fd; }

    auto extract() noexcept -> int;

    explicit operator bool() const noexcept { return fd >= 0; }
};
