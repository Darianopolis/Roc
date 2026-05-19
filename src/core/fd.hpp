#pragma once

#include "types.hpp"

// -----------------------------------------------------------------------------

using fd_t = int;

static constexpr fd_t fd_limit = 1024;

// -----------------------------------------------------------------------------

void fd_registry_init();
void fd_registry_deinit();

// -----------------------------------------------------------------------------

auto fd_are_same(fd_t fd0, fd_t fd1) -> bool;
auto fd_dup_unsafe(fd_t fd) -> fd_t;

// -----------------------------------------------------------------------------

inline
auto fd_is_valid(fd_t fd) -> bool
{
    return fd >= 0 && fd < fd_limit;
}

auto fd_get_ref_count(fd_t fd) -> u32;

auto fd_ref(fd_t fd) -> fd_t;
auto fd_unref(fd_t fd) -> fd_t;

auto fd_extract(fd_t fd) -> fd_t;

struct Fd
{
    fd_t fd;

    Fd()
        : fd(-1)
    {}

    explicit Fd(fd_t fd)
        : fd(fd)
    {
        fd_ref(fd);
    }

    Fd(const Fd& other)
        : fd(other.fd)
    {
        fd_ref(fd);
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
            fd_unref(fd);
            fd = std::exchange(other.fd, -1);
        }
        return *this;
    }

    ~Fd()
    {
        fd_unref(fd);
    }

    void reset(fd_t new_fd = -1)
    {
        fd_unref(fd);
        fd = fd_ref(new_fd);
    }

    auto& operator=(std::nullptr_t)
    {
        reset();
        return *this;
    }

    auto get() const noexcept -> fd_t { return fd; }

    auto extract() noexcept -> fd_t;

    explicit operator bool() const noexcept { return fd >= 0; }
};
