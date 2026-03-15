#pragma once

#include "object.hpp"
#include "enum.hpp"

// -----------------------------------------------------------------------------

auto core_fd_are_same(int fd0, int fd1) -> bool;
auto core_fd_dup_unsafe(int fd) -> int;

// -----------------------------------------------------------------------------

struct core_fd_listener;
void core_fd_remove_listener(int fd);
auto core_fd_get_listener(   int fd) -> core_fd_listener*;
void core_fd_set_listener(   int fd, core_fd_listener*);

// -----------------------------------------------------------------------------

static constexpr int core_fd_max = 1024;

auto core_fd_get_ref_count(int fd) -> u32;

auto core_fd_add_ref(   int fd) -> int;
auto core_fd_remove_ref(int fd) -> int;

struct core_fd
{
    int fd;

    core_fd()
        : fd(-1)
    {}

    explicit core_fd(int fd)
        : fd(fd)
    {
        core_fd_add_ref(fd);
    }

    core_fd(const core_fd& other)
        : fd(other.fd)
    {
        core_fd_add_ref(fd);
    }

    core_fd& operator=(const core_fd& other)
    {
        if (this != &other) {
            reset(other.fd);
        }
        return *this;
    }

    core_fd(core_fd&& other)
        : fd(std::exchange(other.fd, -1))
    {}

    core_fd& operator=(core_fd&& other)
    {
        if (this != &other) {
            core_fd_remove_ref(fd);
            fd = std::exchange(other.fd, -1);
        }
        return *this;
    }

    ~core_fd()
    {
        core_fd_remove_ref(fd);
    }

    void reset(int new_fd = -1)
    {
        core_fd_remove_ref(fd);
        fd = core_fd_add_ref(new_fd);
    }

    core_fd& operator=(std::nullptr_t)
    {
        reset();
        return *this;
    }

    int get() const noexcept { return fd; }

    explicit operator bool() const noexcept { return fd >= 0; }
};

// -----------------------------------------------------------------------------

auto core_fd_adopt(    int fd) -> core_fd;
auto core_fd_reference(int fd) -> core_fd;
auto core_fd_dup(      int fd) -> core_fd;
