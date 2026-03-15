#include "../internal.hpp"

struct io_evdev
{
};

void io_evdev_init(io_context*)
{
    log_error("IO - evdev backend not implemented");
}
