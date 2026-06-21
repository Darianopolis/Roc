#include "internal.hpp"

#include <core/log.hpp>

#define WM_NOISY_COMPOSITING 0

static
auto get_topmost_texture(SceneTree* tree, rect2f32 viewport) -> SceneTexture*
{
    if (!tree->enabled) return nullptr;
    for (auto* child : tree->children | std::views::reverse) {
        if (child->type == SceneNodeType::texture) {
            auto texture = static_cast<SceneTexture*>(child);
            auto dst = texture->dst;
            dst.origin += scene_tree_get_position(tree);
            if (rect_intersects(dst, viewport)) return texture;
        }
        if (child->type == SceneNodeType::tree) {
            if (auto* texture = get_topmost_texture(static_cast<SceneTree*>(child), viewport)) {
                return texture;
            }
        }
    }
    return nullptr;
}

static
bool try_direct_scanout(WmOutput* output, const GpuFormatSet* formats)
{
    auto* server = output->server;

    // Find topmost image in scene that intersects with the output

    auto texture = get_topmost_texture(wm_get_scene(server), wm_output_get_viewport(output));
    if (!texture) return false;
    if (!texture->image) return false;

    // Check that image covers entire output
    // TODO: Check for opaqueness

    auto dst = texture->dst;
    dst.origin += scene_tree_get_position(texture->parent);
    auto viewport = wm_output_get_viewport(output);
    if (dst != viewport) {
        return false;
    }

    // Check that image is exportable

    auto* image = texture->image.get();
    if (!gpu_image_is_exportable(image)) {
        return false;
    }

    // Check that image format is compatible
    // TODO: Should we require this to be handled in the commit implementations?

    auto* base = image->base();
    if (!formats->get(base->format).contains(base->modifier)) {
        return false;
    }

#if WM_NOISY_COMPOSITING
    log_debug("Applying direct scanout optimizations @ {}", viewport);
#endif

    // TODO: We need to QFOT the image back to foreign in this case
    //       We should delay QFOT from foreign to avoid needing to do this on what should be the fast-path

    bool committed = output->interface.commit(output->userdata, {
        .planes = {{
            {
                .image = image,
                .type = WmOutputPlaneType::primary,
            }
        }},
        .ready = gpu_flush(output->server->gpu),
        .flags = WmOutputCommitFlag::vsync
    });

#if WM_NOISY_COMPOSITING
    if (!committed) {
        log_error("Direct scanout commit failed, falling back to composition");
    }
#endif

    return committed;
}

auto wm_output_frame(WmOutput* output, const GpuFormatSet* formats) -> bool
{
    auto* server = output->server;

    if (output->bump_frame_id) {
        output->frame_id = ++server->io.prev_frame_id;
        output->bump_frame_id = false;
    }

    wm_broadcast_event(server, ptr_to(WmEvent {
        .output = {
            .type = WmEventType::output_frame,
            .output = output,
            .frame_id = output->frame_id,
        }
    }));

    if (!output->needs_redraw) return false;

    output->needs_redraw = false;
    output->bump_frame_id = true;

    if (try_direct_scanout(output, formats)) {
        return true;
    }

    auto format = gpu_format_from_drm(DRM_FORMAT_ABGR8888);
    auto usage = GpuImageUsage::render;

    auto target = server->image_pool->acquire({
        .extent = vec_cast<u32>(output->viewport.extent),
        .format = format,
        .usage = usage,
        .modifiers = ptr_to(gpu_intersect_format_modifiers({{
            &gpu_get_format_properties(server->gpu, format, usage)->mods,
            &formats->get(format),
        }}))
    });

    scene_render(wm_get_scene_renderer(server), wm_get_scene(server), target.get(), wm_output_get_viewport(output));

    bool committed = output->interface.commit(output->userdata, {
        .planes = {{
            {
                .image = target.get(),
                .type = WmOutputPlaneType::primary,
            }
        }},
        .ready = gpu_flush(server->gpu),
        .flags = WmOutputCommitFlag::vsync
    });
    debug_assert(committed, "Failed to commit to output!");

    return true;
}
