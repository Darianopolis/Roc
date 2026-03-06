#include "wayland.hpp"

static
void configure(void* udata, xdg_surface* xdg_surface, u32 serial)
{
    auto* output = static_cast<io_output_wayland*>(udata);

    xdg_surface_ack_configure(xdg_surface, serial);
    wl_surface_commit(output->wl_surface);

    bool post_configure = false;

    post_configure |= output->size != output->configure.size;
    output->size = output->configure.size;

    if (post_configure) {
        io_output_post_configure(output);
    }
    io_output_try_redraw_later(output);
}

IO_WL_LISTENER(xdg_surface) = {
    .configure = configure,
};

IO_WL_LISTENER(zxdg_toplevel_decoration_v1) = {
    .configure = [](void*, zxdg_toplevel_decoration_v1*, u32 mode) {
        if (mode == ZXDG_TOPLEVEL_DECORATION_V1_MODE_CLIENT_SIDE) {
            log_warn("IO - <zxdg_toplevel_decoration_v1> requested client-side decorations, outputs will remain undecorated");
        }
    }
};

static
void toplevel_configure(void* udata, xdg_toplevel* toplevel, i32 width, i32 height, wl_array* states)
{
    auto output = static_cast<io_output_wayland*>(udata);

    output->configure.size = (width && height) ? vec2i32{width, height} : vec2i32{1920, 1080};
}

static
void toplevel_close(void* udata, xdg_toplevel*)
{
    auto* output = static_cast<io_output_wayland*>(udata);
    auto* ctx = output->ctx;
    std::erase_if(ctx->wayland->outputs, core_object_equals{output});
    if (ctx->wayland->outputs.empty()) {
        io_request_shutdown(ctx, io_shutdown_reason::no_more_outputs);
    }
}

IO_WL_LISTENER(xdg_toplevel) = {
    .configure = toplevel_configure,
    .close = toplevel_close,
    IO_WL_STUB(xdg_toplevel, configure_bounds),
    IO_WL_STUB(xdg_toplevel, wm_capabilities),
};

IO_WL_LISTENER(zwp_linux_dmabuf_feedback_v1) = {
    .done = [](void*, zwp_linux_dmabuf_feedback_v1 *feedback) {
        zwp_linux_dmabuf_feedback_v1_destroy(feedback);
    },
    IO_WL_STUB(zwp_linux_dmabuf_feedback_v1, format_table),
    IO_WL_STUB(zwp_linux_dmabuf_feedback_v1, main_device),
    IO_WL_STUB(zwp_linux_dmabuf_feedback_v1, tranche_done),
    IO_WL_STUB(zwp_linux_dmabuf_feedback_v1, tranche_target_device),
    IO_WL_STUB(zwp_linux_dmabuf_feedback_v1, tranche_formats),
    IO_WL_STUB(zwp_linux_dmabuf_feedback_v1, tranche_flags),
};

IO_WL_LISTENER(zwp_locked_pointer_v1) = {
    .locked   = [](void* udata, zwp_locked_pointer_v1*) {
        static_cast<io_output_wayland*>(udata)->pointer_locked = true;
    },
    .unlocked = [](void* udata, zwp_locked_pointer_v1*) {
        static_cast<io_output_wayland*>(udata)->pointer_locked = false;
    },
};

void io_add_output(io_context* ctx)
{
    auto* wl = ctx->wayland.get();
    if (!wl) return;

    static u32 window_id = 0;
    auto title = std::format("WL-{}", ++window_id);

    auto output = core_create<io_output_wayland>();
    output->ctx = ctx;
    wl->outputs.emplace_back(output);

    output->wl_surface = wl_compositor_create_surface(wl->wl_compositor);
    output->xdg_surface = xdg_wm_base_get_xdg_surface(wl->xdg_wm_base, output->wl_surface);
    xdg_surface_add_listener(output->xdg_surface, &io_xdg_surface_listener, output.get());

    output->xdg_toplevel = xdg_surface_get_toplevel(output->xdg_surface);
    xdg_toplevel_add_listener(output->xdg_toplevel, &io_xdg_toplevel_listener, output.get());

    xdg_toplevel_set_app_id(output->xdg_toplevel, PROGRAM_NAME);
    xdg_toplevel_set_title(output->xdg_toplevel, title.c_str());

    if (auto decoration_manager = wl->zxdg_decoration_manager_v1) {
        output->zxdg_toplevel_decoration_v1 = zxdg_decoration_manager_v1_get_toplevel_decoration(decoration_manager, output->xdg_toplevel);
        zxdg_toplevel_decoration_v1_add_listener(output->zxdg_toplevel_decoration_v1, &io_zxdg_toplevel_decoration_v1_listener, output.get());
        zxdg_toplevel_decoration_v1_set_mode(output->zxdg_toplevel_decoration_v1, ZXDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE);
    } else {
        log_warn("IO - <zxdg_decoration_manager_v1> protocol not available, outputs will remain undecorated");
    }

    output->zwp_locked_pointer_v1 = zwp_pointer_constraints_v1_lock_pointer(
        wl->zwp_pointer_constraints_v1,
        output->wl_surface,
        wl->pointer->wl_pointer,
        nullptr,
        ZWP_POINTER_CONSTRAINTS_V1_LIFETIME_PERSISTENT);
    zwp_locked_pointer_v1_add_listener(output->zwp_locked_pointer_v1, &io_zwp_locked_pointer_v1_listener, output.get());

    output->wp_linux_drm_syncobj_surface_v1 = wp_linux_drm_syncobj_manager_v1_get_surface(wl->wp_linux_drm_syncobj_manager_v1, output->wl_surface);

    wl_surface_commit(output->wl_surface);
    wl_display_flush(wl->wl_display);

    io_output_add(output.get());
}

