#include "shell.hpp"

#include <core/log.hpp>

void shell_init_xwayland(Shell* shell, int argc, char* argv[])
{
    std::vector<std::string> args;
    args.append_range(std::span(argv, argc));

    if (auto iter = std::ranges::find(args, std::string("--xwayland")); iter != args.end()) {
        auto socket = ++iter;
        if (socket == args.end()) {
            log_error("Expected XWayland socket name");
            return;
        }
        log_debug("Launching xwayland-satellite instance, DISPLAY={}", *socket);

        if (fork() == 0) {
            setenv("WAYLAND_DISPLAY", way_server_get_socket(shell->way.get()), true);
            execlp("xwayland-satellite", "xwayland-satellite", socket->c_str(), nullptr);
            std::terminate();
        }
        shell->xwayland_socket = *socket;
    }
}
