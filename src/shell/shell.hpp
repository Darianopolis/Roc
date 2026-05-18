#pragma once

#include <scene/scene.hpp>
#include <way/way.hpp>
#include <io/io.hpp>
#include <ui/ui.hpp>

struct Shell
{
    ExecContext* exec;
    Ref<Gpu> gpu;
    Ref<IoContext> io;
    Ref<WmServer> wm;
    Ref<WayServer> way;
    Ref<UiClient> ui;

    SeatModifier main_mod;

    std::filesystem::path app_share;
    std::filesystem::path wallpaper;

    std::string xwayland_socket;

    RefVector<void> apps;

    ~Shell()
    {
        apps.destroy_all();
        ui.destroy();
        way.destroy();
        wm.destroy();
        io.destroy();
        gpu.destroy();
    }
};

void shell_init_xwayland(Shell*, int argc, char* argv[]);
void shell_init_menu(Shell*);
void shell_init_launcher(Shell*);
void shell_init_log_viewer(Shell*);
void shell_init_background(Shell*);
void shell_init_io_bridge(Shell*);
