#include "shell.hpp"

#include <core/math.hpp>
#include <core/signal.hpp>
#include <core/log.hpp>

#include <wm/wm.hpp>

auto main(int argc, char* argv[]) -> int
{
    debug_handlers();

    bool in_direct_session = env_get("WAYLAND_DISPLAY").value_or("").empty();
    auto home_dir = std::filesystem::path(env_get("HOME").value_or(""));
    auto app_share = home_dir / ".local/share" / PROGRAM_NAME;
    std::filesystem::create_directories(app_share);

    if (in_direct_session) {
        log_set_structured_log(app_share / PROGRAM_NAME ".log");
        log_redirect_stdout(app_share / "stdout.log");
        log_redirect_stderr(app_share / "stderr.log");

        env_set("XDG_SESSION_TYPE", "wayland");
    } else {
        log_set_structured_log(PROGRAM_NAME ".log");
    }

    fd_leak_mark_inherited();
    defer {
        fd_leak_check();
    };

    log_info("{} ({:n:})", PROJECT_NAME, std::span<const char* const>(argv, num_cast<usz>(argc)));

    auto exec = exec_create();

    auto shell = ref_create<Shell>();
    shell->exec = exec.get();

    shell->dev_null = path_open("/dev/null", O_RDWR);

    // Environment

    shell->env.load(environ);
    shell->env.set("XDG_CURRENT_DESKTOP", PROGRAM_NAME);
    shell->env.set("DISPLAY", std::nullopt);
    shell->env.dir = path_open(home_dir);

    // Config

    shell->app_share = app_share;
    shell->wallpaper = env_get("WALLPAPER").value_or("");

    if (in_direct_session) {
        log_debug("Running in direct session");
        shell->main_mod = SeatModifier::super;
    }  else {
        log_debug("Running nested!");
        shell->main_mod = SeatModifier::alt;
    }

    // Systems

    shell->gpu = gpu_create(exec.get(), {});
    debug_assert(shell->gpu, "Failed to initialize GPU");

    shell->wm = wm_create({
        .exec = exec.get(),
        .gpu = shell->gpu.get(),
        .main_mod = shell->main_mod,
    });

    shell->io = io_create(shell->wm.get(), exec.get(), shell->gpu.get());
    auto _ = io_get_signals(shell->io.get()).shutdown.listen([&] {
        shell.destroy();
        exec_stop(exec.get());
    });

    shell->way = way_create(shell->wm.get(), exec.get());
    shell->env.set("WAYLAND_DISPLAY", way_server_get_socket(shell->way.get()));

    // Applets

    shell_init_background(shell.get());
    shell_init_xwayland(shell.get(), argc, argv);
    shell_init_hotkeys(shell.get());

    if (in_direct_session) {

        // Helpers

        shell_launch(shell.get(), "playerctld", {{ "playerctld" }});
        shell_launch(shell.get(), "dunst",      {{ "dunst"      }});

        shell_launch(shell.get(), "tray-service", {{ "tray-service" }});

        // System

        log_info("Exporting environment to system...");
        std::vector<std::string_view> args {
            "systemctl",
            "--user", "import-environment",
            "XDG_CURRENT_DESKTOP",
            "XDG_SESSION_TYPE",
            "WAYLAND_DISPLAY"
        };
        if (shell->env.entries.contains("DISPLAY")) {
            args.emplace_back("DISPLAY");
        }
        shell_launch(shell.get(), "systemctl", args);
    }

    // Run

    io_start(shell->io.get());
    exec_run(exec.get());
}
