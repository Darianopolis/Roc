#include "../shell.hpp"

#include <core/math.hpp>
#include <core/log.hpp>

struct ShellBackground
{
    Shell* shell;

    Ref<WmClient> client;

    Ref<GpuImage>   image;
    Ref<GpuSampler> sampler;

    RefVector<SceneTexture> textures;
};

static
void update_backgrounds(ShellBackground* bg)
{
    auto* shell = bg->shell;

    auto layer = wm_get_layer(shell->wm.get(), WmLayer::background);

    bg->textures.clear();

    for (auto* output : wm_list_outputs(shell->wm.get())) {
        auto image_size = vec_cast<f32>(bg->image->extent());
        auto viewport = wm_output_get_viewport(output);

        // Create texture node
        auto texture = scene_texture_create();
        scene_texture_set_image(texture.get(), bg->image.get(), bg->sampler.get(), GpuBlendMode::premultiplied);
        auto src = rect_fit<f32>(image_size, viewport.extent);
        scene_texture_set_src(texture.get(), {src.origin / image_size, src.extent / image_size, xywh});
        scene_texture_set_dst(texture.get(), viewport);
        scene_tree_place_above(layer, nullptr, texture.get());
        bg->textures.emplace_back(texture.get());
    }
}

void shell_init_background(Shell* shell)
{
    if (shell->wallpaper.empty()) {
        log_warn("WALLPAPER not set, no background will be loaded");
        return;
    }

    int w = {}, h = {};
    int num_channels = {};
    stbi_uc* data = stbi_load(shell->wallpaper.c_str(), &w, &h, &num_channels, STBI_rgb_alpha);
    if (!data || !w || !h) {
        log_error("WALLPAPER [{}] could not be loaded", shell->wallpaper);
        return;
    }
    defer { stbi_image_free(data); };
    log_info("Loaded background ({}, {}x{})", shell->wallpaper, w, h);

    auto bg = ref_create<ShellBackground>();
    bg->shell = shell;

    bg->sampler = gpu_sampler_create(shell->gpu.get(), {
        .mag = VK_FILTER_NEAREST,
        .min = VK_FILTER_LINEAR,
    });

    // Create background texture node
    bg->image = gpu_image_create(shell->gpu.get(), {
        .extent = {u32(w), u32(h)},
        .format = gpu_format_from_drm(DRM_FORMAT_XBGR8888),
        .usage = GpuImageUsage::texture | GpuImageUsage::transfer
    });
    gpu_copy_memory_to_image(bg->image.get(), as_bytes(data, w * h * 4), {{{bg->image->extent()}}});

    // Listen for outputs to assign backgrounds to
    bg->client = wm_connect(shell->wm.get());
    wm_listen(bg->client.get(), [bg = bg.get()](WmClient*, WmEvent* event) {
        switch (event->type) {
            break;case WmEventType::output_layout:
                update_backgrounds(bg);
            break;default:
                ;
        }
    });

    shell->apps.emplace_back(bg);
}
