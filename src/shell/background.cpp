#include "shell.hpp"

#include <core/math.hpp>
#include <core/log.hpp>

struct ShellBackgroundOutput
{
    Ref<SceneInputRegion> region;
    Ref<SeatFocus> focus;
    Ref<SceneTexture> texture;
};

struct ShellBackground
{
    Shell* shell;

    Ref<WmClient> client;

    Ref<GpuImage>   image;
    Ref<GpuSampler> sampler;

    std::vector<ShellBackgroundOutput> outputs;
};

static
void update_backgrounds(ShellBackground* bg)
{
    auto* shell = bg->shell;

    auto layer = wm_get_layer(shell->wm.get(), WmLayer::background);

    bg->outputs.clear();

    for (auto* output : wm_list_outputs(shell->wm.get())) {
        auto viewport = wm_output_get_viewport(output);

        auto& bgo = bg->outputs.emplace_back();

        bgo.region = scene_input_region_create();
        scene_input_region_set_clip(bgo.region.get(), viewport);
        scene_tree_place_above(layer, nullptr, bgo.region.get());
        bgo.focus = seat_focus_create(wm_get_seat_client(bg->client.get()), bgo.region.get());

        if (bg->image) {
            auto image_size = vec_cast<f32>(bg->image->base()->extent);

            // Create texture node
            bgo.texture = scene_texture_create();
            scene_texture_set_image(bgo.texture.get(), bg->image.get(), bg->sampler.get(), GpuBlendMode::premultiplied);
            auto src = rect_fit<f32>(image_size, viewport.extent);
            scene_texture_set_src(bgo.texture.get(), {src.origin / image_size, src.extent / image_size, xywh});
            scene_texture_set_dst(bgo.texture.get(), viewport);
            scene_tree_place_above(layer, nullptr, bgo.texture.get());
        }
    }
}

static
auto load_background_image(Shell* shell) -> Ref<GpuImage>
{
    if (shell->wallpaper.empty()) {
        log_warn("WALLPAPER not set, no background will be loaded");
        return {};
    }

    int w = {}, h = {};
    int num_channels = {};
    stbi_uc* data = stbi_load(shell->wallpaper.c_str(), &w, &h, &num_channels, STBI_rgb_alpha);
    if (!data || !w || !h) {
        log_error("WALLPAPER [{}] could not be loaded", shell->wallpaper);
        return {};
    }
    defer { stbi_image_free(data); };
    log_info("Loaded background ({}, {}x{})", shell->wallpaper, w, h);

    // Create background texture node
    auto image = gpu_image_create(shell->gpu.get(), {
        .extent = {u32(w), u32(h)},
        .format = gpu_format_from_drm(DRM_FORMAT_XBGR8888),
        .usage = GpuImageUsage::texture | GpuImageUsage::transfer
    });
    gpu_copy_memory_to_image(image.get(), as_bytes(data, w * h * 4), {{{image->base()->extent}}});
    return image;
}

static
void handle_seat_event(ShellBackground* bg, SeatEvent* event)
{
    switch (event->type) {
        break;case SeatEventType::pointer_button: {
            wm_focus(bg->shell->wm.get(), nullptr);
        }
        break;default:
            ;
    }
}

void shell_init_background(Shell* shell)
{
    auto bg = ref_create<ShellBackground>();
    bg->shell = shell;

    bg->sampler = gpu_sampler_create(shell->gpu.get(), {
        .mag = VK_FILTER_NEAREST,
        .min = VK_FILTER_LINEAR,
    });

    bg->image = load_background_image(shell);

    // Listen for outputs to assign backgrounds to
    bg->client = wm_connect(shell->wm.get());
    wm_listen(bg->client.get(), [bg = bg.get()](WmClient*, WmEvent* event) {
        switch (event->type) {
            break;case WmEventType::output_layout:
                update_backgrounds(bg);
            break;case WmEventType::seat_event:
                handle_seat_event(bg, event->seat.event);

            break;default:
                ;
        }
    });

    shell->apps.emplace_back(bg);
}
