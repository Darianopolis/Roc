#include "shell.hpp"

#include <core/math.hpp>
#include <core/signal.hpp>
#include <core/log.hpp>

#include <wm/wm.hpp>
#include <ui/ui.hpp>

auto main(int argc, char* argv[]) -> int
{
    log_init(PROGRAM_NAME ".log");
    fd_registry_init();
    registry_init();
    defer {
        registry_deinit();
        fd_registry_deinit();
        log_deinit();
    };

    log_info("{} ({:n:})", PROJECT_NAME, std::span<const char* const>(argv, argc));

    auto exec = exec_create();

    auto shell = ref_create<Shell>();
    shell->exec = exec.get();

    // Config

    shell->app_share = std::filesystem::path(getenv("HOME")) / ".local/share" / PROGRAM_NAME;
    shell->wallpaper = getenv("WALLPAPER") ?: "";
    if (getenv("WAYLAND_DISPLAY")) {
        log_debug("Running nested!");
        shell->main_mod = SeatModifier::alt;
    } else {
        log_debug("Running in direct session");
        shell->main_mod = SeatModifier::super;
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
    shell->ui = ui_create(shell->wm.get(), shell->app_share);

    // Applets

    shell_init_background(shell.get());
    shell_init_launcher(shell.get());
    shell_init_log_viewer(shell.get());
    shell_init_menu(shell.get());
    shell_init_xwayland(shell.get(), argc, argv);

    // IO

    shell_init_io_bridge(shell.get());

    auto _ = io_get_signals(shell->io.get()).shutdown.listen([&] {
        shell.destroy();
        exec_stop(exec.get());
    });

    // Run

    io_start(shell->io.get());
    exec_run(exec.get());
}
