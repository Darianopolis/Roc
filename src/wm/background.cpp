#include "internal.hpp"

#include "core/math.hpp"

static
void unparent_background(WindowManager* wm)
{
    if (wm->background.layer) {
        scene_node_unparent(wm->background.layer.get());
    }
}

static
void update_backgrounds(WindowManager* wm)
{
        unparent_background(wm);
        wm->background.layer = scene_tree_create(wm->scene);
        scene_tree_place_above(scene_get_layer(wm->scene, SceneLayer::background), nullptr, wm->background.layer.get());

        for (auto* output : scene_list_outputs(wm->scene)) {
            vec2f32 image_size = wm->background.image->extent();
            auto viewport = scene_output_get_viewport(output);

            // Create texture node
            auto texture = scene_texture_create(wm->scene);
            scene_texture_set_image(texture.get(), wm->background.image.get(), wm->background.sampler.get(), GpuBlendMode::premultiplied);
            auto src = rect_fit<f32>(image_size, viewport.extent);
            scene_texture_set_src(texture.get(), {src.origin / image_size, src.extent / image_size, xywh});
            scene_texture_set_dst(texture.get(), viewport);
            scene_tree_place_above(wm->background.layer.get(), nullptr, texture.get());
        }
}

void wm_init_background(WindowManager* wm, const std::filesystem::path& path)
{
    wm->background.sampler = gpu_sampler_create(wm->gpu, {
        .mag = VK_FILTER_NEAREST,
        .min = VK_FILTER_LINEAR,
    });

    wm->background.image = [wm, path = path] {
        int w, h;
        int num_channels;
        stbi_uc* data = stbi_load(path.c_str(), &w, &h, &num_channels, STBI_rgb_alpha);
        defer { stbi_image_free(data); };
        log_info("Loaded background ({}, width = {}, height = {})", path.c_str(), w, h);

        // Create background texture node
        auto image = gpu_image_create(wm->gpu, {
            .extent = {w, h},
            .format = gpu_format_from_drm(DRM_FORMAT_XBGR8888),
            .usage = GpuImageUsage::texture | GpuImageUsage::transfer
        });
        gpu_copy_memory_to_image(image.get(), as_bytes(data, w * h * 4), {{{image->extent()}}});
        return image;
    }();

    wm->background.client = scene_client_create(wm->scene);

    scene_client_set_event_handler(wm->background.client.get(), [wm](SceneEvent* event) {
        switch (event->type) {
            break;case SceneEventType::output_layout:
                update_backgrounds(wm);
            break;default:
                ;
        }
    });
}
