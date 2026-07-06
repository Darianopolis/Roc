#pragma once

#include <scene/scene.hpp>
#include <way/way.hpp>
#include <io/io.hpp>

#include <core/process.hpp>
#include <core/chrono.hpp>
#include <core/log.hpp>

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
    Fd dev_null;

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
    std::span<const SpawnFdInherit> _fds = {}) -> Fd
{
    auto& path = shell->env.entries.at("PATH");

    std::vector<SpawnFdInherit> fds{_fds.begin(), _fds.end()};
    for (fd_t std_fd : {STDIN_FILENO, STDOUT_FILENO, STDERR_FILENO}) {
        if (!std::ranges::contains(fds, std_fd, &SpawnFdInherit::child)) {
            fds.emplace_back(shell->dev_null.get(), std_fd);
        }
    }

    usz offset = 0;
    for (;;) {
        auto sep = path.find_first_of(':', offset);
        if (sep >= path.size()) return {};

        auto test = std::filesystem::path(std::string_view(path).substr(offset, sep - offset)) / name;
        if (std::filesystem::exists(test)) {
            // Launch
            auto start = std::chrono::steady_clock::now();
            auto process = spawn(path_open(test).get(), args, &shell->env, fds);
            auto end = std::chrono::steady_clock::now();
            if (process) {
                log_debug("Process {} launched in {}", test, FmtDuration{end - start});
            } else {
                log_error("Process {} failed to launch", test);
            }
            return process;
        }

        offset = sep + 1;
    }

    log_error("Failed to find executable {} on PATH", name);

    return {};
}

void shell_init_xwayland(Shell*, int argc, char* argv[]);
void shell_init_background(Shell*);
void shell_init_hotkeys(Shell*);
