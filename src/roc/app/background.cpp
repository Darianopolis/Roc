#include "../roc.hpp"

#include "core/math.hpp"

struct RocBackground
{
    Roc* roc;

    Ref<SceneClient> client;
    Ref<GpuImage>    image;
    Ref<GpuSampler>  sampler;
    Ref<SceneTree>   layer;
};

static
void unparent_background(RocBackground* bg)
{
    if (bg->layer) {
        scene_node_unparent(bg->layer.get());
    }
}

static
void update_backgrounds(RocBackground* bg)
{
    auto* roc = bg->roc;

    unparent_background(bg);
    bg->layer = scene_tree_create(roc->scene);
    scene_tree_place_above(scene_get_layer(roc->scene, SceneLayer::background), nullptr, bg->layer.get());

    for (auto* output : scene_list_outputs(roc->scene)) {
        vec2f32 image_size = bg->image->extent();
        auto viewport = scene_output_get_viewport(output);

        // Create texture node
        auto texture = scene_texture_create(roc->scene);
        scene_texture_set_image(texture.get(), bg->image.get(), bg->sampler.get(), GpuBlendMode::premultiplied);
        auto src = rect_fit<f32>(image_size, viewport.extent);
        scene_texture_set_src(texture.get(), {src.origin / image_size, src.extent / image_size, xywh});
        scene_texture_set_dst(texture.get(), viewport);
        scene_tree_place_above(bg->layer.get(), nullptr, texture.get());
    }
}

auto roc_init_background(Roc* roc) -> Ref<void>
{
    auto bg = ref_create<RocBackground>();
    bg->roc = roc;

    bg->sampler = gpu_sampler_create(roc->gpu, {
        .mag = VK_FILTER_NEAREST,
        .min = VK_FILTER_LINEAR,
    });

    bg->image = [&] {
        int w, h;
        int num_channels;
        stbi_uc* data = stbi_load(roc->wallpaper.c_str(), &w, &h, &num_channels, STBI_rgb_alpha);
        defer { stbi_image_free(data); };
        log_info("Loaded background ({}, width = {}, height = {})", roc->wallpaper.c_str(), w, h);

        // Create background texture node
        auto image = gpu_image_create(roc->gpu, {
            .extent = {w, h},
            .format = gpu_format_from_drm(DRM_FORMAT_XBGR8888),
            .usage = GpuImageUsage::texture | GpuImageUsage::transfer
        });
        gpu_copy_memory_to_image(image.get(), as_bytes(data, w * h * 4), {{{image->extent()}}});
        return image;
    }();

    bg->client = scene_client_create(roc->scene);

    scene_client_set_event_handler(bg->client.get(), [bg = bg.get()](SceneEvent* event) {
        switch (event->type) {
            break;case SceneEventType::output_layout:
                update_backgrounds(bg);
            break;default:
                ;
        }
    });

    return bg;
}
