#include "internal.hpp"

#include <core/log.hpp>

#define WM_NOISY_COMPOSITING 0

static
bool try_commit(WmOutput* output, GpuImage* primary, bool use_cursor_plane)
{
    auto* server = output->server;

    auto* pointer = seat_get_pointer(wm_get_seat(server));

    auto pointer_region = server->cursor_image_bounds;
    pointer_region.origin += seat_pointer_get_position(pointer);

    return output->interface.commit(output->userdata, {
        .primary { .image = primary },
        .cursor {
            .image = use_cursor_plane && !aabb_is_empty<f32>(pointer_region) && rect_intersects<f32>(pointer_region, output->viewport)
                ? server->cursor_image.get()
                : nullptr,
            .position = vec_cast<i32>(pointer_region.origin - output->viewport.origin),
        },
        .ready = gpu_flush(server->gpu),
        .flags = WmOutputCommitFlag::vsync
    });
}

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
bool try_direct_scanout(WmOutput* output, const GpuFormatSet* formats, bool use_cursor_plane)
{
    auto* server = output->server;

    // Find topmost image in scene that intersects with the output

    auto texture = get_topmost_texture(server->scene_primary_tree.get(), wm_output_get_viewport(output));
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

    bool committed = try_commit(output, image, use_cursor_plane);

#if WM_NOISY_COMPOSITING
    if (!committed) {
        log_error("Direct scanout commit failed, falling back to composition");
    }
#endif

    return committed;
}

static
auto get_cursor_bounds(WmServer* server) -> aabb2f32
{
    aabb2f32 new_cursor_region = aabb_make_empty<f32>();
    [&](this auto&& visit, SceneNode* node, vec2f32 translation) -> void {
        scene_visit(node, OverloadSet {
            [&](SceneTexture* texture) {
                auto dst = texture->dst;
                dst.origin += translation;
                new_cursor_region = aabb_outer<f32>(new_cursor_region, dst);
            },
            [&](SceneTree* tree) {
                if (!tree->enabled) return;
                translation += tree->translation;
                for (auto* child : tree->children) {
                    visit(child, translation);
                }
            },
            [&](SceneInputRegion*) {}
        });
    }(wm_get_layer(server, WmLayer::cursor), {});
    return new_cursor_region;
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

    // Cursor

    wm_prepare_cursor_image(server);
    auto use_cursor_plane = server->cursor_image_valid && !server->debug.disable_cursor_plane;

    auto composited_cursor_bounds = use_cursor_plane ? aabb_make_empty<f32>() : get_cursor_bounds(server);
    composited_cursor_bounds = aabb_inner<f32>(composited_cursor_bounds, output->viewport);
    if (output->last_cursor_bounds != composited_cursor_bounds || (!use_cursor_plane && output->cursor_damaged)) {
        auto cursor_damage = region_op<RegionOpUnion>(RegionSingle<f32>(output->last_cursor_bounds),
                                                      RegionSingle<f32>(composited_cursor_bounds));
        region_op<RegionOpUnion>(output->primary_damage, output->primary_damage, cursor_damage);
        output->last_cursor_bounds = composited_cursor_bounds;
    }

    // Try direct scanout

    if (use_cursor_plane || aabb_is_empty(composited_cursor_bounds)) {
        if (try_direct_scanout(output, formats, use_cursor_plane)) {
            // Set infinite damage to avoid accumulating damage cruft
            output->primary_damage = aabb_make_infinite<f32>();
            return true;
        }
    }

    // Update primary surface

    if (!output->primary_damage.empty()) {
        auto format = gpu_format_from_drm(DRM_FORMAT_ABGR8888);
        auto usage = GpuImageUsage::render | GpuImageUsage::storage;

        output->primary_image = server->image_pool->acquire({
            .extent = vec_cast<u32>(output->viewport.extent),
            .format = format,
            .usage = usage,
            .modifiers = ptr_to(gpu_intersect_format_modifiers({{
                &gpu_get_format_properties(server->gpu, format, usage)->mods,
                &formats->get(format),
            }}))
        });

        Flags<SceneRenderOption> flags = {};
        if (server->debug.use_compute) flags |= SceneRenderOption::use_compute;
        if (server->debug.show_damage) flags |= SceneRenderOption::show_damage;
        scene_render(wm_get_scene_renderer(server), {
            .options = flags,
            .root = use_cursor_plane
                ? server->scene_primary_tree.get()
                : server->scene_root.get(),
            .target = output->primary_image.get(),
            .viewport = wm_output_get_viewport(output),
            .damage = &output->primary_damage,
        });
        output->primary_damage.clear();
    }

    // Commit composited result

    bool committed = try_commit(output, output->primary_image.get(), use_cursor_plane);
    if (!committed) {
        log_error("Failed to commit an anything to output, session was probably suspended");
    }

    return committed;
}
