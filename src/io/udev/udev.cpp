#include "../internal.hpp"

void io_udev_init(IoContext* ctx)
{
    ctx->udev = unix_check<udev_new>().value;
}

void io_udev_deinit(IoContext* ctx)
{
    udev_unref(ctx->udev);
}
