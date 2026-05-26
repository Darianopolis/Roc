#include "shell.hpp"

#include <core/math.hpp>
#include <core/signal.hpp>
#include <core/log.hpp>
#include <core/process.hpp>

#include <wm/wm.hpp>

auto main(int argc, char* argv[]) -> int
{
    bool in_direct_session = env_get("WAYLAND_DISPLAY")->empty();
    auto home_dir = std::filesystem::path(env_get("HOME").value_or(""));
    auto app_share = home_dir / ".local/share" / PROGRAM_NAME;

    if (in_direct_session) {
        log_set_structured_log(app_share / PROGRAM_NAME ".log");
        log_redirect_stdout(app_share / "stdout.log");
        log_redirect_stderr(app_share / "stderr.log");
        chdir(home_dir.c_str());
    } else {
        log_set_structured_log(PROGRAM_NAME ".log");
    }

    fd_leak_mark_inherited();
    defer {
        fd_leak_check();
    };

    log_info("{} ({:n:})", PROJECT_NAME, std::span<const char* const>(argv, argc));

    auto exec = exec_create();

    auto shell = ref_create<Shell>();
    shell->exec = exec.get();

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
    shell->io = io_create(exec.get(), shell->gpu.get());
    shell->wm = wm_create({
        .exec = exec.get(),
        .gpu = shell->gpu.get(),
        .main_mod = shell->main_mod,
    });
    shell->way = way_create(shell->wm.get(), exec.get());

    // Applets

    shell_init_background(shell.get());
    shell_init_xwayland(shell.get(), argc, argv);
    shell_init_hotkeys(shell.get());

    // IO

    shell_init_io_bridge(shell.get());

    auto _ = io_get_signals(shell->io.get()).shutdown.listen([&] {
        shell.destroy();
        exec_stop(exec.get());
    });

    // Helpers

    if (in_direct_session) {
        spawn_path("playerctld", {{"playerctld"}});
        spawn_path("dunst", {{"dunst"}});
    }

    // Environment

    // TODO: Avoid setting these globally
    env_set("XDG_CURRENT_DESKTOP", PROGRAM_NAME);
    env_set("WAYLAND_DISPLAY", way_server_get_socket(shell->way.get()));
    env_set("DISPLAY", shell->xwayland_socket);
    if (in_direct_session) {
        log_info("Exporting environment to system...");
        spawn_path("systemctl", {{
            "systemctl",
            "--user", "import-environment",
            "XDG_CURRENT_DESKTOP", "WAYLAND_DISPLAY", "DISPLAY"
        }});
    }

    // Run

    io_start(shell->io.get());
    exec_run(exec.get());
}
