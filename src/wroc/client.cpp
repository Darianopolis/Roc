#include "util.hpp"

bool wroc_is_client_behind(wl_client* client)
{
    if (!client) return true;
    return poll(core::ptr_to(pollfd {
        .fd = wl_client_get_fd(client),
        .events = POLLOUT,
    }), 1, 0) != 1;
}
