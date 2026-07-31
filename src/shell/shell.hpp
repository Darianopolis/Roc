#pragma once

#include <scene/scene.hpp>
#include <way/way.hpp>
#include <io/io.hpp>

#include <core/process.hpp>
#include <core/chrono.hpp>
#include <core/log.hpp>

UNIX_FUNCTION(sd_bus_open_user,                 UnixErrorBehavior::negative_errno)
UNIX_FUNCTION(sd_bus_request_name,              UnixErrorBehavior::negative_errno)
UNIX_FUNCTION(sd_bus_send,                      UnixErrorBehavior::negative_errno)
UNIX_FUNCTION(sd_bus_emit_signal,               UnixErrorBehavior::negative_errno)
UNIX_FUNCTION(sd_bus_reply_method_return,       UnixErrorBehavior::negative_errno)
UNIX_FUNCTION(sd_bus_add_object_vtable,         UnixErrorBehavior::negative_errno)
UNIX_FUNCTION(sd_bus_message_read,              UnixErrorBehavior::negative_errno)
UNIX_FUNCTION(sd_bus_message_skip,              UnixErrorBehavior::negative_errno)
UNIX_FUNCTION(sd_bus_message_new_method_return, UnixErrorBehavior::negative_errno)
UNIX_FUNCTION(sd_bus_message_append,            UnixErrorBehavior::negative_errno)
UNIX_FUNCTION(sd_bus_message_open_container,    UnixErrorBehavior::negative_errno)
UNIX_FUNCTION(sd_bus_message_close_container,   UnixErrorBehavior::negative_errno)
UNIX_FUNCTION(sd_bus_get_sender,                UnixErrorBehavior::negative_errno)
UNIX_FUNCTION(sd_bus_track_new,                 UnixErrorBehavior::negative_errno)
UNIX_FUNCTION(sd_bus_track_add_sender,          UnixErrorBehavior::negative_errno)

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

    sd_bus* dbus;

    ~Shell()
    {
        while (!plugins.empty()) plugins.pop_back();

        way.destroy();
        wm.destroy();
        io.destroy();
        gpu.destroy();

        fd_unlisten(exec, sd_bus_get_fd(dbus));
        dbus = sd_bus_unref(dbus);
        debug_assert(!dbus);
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

static constexpr auto xdg_desktop_portal_name = "org.freedesktop.impl.portal.desktop.roc";

inline
auto shell_dbus_acquire_name(Shell* shell) -> UnixResult<int>
{
    return unix_check<sd_bus_request_name, EALREADY>(shell->dbus, xdg_desktop_portal_name, literal_cast<u64>(SD_BUS_NAME_ALLOW_REPLACEMENT | SD_BUS_NAME_REPLACE_EXISTING));
}

inline
void shell_dbus_init(Shell* shell, bool grab_portal_name)
{
    unix_check<sd_bus_open_user>(&shell->dbus);
    fd_listen(shell->exec, sd_bus_get_fd(shell->dbus), FdEventBit::readable, [shell](fd_t, Flags<FdEventBit>) {
        sd_bus_process(shell->dbus, nullptr);
    });

    if (grab_portal_name) {
        shell_dbus_acquire_name(shell);
    }
}

void shell_init_xwayland(Shell*, int argc, char* argv[]);
void shell_init_background(Shell*);
void shell_init_hotkeys(Shell*);
void shell_init_screencast(Shell*);
void shell_init_screenshot(Shell*);
void shell_init_renderdoc(Shell*);
