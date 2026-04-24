#include "roc.hpp"

#include "way/server.hpp"

void roc_init_xwayland(Roc* roc, int argc, char* argv[])
{
    std::vector<std::string> args;
    args.append_range(std::span(argv, argc));

    if (auto iter = std::ranges::find(args, "--xwayland"); iter != args.end()) {
        auto socket = ++iter;
        if (socket == args.end()) {
            log_error("Expected XWayland socket name");
            return;
        }
        log_debug("Launching xwayland-satellite instance, DISPLAY={}", *socket);

        if (fork() == 0) {
            setenv("WAYLAND_DISPLAY", roc->way->socket_name.c_str(), true);
            execlp("xwayland-satellite", "xwayland-satellite", socket->c_str(), nullptr);
            std::terminate();
        }
        roc->xwayland_socket = *socket;
    }
}
