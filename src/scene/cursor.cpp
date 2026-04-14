#include "internal.hpp"

struct SeatCursorManager
{
    Gpu* gpu;
    Ref<GpuSampler> sampler;

    std::string theme;
    i32         size;

    ankerl::unordered_dense::map<std::string_view, Ref<SceneNode>> cache;
};

auto scene_cursor_manager_create(Gpu* gpu, const char* theme, i32 size) -> Ref<SeatCursorManager>
{
    auto cursor_manager = ref_create<SeatCursorManager>();

    cursor_manager->gpu = gpu;
    cursor_manager->theme = theme;
    cursor_manager->size = size;

    cursor_manager->sampler = gpu_sampler_create(gpu, {
        .mag = VK_FILTER_NEAREST,
        .min = VK_FILTER_LINEAR,
    });

    return cursor_manager;
}

static
auto get_visual(SeatPointer* pointer) -> SceneNode*
{
    debug_assert(pointer->tree->children.size() <= 1);

    return pointer->tree->children.empty()
        ? nullptr
        : pointer->tree->children.front();
}

static
void set_visual(SeatPointer* pointer, SceneNode* visual)
{
    debug_assert(get_visual(pointer) != visual);

    while (!pointer->tree->children.empty()) {
        scene_node_unparent(*pointer->tree->children.begin());
    }

    if (visual) {
        scene_tree_place_above(pointer->tree.get(), nullptr, visual);
    }
}

void seat_pointer_set_cursor(SeatPointer* pointer, SceneNode* visual)
{
    if (visual != get_visual(pointer)) {
        log_trace("scene.pointer.set_cursor({})", visual ? std::format("{}", (void*)visual) : "nullptr");
        set_visual(pointer, visual);
    }
}

static
auto get_xcursor(SeatCursorManager* manager, const char* semantic) -> SceneNode*
{
    auto iter = manager->cache.find(semantic);
    if (iter != manager->cache.end()) {
        return iter->second.get();
    }

    log_debug("Loading XCursor icon \"{}\"", semantic);

    auto* cursor = XcursorLibraryLoadImage(semantic, manager->theme.c_str(), manager->size);

    if (!cursor) {
        debug_assert("default"sv != semantic);
        log_error("XCursor icon \"{}\" not found, falling back to \"default\"", semantic);
        auto fallback = get_xcursor(manager, "default");
        manager->cache.insert({semantic, fallback});
        return fallback;
    }

    defer { XcursorImageDestroy(cursor); };
    auto image = gpu_image_create(manager->gpu, {
        .extent = {cursor->width, cursor->height},
        .format = gpu_format_from_drm(DRM_FORMAT_ABGR8888),
        .usage = GpuImageUsage::texture | GpuImageUsage::transfer
    });
    gpu_copy_memory_to_image(image.get(), as_bytes(cursor->pixels, cursor->width * cursor->height * 4), {{image->extent()}});

    auto visual = scene_texture_create();
    scene_texture_set_image(visual.get(), image.get(), manager->sampler.get(), GpuBlendMode::premultiplied);
    scene_texture_set_dst(visual.get(), {-vec2f32{cursor->xhot, cursor->yhot}, {cursor->width, cursor->height}, xywh});

    manager->cache.insert({semantic, visual});

    return visual.get();
}

void seat_pointer_set_xcursor(SeatPointer* pointer, const char* semantic)
{
    auto visual = semantic ? get_xcursor(pointer->cursor_manager, semantic) : nullptr;

    if (visual != get_visual(pointer)) {
        log_trace("scene.pointer.set_xcursor({})", semantic ?: "nullptr");
        set_visual(pointer, visual);
    }
}
