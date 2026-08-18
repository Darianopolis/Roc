#include "shell.hpp"

#include <core/log.hpp>
#include <core/process.hpp>

void shell_init_xwayland(Shell* shell, const CommandArgs& args)
{
    if (auto* socket = args.find("--xwayland")) {
        if (!socket->has_value) {
            log_error("Expected socket name for --xwayland option (E.g. --xwayland=:0)");
            return;
        }

        log_debug("Launching xwayland-satellite instance, DISPLAY={}", socket->value);

        shell_launch(shell, "xwayland-satellite", {{ "xwayland-satellite", socket->value, }}, {});

        shell->env.set("DISPLAY", std::string(socket->value));
    }
}