// -----------------------------------------------------------------------------

static
wl_buffer* get_image_proxy(io_context* ctx, gpu_image* image)
{
    auto* wl = ctx->wayland.get();

    if (auto* found = wl->buffer_cache.find(image)) return found;

    auto size = image->extent;
    auto format = image->format;

    auto dma_params = gpu_image_export_dmabuf(image);
    u32 mod_hi = dma_params.modifier >> 32;
    u32 mod_lo = dma_params.modifier & ~0u;

    auto buffer_params = zwp_linux_dmabuf_v1_create_params(wl->zwp_linux_dmabuf_v1);
    for (u32 plane_idx = 0; plane_idx < dma_params.planes.count; ++plane_idx) {
        auto& plane = dma_params.planes[plane_idx];
        zwp_linux_buffer_params_v1_add(buffer_params, plane.fd.get(), plane_idx, plane.offset, plane.stride, mod_hi, mod_lo);
    }
    auto buffer = zwp_linux_buffer_params_v1_create_immed(buffer_params, size.x, size.y, format->drm, 0);
    zwp_linux_buffer_params_v1_destroy(buffer_params);

    return wl->buffer_cache.insert(image, buffer);
}

static
wp_linux_drm_syncobj_timeline_v1* get_semaphore_proxy(io_context* ctx, gpu_semaphore* semaphore)
{
    auto* wl = ctx->wayland.get();

    if (auto* found = wl->syncobj_cache.find(semaphore)) return found;

    auto fd = gpu_semaphore_export_syncobj(semaphore);
    auto syncobj = wp_linux_drm_syncobj_manager_v1_import_timeline(wl->wp_linux_drm_syncobj_manager_v1, fd);
    close(fd);

    return wl->syncobj_cache.insert(semaphore, syncobj);
}

static
void on_present_frame(void* udata, wl_callback*, u32 time)
{
    auto* output = static_cast<io_output_wayland*>(udata);

    wl_callback_destroy(output->frame_callback);
    output->frame_callback = nullptr;

    if (!output->commit_available) {
        output->commit_available = true;
        io_output_try_redraw(output);
    }
}

void io_output_wayland::commit(gpu_image* image, gpu_syncpoint acquire, gpu_syncpoint release, flags<io_output_commit_flag> flags)
{
    core_assert(commit_available);

    auto* wl_buffer = get_image_proxy(ctx, image);
    auto* acquire_syncpoint = get_semaphore_proxy(ctx, acquire.semaphore);
    auto* release_syncpoint = get_semaphore_proxy(ctx, release.semaphore);

    wl_surface_attach(wl_surface, wl_buffer, 0, 0);
    wl_surface_damage_buffer(wl_surface, 0, 0, INT32_MAX, INT32_MAX);

    wp_linux_drm_syncobj_surface_v1_set_acquire_point(wp_linux_drm_syncobj_surface_v1, acquire_syncpoint, acquire.value >> 32, acquire.value & ~0u);
    wp_linux_drm_syncobj_surface_v1_set_release_point(wp_linux_drm_syncobj_surface_v1, release_syncpoint, release.value >> 32, release.value & ~0u);

    if (!frame_callback) {
        frame_callback = wl_surface_frame(wl_surface);
        static constexpr wl_callback_listener listener { on_present_frame };
        wl_callback_add_listener(frame_callback, &listener, this);
    }

    if (flags.contains(io_output_commit_flag::vsync)) {
        commit_available = false;
    }

    wl_surface_commit(wl_surface);
    wl_display_flush(ctx->wayland->wl_display);
}


io_output_wayland::~io_output_wayland()
{
    wp_linux_drm_syncobj_surface_v1_destroy(wp_linux_drm_syncobj_surface_v1);

    if (frame_callback) wl_callback_destroy(frame_callback);

    zwp_locked_pointer_v1_destroy(zwp_locked_pointer_v1);

    if (zxdg_toplevel_decoration_v1) zxdg_toplevel_decoration_v1_destroy(zxdg_toplevel_decoration_v1);

    xdg_toplevel_destroy(xdg_toplevel);
    xdg_surface_destroy(xdg_surface);
    wl_surface_destroy(wl_surface);
}
