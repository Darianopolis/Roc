#pragma once

#include <scene/scene.hpp>
#include <way/way.hpp>
#include <io/io.hpp>

#include <core/process.hpp>

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

    RefVector<void> apps;

    Environment env;

    ~Shell()
    {
        apps.destroy_all();
        way.destroy();
        wm.destroy();
        io.destroy();
        gpu.destroy();
    }
};

inline
auto shell_launch(
    Shell* shell,
    std::string_view name,
    std::span<const std::string_view> args,
    std::span<const SpawnFdInherit> fds = {}) -> Fd
{
    auto& path = shell->env.entries.at("PATH");

    usz offset = 0;
    for (;;) {
        auto sep = path.find_first_of(':', offset);
        if (sep >= path.size()) return {};

        auto test = std::filesystem::path(std::string_view(path).substr(offset, sep - offset)) / name;
        if (std::filesystem::exists(test)) {
            // Launch
            return spawn(path_open(test).get(), args, &shell->env, fds);
        }

        offset = sep + 1;
    }

    return {};
}

void shell_init_xwayland(Shell*, int argc, char* argv[]);
void shell_init_background(Shell*);
void shell_init_hotkeys(Shell*);
