#include "wroc.hpp"

static
std::filesystem::path find_on_path(std::string_view in)
{
    std::string_view path = getenv("PATH");

    usz b = 0;
    for (;;) {
        usz n = path.find_first_of(":", b);
        auto part = path.substr(b, n - b);

        auto test_path = std::filesystem::path(part) / in;
        if (std::filesystem::exists(test_path)) {
            return test_path;
        }

        if (n == std::string::npos) break;
        b = n + 1;
    }

    return {};
}

pid_t wroc_spawn(
    std::string_view file,
    std::span<const std::string_view> args,
    std::span<const wroc_spawn_action> actions)
{
    std::vector<std::string> args_str;
    for (std::string_view a : args) args_str.emplace_back(a);

    std::vector<char*> args_cstr;
    for (std::string& s : args_str) args_cstr.emplace_back(s.data());
    args_cstr.emplace_back(nullptr);

    log_info("Spawning process [{}] args {}", file, args);

    auto path = find_on_path(file);
    if (path.empty()) {
        log_error("  Could not find on path");
        return 0;
    }

    log_debug("  Full path: {}", path.c_str());

    if (access(path.c_str(), X_OK) != 0) {
        log_error("  File is not executable");
        return 0;
    }

    int pid = fork();
    if (pid == 0) {
        setenv("WAYLAND_DISPLAY", server->socket.c_str(), true);
        unsetenv("DISPLAY");

        for (const wroc_spawn_action& action : actions) {
            std::visit(core::OverloadSet {
                [&](const wroc_spawn_env_action& env_action) {
                    if (!env_action.name) return;
                    if (env_action.value) {
                        setenv(env_action.name, env_action.value, true);
                    } else {
                        unsetenv(env_action.name);
                    }
                },
                [&](const wroc_spawn_x11_action& x11_action) {
                    // Clear instead of unsetting for compatibility with certain toolkits
                    if (x11_action.force)      setenv("WAYLAND_DISPLAY", "", true);
                    if (x11_action.x11_socket) setenv("DISPLAY", x11_action.x11_socket, true);
                },
                [&](const wroc_spawn_cwd_action& cwd_action) {
                    if (cwd_action.cwd) chdir(cwd_action.cwd);
                },
            }, action);
        }
        close(STDOUT_FILENO);
        close(STDERR_FILENO);
        i32 _ = openat(STDOUT_FILENO, "/dev/null", O_RDWR);
        i32 _ = openat(STDERR_FILENO, "/dev/null", O_RDWR);
        execv(path.c_str(), args_cstr.data());
        _Exit(0);
    } else {
        return pid;
    }
}

void wroc_spawn(GAppInfo* app, std::span<const wroc_spawn_action> actions)
{
    auto* name = g_app_info_get_display_name(app) ?: g_app_info_get_name(app);
    log_info("Running: {}", name);
    log_info("  command line: {}", g_app_info_get_commandline(app) ?: "");

    // TODO: Select action
    //
    // auto* desktop = G_DESKTOP_APP_INFO(app.app_info);
    // auto actions = g_desktop_app_info_list_actions(desktop);
    // for (auto action = actions; *action; ++action) {
    //     log_info("  action[{}] = {}", *action, g_desktop_app_info_get_action_name(desktop, *action));
    // }
    // g_desktop_app_info_launch_action()

    auto* ctx = g_app_launch_context_new();
    defer { g_object_unref(ctx); };

    g_app_launch_context_setenv(ctx, "WAYLAND_DISPLAY", server->socket.c_str());
    g_app_launch_context_unsetenv(ctx, "DISPLAY");

    for (auto& action : actions) {
        std::visit(core::OverloadSet {
            [&](const wroc_spawn_env_action& env_action) {
                if (!env_action.name) return;
                if (env_action.value) {
                    g_app_launch_context_setenv(ctx, env_action.name, env_action.value);
                } else {
                    g_app_launch_context_unsetenv(ctx, env_action.name);
                }
            },
            [&](const wroc_spawn_x11_action& x11_action) {
                // Clear instead of unsetting for compatibility with certain toolkits
                if (x11_action.force)      g_app_launch_context_setenv(ctx, "WAYLAND_DISPLAY", "");
                if (x11_action.x11_socket) g_app_launch_context_setenv(ctx, "DISPLAY", x11_action.x11_socket);
            },
            [&](const wroc_spawn_cwd_action& cwd_action) {
                log_warn("Spawning GAppInfo ignores CWD requests");
            },
        }, action);
    }

    GError* err = nullptr;
    if (!g_app_info_launch(app, nullptr, ctx, &err)) {
        log_error("Error launching {}: {}", name, err->message);
    }
}

void wroc_setenv(const char* name, const char* value, core::Flags<wroc_setenv_option> options)
{
    if (value) {
        setenv(name, value, true);
    } else {
        unsetenv(name);
    }

    if (options.contains(wroc_setenv_option::system_wide) && server->backend->type == wroc_backend_type::direct) {
        wroc_spawn("systemctl", {"systemctl", "--user", "import-environment", name}, {});
    }
}
