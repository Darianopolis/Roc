#pragma once

#include <scene/scene.hpp>
#include "ui/ui.hpp"
#include "way/way.hpp"
#include <io/io.hpp>

struct Shell
{
    ExecContext* exec;
    Gpu*         gpu;
    WmServer*    wm;
    WayServer*   way;
    IoContext*   io;

    SeatModifier main_mod;

    std::filesystem::path app_share;
    std::filesystem::path wallpaper;

    std::string xwayland_socket;
};

void shell_init_xwayland(          Shell*, int argc, char* argv[]);
auto shell_init_launcher(          Shell*) -> Ref<void>;
auto shell_init_log_viewer(        Shell*) -> Ref<void>;
auto shell_init_simple_test_client(Shell*) -> Ref<void>;
auto shell_init_ui_demo_window(    Shell*) -> Ref<void>;
auto shell_init_background(        Shell*) -> Ref<void>;
