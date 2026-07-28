#include "shell.hpp"

#include <core/process.hpp>
#include <core/chrono.hpp>

#include <way/surface/surface.hpp>

static
void print_scene_graph(Shell* shell)
{
    auto* wm = shell->wm.get();
    auto* way = shell->way.get();

    std::ostringstream oss;

    [&](this auto&& visit, SceneNode* node, u32 depth = 0) -> void {
        auto indent = [&] { return std::string(depth, ' '); };
        scene_visit(node, OverloadSet {
            [&](SceneTree* tree) {
                WaySurface* surface;
                if (tree->userdata.id == way->userdata_id && (surface = way_get_userdata<WaySurface>(way, tree->userdata.data))) {
                    std::println(oss, "{}{} {} {{", indent(),
                        surface->role,
                        tree->translation);
                } else {
                    std::println(oss, "{}tree {} {{", indent(), tree->translation);
                }
                for (auto* child : tree->children) {
                    visit(child, depth + 2);
                }
                std::println(oss, "{}}}", indent());
            },
            [&](SceneTexture* texture) {
                std::println(oss, "{}texture {}", indent(), texture->dst);
            },
            [&](SceneInputRegion* input_region) {
                std::println(oss, "{}input_region {}", indent(), rect2f32(input_region->clip));
            },
        });
    }(wm_get_scene(wm));

    log_info("Scene graph:\n{}", oss.str());
}

struct ShellHotkeys : ShellPlugin
{
    RefVector<WmHotkey> hotkeys;
};

void shell_init_hotkeys(Shell* shell)
{
    auto hotkeys = ref_create<ShellHotkeys>();
    shell->plugins.emplace_back(hotkeys);

    auto hotkey = [&](SeatInputCode code, Flags<SeatModifier> modifiers, std::function<WmHotkeyCallback> callback) {
        hotkeys->hotkeys.emplace_back(wm_bind_hotkey(shell->wm.get(), modifiers, code, callback));
    };

    hotkey(KEY_V, shell->main_mod, [shell](auto...) {
        shell_launch(shell, "systemctl", {{"systemctl", "--user", "restart", "xdg-desktop-portal"}});
        wm_toast(shell->wm.get(), "XDG Desktop Portal : Restarted");
    });

    hotkey(KEY_N,   shell->main_mod, [shell](auto...) { shell_launch(shell, "systemctl", {{"systemctl", "suspend"}}); });
    hotkey(KEY_T,   shell->main_mod, [shell](auto...) { way_clear(shell->way.get()); });
    hotkey(KEY_ESC, shell->main_mod, [shell](auto...) { io_stop(shell->io.get()); });

    hotkey(KEY_D, shell->main_mod, [shell](auto...) { shell_launch(shell, "launcher", {{"launcher"}}); });
    hotkey(KEY_X, shell->main_mod, [shell](auto...) { shell_launch(shell, "tray", {{"tray"}}); });

    hotkey(KEY_O, shell->main_mod, [shell](auto...) { io_output_create(shell->io.get()); });
    hotkey(KEY_G, shell->main_mod, [shell](auto...) { print_scene_graph(shell); });

    hotkey(KEY_PREVIOUSSONG, {}, [shell](auto...) { shell_launch(shell, "playerctl", {{"playerctl", "previous"}}); });
    hotkey(KEY_PLAYPAUSE,    {}, [shell](auto...) { shell_launch(shell, "playerctl", {{"playerctl", "play-pause"}}); });
    hotkey(KEY_NEXTSONG,     {}, [shell](auto...) { shell_launch(shell, "playerctl", {{"playerctl", "next"}}); });
    hotkey(KEY_VOLUMEDOWN,   {}, [shell](auto...) { shell_launch(shell, "wpctl", {{"wpctl", "set-volume", "@DEFAULT_AUDIO_SINK@", "0.02-"}}); });
    hotkey(KEY_VOLUMEUP,     {}, [shell](auto...) { shell_launch(shell, "wpctl", {{"wpctl", "set-volume", "@DEFAULT_AUDIO_SINK@", "0.02+", "-l", "1.0"}}); });

    for (u32 i = 0; i < 12; ++i) {
        hotkey(KEY_F1 + i, SeatModifier::ctrl | SeatModifier::alt, [shell, session = num_cast<i32>(i + 1)](auto...) {
            log_debug("Switching VT to {}", session);
            io_switch_session(shell->io.get(), session);
        });
    }
}
