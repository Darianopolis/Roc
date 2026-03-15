#include "../internal.hpp"

struct io_libinput
{
};

void io_libinput_init(io_context*)
{
    log_error("IO - libinput backend not implemented");
}
