#include "shell.hpp"

#include <core/log.hpp>
#include <core/process.hpp>

void shell_init_xwayland(Shell* shell, int argc, char* argv[])
{
    std::vector<std::string> args;
    args.append_range(std::span(argv, num_cast<usz>(argc)));

    if (auto iter = std::ranges::find(args, std::string("--xwayland")); iter != args.end()) {
        auto socket = ++iter;
        if (socket == args.end()) {
            log_error("Expected XWayland socket name");
            return;
        }
        log_debug("Launching xwayland-satellite instance, DISPLAY={}", *socket);

        shell_launch(shell, "xwayland-satellite", {{ "xwayland-satellite", socket->c_str(), }}, {});

        shell->env.set("DISPLAY", *socket);
    }
}
