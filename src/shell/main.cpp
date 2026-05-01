#include "shell.hpp"

#include <core/math.hpp>

#include <wm/wm.hpp>
#include <ui/ui.hpp>

auto main(int argc, char* argv[]) -> int
{
    log_set_file(PROGRAM_NAME ".log");

    log_info("{} ({:n:})", PROJECT_NAME, std::span<const char* const>(argv, argc));

    Shell shell = {};

    // Config

    shell.app_share = std::filesystem::path(getenv("HOME")) / ".local/share" / PROGRAM_NAME;
    shell.wallpaper = getenv("WALLPAPER") ?: "";
    if (getenv("WAYLAND_DISPLAY")) {
        log_debug("Running nested!");
        shell.main_mod = SeatModifier::alt;
    } else {
        log_debug("Running in direct session");
        shell.main_mod = SeatModifier::super;
    }

    // Systems

    auto exec = exec_create();
    auto gpu = gpu_create(exec.get(), {});
    auto io = io_create(exec.get(), gpu.get());
    auto wm = wm_create({
        .exec = exec.get(),
        .gpu = gpu.get(),
        .io = io.get(),
        .main_mod = shell.main_mod,
    });
    auto way = way_create(exec.get(), gpu.get(), wm.get());

    shell.exec = exec.get();
    shell.gpu = gpu.get();
    shell.way = way.get();
    shell.io = io.get();
    shell.wm = wm.get();

    // Applets

    auto _ = shell_init_background(&shell);
    auto _ = shell_init_launcher(&shell);
    auto _ = shell_init_log_viewer(&shell);
    auto _ = shell_init_menu(&shell);

    shell_init_xwayland(&shell, argc, argv);

    // Run

    io_run(io.get());
}
