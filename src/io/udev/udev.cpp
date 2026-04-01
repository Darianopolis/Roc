#include "../internal.hpp"

void io_udev_init(IoContext* io)
{
    io->udev = unix_check<udev_new>().value;
}

void io_udev_deinit(IoContext* io)
{
    udev_unref(io->udev);
}
