#include "internal.hpp"

#include <core/math.hpp>
#include <core/log.hpp>

struct SeatCursorManager
{
    Gpu* gpu;
    Ref<GpuSampler> sampler;

    std::string theme;
    i32         size;

    ankerl::unordered_dense::map<std::string, Ref<SceneNode>> cache;
};

auto seat_cursor_manager_create(Gpu* gpu, const char* theme, i32 size) -> Ref<SeatCursorManager>
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
void set_visual(SeatPointer* pointer, SceneNode* visual)
{
    if (pointer->cursor_visual.get() == visual) return;

    if (pointer->cursor_visual) {
        scene_node_unparent(pointer->cursor_visual.get());
    }

    if (visual) {
        scene_tree_place_above(pointer->tree.get(), nullptr, visual);
    }
    pointer->cursor_visual = visual;
}

void seat_pointer_set_cursor(SeatPointer* pointer, SceneNode* visual)
{
    set_visual(pointer, visual);
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
    gpu_copy_memory_to_image(image.get(), as_bytes(cursor->pixels, cursor->width * cursor->height * 4), {{{image->base()->extent}}});

    auto visual = scene_texture_create();
    scene_texture_set_image(visual.get(), image.get(), manager->sampler.get(), SceneTextureFlag::premultiplied);
    scene_texture_set_dst(visual.get(), {-vec2f32{num_cast<f32>(cursor->xhot), num_cast<f32>(cursor->yhot)}, {num_cast<f32>(cursor->width), num_cast<f32>(cursor->height)}, xywh});

    manager->cache.insert({semantic, visual});

    return visual.get();
}

void seat_pointer_set_xcursor(SeatPointer* pointer, const char* semantic)
{
    auto visual = semantic ? get_xcursor(pointer->cursor_manager, semantic) : nullptr;

    set_visual(pointer, visual);
}
