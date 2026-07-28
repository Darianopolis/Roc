#include "shell.hpp"

#include "pipewire.hpp"

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

static
void renderdoc_capture(Shell* shell)
{
    auto* gpu = shell->gpu.get();
    auto* wm = shell->wm.get();

    if (!gpu->renderdoc) {
        log_warn("RenderDoc isn't attached, can't capture!");
        return;
    }

    static u32 capture = 0;
    gpu->renderdoc->StartFrameCapture(nullptr, nullptr);
    gpu->renderdoc->SetCaptureTitle(std::format("Shell capture {}", ++capture).c_str());
    auto* output = wm_find_output_at(wm, seat_pointer_get_position(seat_get_pointer(wm_get_seat(wm)))).output;
    if (output) {
        auto viewport = wm_output_get_viewport(output);
        auto texture = gpu_image_create(gpu, {
            .extent = vec_cast<u32>(viewport.extent),
            .format = gpu_format_from_drm(DRM_FORMAT_ABGR8888),
            .usage = GpuImageUsage::storage,
        });
        scene_render(wm_get_scene_renderer(wm), {
            .root = wm_get_scene(wm),
            .target = texture.get(),
            .viewport = viewport,
        });
        gpu_flush(gpu);
    }
    gpu->renderdoc->EndFrameCapture(nullptr, nullptr);
}

static
void take_screenshot(Shell* shell, rect2f32 region)
{
    log_info("Taking screenshot, region: {}", region);
    region = pixel_round<f32>(region);
    log_debug("  rounded: {}", region);

    if (region.extent == vec2f32{0,0}) {
        log_warn("  region is empty, cancelling screenshot...");
        return;
    }

    auto* gpu = shell->gpu.get();
    auto* wm = shell->wm.get();

    auto extent = vec_cast<u32>(region.extent);

    auto texture = gpu_image_create(gpu, {
        .extent = extent,
        .format = gpu_format_from_drm(DRM_FORMAT_ABGR8888),
        .usage = GpuImageUsage::storage
    });

    auto buffer = gpu_buffer_create(gpu, extent.x * extent.y * 4, GpuBufferFlag::host);

    scene_render(wm_get_scene_renderer(wm), {
        .root = wm_get_scene(wm),
        .target = texture.get(),
        .viewport = region,
    });
    gpu_copy_image_to_buffer(buffer.get(), texture.get());

    gpu_wait(gpu_flush(gpu), [buffer, extent, dir = shell->app_share](u64) {
        log_debug("Screenshot prepared, saving...");

        auto start = std::chrono::steady_clock::now();
        std::vector<u8> data;
        data.resize(extent.x * extent.y * 4);
        std::memcpy(data.data(), buffer->host_address, data.size());
        auto end = std::chrono::steady_clock::now();
        log_debug("Screenshot data copied in {}", FmtDuration{end - start});

        std::thread{[data = std::move(data), extent, dir = std::move(dir)] {
            auto start = std::chrono::steady_clock::now();

            auto save_path = dir / std::format("screenshot-{}.png", FmtTime{std::chrono::system_clock::now(), TimeFormat::iso8601});

            stbi_write_png(save_path.c_str(), num_cast<i32>(extent.x), num_cast<i32>(extent.y), STBI_rgb_alpha, data.data(), num_cast<i32>(extent.x * 4));
            auto end = std::chrono::steady_clock::now();
            log_info("Screenshot saved to [{}] in {}", save_path, FmtDuration{end - start});
        }}.detach();
    });
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

    hotkey(KEY_B, shell->main_mod, [shell](auto...) {
        auto res = shell_dbus_acquire_name(shell);
        if (res.ok()) {
            wm_toast(shell->wm.get(), "XDG Desktop Portal : Name acquired successfully");
        } else if (res.error == EALREADY) {
            wm_toast(shell->wm.get(), "XDG Desktop Portal : Name already acquired", {1, 1, 0, 1});
        } else {
            wm_toast(shell->wm.get(), "XDG Desktop Portal : Name could not be acquired", {1, 0, 0, 1});
        }
    });

    hotkey(KEY_P, shell->main_mod, [shell](auto...) {
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
    hotkey(KEY_J, shell->main_mod, [shell](auto...) { renderdoc_capture(shell); });

    hotkey(KEY_C, shell->main_mod, [shell](auto...) {
        auto* pw_context = shell_pw_find_plugin(shell);
        if (!pw_context) return;
        pw_context->stream->enabled = !pw_context->stream->enabled;
        wm_toast(shell->wm.get(), std::format("Capture: {}", pw_context->stream->enabled ? "Enabled" : "Disabled"));
    });

    hotkey(KEY_PREVIOUSSONG, {}, [shell](auto...) { shell_launch(shell, "playerctl", {{"playerctl", "previous"}}); });
    hotkey(KEY_PLAYPAUSE,    {}, [shell](auto...) { shell_launch(shell, "playerctl", {{"playerctl", "play-pause"}}); });
    hotkey(KEY_NEXTSONG,     {}, [shell](auto...) { shell_launch(shell, "playerctl", {{"playerctl", "next"}}); });
    hotkey(KEY_VOLUMEDOWN,   {}, [shell](auto...) { shell_launch(shell, "wpctl", {{"wpctl", "set-volume", "@DEFAULT_AUDIO_SINK@", "0.02-"}}); });
    hotkey(KEY_VOLUMEUP,     {}, [shell](auto...) { shell_launch(shell, "wpctl", {{"wpctl", "set-volume", "@DEFAULT_AUDIO_SINK@", "0.02+", "-l", "1.0"}}); });

    hotkey(KEY_SYSRQ /* Print */, {}, [shell](Seat* seat, SeatFocus*) {
        wm_begin_selection(shell->wm.get(), seat_get_pointer(seat), [shell = Weak(shell)](rect2f32 region) {
            if (!shell) return;
            take_screenshot(shell.get(), region);
        });
    });

    for (u32 i = 0; i < 12; ++i) {
        hotkey(KEY_F1 + i, SeatModifier::ctrl | SeatModifier::alt, [shell, session = num_cast<i32>(i + 1)](auto...) {
            log_debug("Switching VT to {}", session);
            io_switch_session(shell->io.get(), session);
        });
    }
}
