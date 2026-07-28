#include "shell.hpp"

#include <stb_image.h>
#include <stb_image_write.h>

struct ShellScreenshotPlugin : ShellPlugin
{
    RefVector<WmHotkey> hotkey;
};

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

static
void start_screenshot(Shell* shell, Seat* seat)
{
    wm_begin_selection(shell->wm.get(), seat_get_pointer(seat), [shell = Weak(shell)](rect2f32 region) {
        if (!shell) return;
        take_screenshot(shell.get(), region);
    });
}

void shell_init_screenshot(Shell* shell)
{
    auto plugin = ref_create<ShellScreenshotPlugin>();
    shell->plugins.emplace_back(plugin);

    plugin->hotkey.emplace_back(wm_bind_hotkey(shell->wm.get(), {}, KEY_SYSRQ /* Print */, [shell](Seat* seat, SeatFocus*) {
        start_screenshot(shell, seat);
    }));

    plugin->hotkey.emplace_back(wm_bind_hotkey(shell->wm.get(), shell->main_mod, KEY_P, [shell](Seat* seat, SeatFocus*) {
        start_screenshot(shell, seat);
    }));
}
