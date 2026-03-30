#pragma once

#include "../internal.hpp"

struct IoSeatDevice
{
    int id;
    int fd;
};

struct IoSession
{
    libseat *seat;

    std::vector<IoSeatDevice> devices;
};

auto io_session_get_seat_name(IoSession*) -> const char*;

auto io_session_open_device(IoSession*, const char* path) -> int;
void io_session_close_device(IoSession*, int);
