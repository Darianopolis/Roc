#include "shell.hpp"

#include <core/process.hpp>
#include <way/surface/surface.hpp>

static
void print_scene_graph(Shell* shell)
{
    auto* wm = shell->wm.get();
    auto* way = shell->way.get();

    u32 depth = 0;
    auto indent = [&] { return std::string(depth, ' '); };
    scene_iterate<SceneIterateDirection::back_to_front>(
        wm_get_layer(wm, WmLayer::window)->parent,
        [&](SceneTree* tree) {
            WaySurface* surface;
            if (tree->userdata.id == way->userdata_id
                    && (surface = way_get_userdata<WaySurface>(way, tree->userdata.data))) {
                log_warn("{}tree({}{}) {{", indent(),
                    surface->role,
                    tree->enabled ? "": ", disabled");
            } else {
                log_warn("{}tree{} {{", indent(), tree->enabled ? "": "(disabled)");
            }
            depth += 2;
        },
        [&](SceneNode* node) {
            log_warn("{}{}", indent(), typeid(*node).name());
        },
        [&](SceneTree* tree) {
            depth -= 2;
            log_warn("{}}}", indent());
        });
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
    for (auto* output : wm_list_outputs(wm)) {
        auto viewport = wm_output_get_viewport(output);
        auto texture = gpu_image_create(gpu, {
            .extent = vec_cast<u32>(viewport.extent),
            .format = gpu_format_from_drm(DRM_FORMAT_ABGR8888),
            .usage = GpuImageUsage::render
        });
        scene_render(wm_get_scene(wm), texture.get(), viewport);
        gpu_wait(gpu_flush(gpu));
    }
    gpu->renderdoc->EndFrameCapture(nullptr, nullptr);
}

static
auto filter_event(Shell* shell, SeatEvent* event) -> SeatEventFilterResult
{
    switch (event->type) {
        break;case SeatEventType::keyboard_key:
            if (!event->keyboard.key.pressed) return {};

            if (seat_keyboard_get_modifiers(event->keyboard.keyboard).contains(shell->main_mod)) {
                switch (event->keyboard.key.code) {
                    break;case KEY_N:
                        spawn_path("systemctl", {{"systemctl", "suspend"}});
                        return SeatEventFilterResult::capture;
                    break;case KEY_D:
                        spawn_path("launcher", {{"launcher"}});
                        return SeatEventFilterResult::capture;
                    break;case KEY_ESC:
                        io_stop(shell->io.get());
                        return SeatEventFilterResult::capture;
                    break;case KEY_T:
                        way_clear(shell->way.get());
                        return SeatEventFilterResult::capture;
                    break;case KEY_O:
                        io_output_create(shell->io.get());
                        return SeatEventFilterResult::capture;
                    break;case KEY_G:
                        print_scene_graph(shell);
                        return SeatEventFilterResult::capture;
                    break;case KEY_J:
                        renderdoc_capture(shell);
                        return SeatEventFilterResult::capture;
                }
            }

            switch (event->keyboard.key.code) {
                break;case KEY_PREVIOUSSONG:
                    spawn_path("playerctl", {{"playerctl", "previous"}});
                    return SeatEventFilterResult::capture;
                break;case KEY_PLAYPAUSE:
                    spawn_path("playerctl", {{"playerctl", "play-pause"}});
                    return SeatEventFilterResult::capture;
                break;case KEY_NEXTSONG:
                    spawn_path("playerctl", {{"playerctl", "next"}});
                    return SeatEventFilterResult::capture;
                break;case KEY_VOLUMEDOWN:
                    spawn_path("wpctl", {{"wpctl", "set-volume", "@DEFAULT_AUDIO_SINK@", "0.02-"}});
                    return SeatEventFilterResult::capture;
                break;case KEY_VOLUMEUP:
                    spawn_path("wpctl", {{"wpctl", "set-volume", "@DEFAULT_AUDIO_SINK@", "0.02+", "-l", "1.0"}});
                    return SeatEventFilterResult::capture;
            }
        break;default:
            ;
    }
    return {};
}

void shell_init_hotkeys(Shell* shell)
{
    for (auto* seat : wm_get_seats(shell->wm.get())) {
        shell->apps.emplace_back( seat_add_event_filter(seat, [shell](SeatEvent* event) {
            return filter_event(shell, event);
        }));
    }
}
