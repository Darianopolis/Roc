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
            .usage = GpuImageUsage::render
        });
        scene_render(wm_get_scene_renderer(wm), wm_get_scene(wm), texture.get(), viewport);
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
        .usage = GpuImageUsage::render
    });

    auto buffer = gpu_buffer_create(gpu, extent.x * extent.y * 4, GpuBufferFlag::host);

    scene_render(wm_get_scene_renderer(wm), wm_get_scene(wm), texture.get(), region);
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

            stbi_write_png(save_path.c_str(), extent.x, extent.y, STBI_rgb_alpha, data.data(), extent.x * 4);
            auto end = std::chrono::steady_clock::now();
            log_info("Screenshot saved to [{}] in {}", save_path, FmtDuration{end - start});
        }}.detach();
    });
}

static
auto filter_event(Shell* shell, SeatEvent* event) -> SeatEventFilterResult
{
    switch (event->type) {
        break;case SeatEventType::keyboard_key: {
            if (!event->keyboard.key.pressed) return {};

            auto mods = seat_keyboard_get_modifiers(event->keyboard.keyboard);

            if (mods.contains(SeatModifier::ctrl | SeatModifier::alt)) {
                switch (event->keyboard.key.code) {
                    break;case KEY_F1 ... KEY_F12:
                        if (mods.contains(SeatModifier::ctrl)) {
                            auto session = 1 + event->keyboard.key.code - KEY_F1;
                            log_debug("Switching VT to {}", session);
                            io_switch_session(shell->io.get(), session);
                        }
                }
            }

            if (mods.contains(shell->main_mod)) {
                switch (event->keyboard.key.code) {
                    break;case KEY_N:
                        shell_launch(shell, "systemctl", {{"systemctl", "suspend"}});
                        return SeatEventFilterResult::capture;
                    break;case KEY_D:
                        shell_launch(shell, "launcher", {{"launcher"}});
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
                    shell_launch(shell, "playerctl", {{"playerctl", "previous"}});
                    return SeatEventFilterResult::capture;
                break;case KEY_PLAYPAUSE:
                    shell_launch(shell, "playerctl", {{"playerctl", "play-pause"}});
                    return SeatEventFilterResult::capture;
                break;case KEY_NEXTSONG:
                    shell_launch(shell, "playerctl", {{"playerctl", "next"}});
                    return SeatEventFilterResult::capture;
                break;case KEY_VOLUMEDOWN:
                    shell_launch(shell, "wpctl", {{"wpctl", "set-volume", "@DEFAULT_AUDIO_SINK@", "0.02-"}});
                    return SeatEventFilterResult::capture;
                break;case KEY_VOLUMEUP:
                    shell_launch(shell, "wpctl", {{"wpctl", "set-volume", "@DEFAULT_AUDIO_SINK@", "0.02+", "-l", "1.0"}});
                    return SeatEventFilterResult::capture;
                break;case KEY_SYSRQ /* PRINT */: {
                    auto* seat = seat_keyboard_get_seat(event->keyboard.keyboard);
                    wm_begin_selection(shell->wm.get(), seat_get_pointer(seat), [shell = Weak(shell)](rect2f32 region) {
                        if (!shell) return;
                        take_screenshot(shell.get(), region);
                    });
                }
            }
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
