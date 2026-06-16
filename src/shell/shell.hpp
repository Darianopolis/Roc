#pragma once

#include <scene/scene.hpp>
#include <way/way.hpp>
#include <io/io.hpp>

struct UiClient;

struct Shell
{
    ExecContext* exec;
    Ref<Gpu> gpu;
    Ref<IoContext> io;
    Ref<WmServer> wm;
    Ref<WayServer> way;

    SeatModifier main_mod;

    std::filesystem::path app_share;
    std::filesystem::path wallpaper;

    std::optional<std::string> xwayland_socket;

    RefVector<void> apps;

    ~Shell()
    {
        apps.destroy_all();
        way.destroy();
        wm.destroy();
        io.destroy();
        gpu.destroy();
    }
};

void shell_init_xwayland(Shell*, int argc, char* argv[]);
void shell_init_background(Shell*);
void shell_init_hotkeys(Shell*);
