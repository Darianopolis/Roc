#include "internal.hpp"

struct scene_cursor_manager
{
    const char* theme = "breeze_cursors";
    i32         size = 24;

    ankerl::unordered_dense::map<std::string_view, ref<scene_node>> cache;
};

CORE_OBJECT_EXPLICIT_DEFINE(scene_cursor_manager);

void scene_cursor_manager_init(scene_context* ctx)
{
    ctx->cursor_manager = core_create<scene_cursor_manager>();
}

static
auto get_visual(scene_pointer* pointer) -> scene_node*
{
    core_assert(pointer->tree->children.size() <= 1);

    return pointer->tree->children.empty()
        ? nullptr
        : pointer->tree->children.front();
}

void scene_pointer_set_cursor(scene_pointer* pointer, scene_node* visual)
{
    if (get_visual(pointer) == visual) return;

    while (!pointer->tree->children.empty()) {
        scene_node_unparent(*pointer->tree->children.begin());
    }

    if (visual) {
        scene_tree_place_above(pointer->tree.get(), nullptr, visual);
    }
}

static
auto get_xcursor(scene_context* ctx, const char* semantic) -> scene_node*
{
    auto* manager = ctx->cursor_manager.get();

    auto iter = manager->cache.find(semantic);
    if (iter != manager->cache.end()) {
        return iter->second.get();
    }

    log_debug("Loading XCursor icon \"{}\"", semantic);

    auto* cursor = XcursorLibraryLoadImage(semantic, manager->theme, manager->size);

    if (!cursor) {
        core_assert("default"sv != semantic);
        log_error("XCursor icon \"{}\" not found, falling back to \"default\"", semantic);
        auto fallback = get_xcursor(ctx, "default");
        manager->cache.insert({semantic, fallback});
        return fallback;
    }

    defer { XcursorImageDestroy(cursor); };
    auto image = gpu_image_create(ctx->gpu, {
        .extent = {cursor->width, cursor->height},
        .format = gpu_format_from_drm(DRM_FORMAT_ABGR8888),
        .usage = gpu_image_usage::texture | gpu_image_usage::transfer
    });
    gpu_copy_memory_to_image(image.get(), {cursor->pixels, cursor->width * cursor->height * 4}, {{image->extent()}});

    auto visual = scene_texture_create(ctx);
    scene_texture_set_image(visual.get(), image.get(), ctx->render.sampler.get(), gpu_blend_mode::premultiplied);
    scene_texture_set_dst(visual.get(), {-vec2f32{cursor->xhot, cursor->yhot}, {cursor->width, cursor->height}, core_xywh});

    manager->cache.insert({semantic, visual});

    return visual.get();
}

void scene_pointer_set_xcursor(scene_pointer* pointer, const char* xcursor_semantic)
{
    auto visual = xcursor_semantic ? get_xcursor(pointer->ctx, xcursor_semantic) : nullptr;

    if (visual != get_visual(pointer)) {
        log_trace("set_xcursor({})", xcursor_semantic ?: "nullptr");
        scene_pointer_set_cursor(pointer, visual);
    }
}
