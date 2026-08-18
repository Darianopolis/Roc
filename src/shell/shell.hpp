#pragma once

#include <scene/scene.hpp>
#include <way/way.hpp>
#include <io/io.hpp>

#include <core/process.hpp>
#include <core/chrono.hpp>
#include <core/log.hpp>
#include <core/cmd-parse.hpp>

struct ShellPlugin
{
    virtual ~ShellPlugin() = default;
};

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

    RefVector<ShellPlugin> plugins;

    Environment env;
    Fd dev_null;

    ~Shell()
    {
        while (!plugins.empty()) plugins.pop_back();

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
    std::span<const SpawnFdInherit> _fds = {},
    fd_t dir = -1) -> Fd
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
            auto process = spawn(SpawnInfo {
                .executable = path_open(test).get(),
                .directory = fd_is_valid(dir) ? dir : shell->env.dir.get(),
                .fd_limit = fd_get_limits().inherited,
                .args = args,
                .env = std::ranges::to<std::vector>(shell->env.entries
                    | std::views::transform([](const auto& e) {
                        return std::make_pair(std::string_view(e.first), std::string_view(e.second));
                    })),
                .fds = fds,
            });
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

void shell_init_xwayland(Shell*, const CommandArgs&);
void shell_init_background(Shell*);
void shell_init_hotkeys(Shell*);
void shell_init_screenshot(Shell*);
void shell_init_renderdoc(Shell*);
