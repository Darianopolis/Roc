#pragma once

#include "../internal.hpp"

struct IoSeatDevice
{
    int  id;
    fd_t fd;
};

struct IoSession
{
    libseat *seat;

    std::vector<IoSeatDevice> devices;

    ~IoSession()
    {
        debug_assert(devices.empty());
    }
};

auto io_session_get_seat_name(IoSession*) -> const char*;

auto io_session_open_device(IoSession*, const char* path) -> fd_t;
void io_session_close_device(IoSession*, fd_t);
