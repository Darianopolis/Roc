#include "backend.hpp"

#include "wroc/util.hpp"

#include "gpu/gpu.hpp"

#include "wroc/event.hpp"

static
void dma_feedback_done(void* data, struct zwp_linux_dmabuf_feedback_v1* feedback)
{
    zwp_linux_dmabuf_feedback_v1_destroy(feedback);
}

static
void dma_feedback_format_table(void* data, struct zwp_linux_dmabuf_feedback_v1* zwp_linux_dmabuf_feedback_v1, int fd, u32 size)
{
    auto* backend = static_cast<wroc_wayland_backend*>(data);

    struct entry {
        u32 format;
        u32 padding;
        u64 modifier;
    };

    core_assert(size % sizeof(entry) == 0);

    defer { close(fd); };
    auto mapped = static_cast<entry*>(mmap(nullptr, size, PROT_READ, MAP_SHARED, fd, 0));
    core_assert(mapped);
    defer { munmap(mapped, size); };

    auto count = size / sizeof(entry);
    auto formats = std::span(mapped, count);

    backend->format_table.clear();
    backend->format_set.clear();
    for (auto& entry : formats) {
        if (auto format = gpu_format_from_drm(entry.format)) {
            backend->format_table.emplace_back(format, entry.modifier);
        }
    }
}

static
void dma_feedback_tranche_formats(void* data, struct zwp_linux_dmabuf_feedback_v1* zwp_linux_dmabuf_feedback_v1, struct wl_array* indices)
{
    auto* backend = static_cast<wroc_wayland_backend*>(data);

    for (auto[i, idx] : wroc_to_span<u16>(indices) | std::views::enumerate) {
        auto[format, modifier] = backend->format_table[idx];
        backend->format_set.add(format, modifier);
    }
}

const zwp_linux_dmabuf_feedback_v1_listener wroc_zwp_linux_dmabuf_feedback_v1_listener = {
    .done = dma_feedback_done,
    .format_table = dma_feedback_format_table,
    WROC_STUB_QUIET(main_device),
    WROC_STUB_QUIET(tranche_done),
    WROC_STUB_QUIET(tranche_target_device),
    .tranche_formats = dma_feedback_tranche_formats,
    WROC_STUB_QUIET(tranche_flags),
};

const gpu_format_set& wroc_wayland_backend::get_output_format_set()
{
    return format_set;
}

// -----------------------------------------------------------------------------

wroc_wayland_output* wroc_wayland_backend_find_output_for_surface(wroc_wayland_backend* backend, wl_surface* surface)
{
    for (auto& output : backend->outputs) {
        if (output->wl_surface == surface) return output.get();
    }
    return nullptr;
}

// -----------------------------------------------------------------------------

static
void wroc_listen_xdg_surface_configure(void* data, xdg_surface* surface, u32 serial)
{
    auto* output = static_cast<wroc_wayland_output*>(data);

    log_debug("xdg_surface::configure");
    log_debug("  serial = {}", serial);

    xdg_surface_ack_configure(surface, serial);

    if (!output->frame_callback && !output->frame_available && output->size.x && output->size.y) {
        log_info("Initial configure complete, marking output frame available");
        output->frame_available = true;
        wroc_post_event(wroc_output_event {
            .type = wroc_event_type::output_frame,
            .output = output,
        });
    }
}

const xdg_surface_listener wroc_xdg_surface_listener {
    .configure = wroc_listen_xdg_surface_configure,
};

// -----------------------------------------------------------------------------

static constexpr vec2i32 wroc_backend_default_output_size = { 1920, 1080 };

static
void wroc_listen_toplevel_configure(void* data, xdg_toplevel*, i32 width, i32 height, wl_array* states)
{
    auto output = static_cast<wroc_wayland_output*>(data);

    log_debug("xdg_toplevel::configure", width, height);
    log_debug("  size = ({}, {})", width, height);

    if (width == 0 && height == 0) {
        output->size = wroc_backend_default_output_size;
    } else {
        output->size = {width, height};
    }

    output->desc.modes = {
        {
            .size = output->size,
            .refresh = 0,
        }
    };

    for (auto[i, state] : wroc_to_span<xdg_toplevel_state>(states) | std::views::enumerate) {
        log_debug("  states[{}] = {}", i, core_enum_to_string(state));
    }

    wroc_post_event(wroc_output_event {
        .type = wroc_event_type::output_added,
        .output = output,
    });
}

static
void wroc_listen_toplevel_close(void* data, xdg_toplevel*)
{
    auto output = static_cast<wroc_wayland_output*>(data);

    log_debug("xdg_toplevel::close");

    auto* backend = static_cast<wroc_wayland_backend*>(server->backend.get());
    backend->destroy_output(output);

    if (backend->outputs.empty()) {
        log_debug("Last output closed, quitting...");
        wroc_terminate();
    }
}

static
void wroc_listen_toplevel_configure_bounds(void* data, xdg_toplevel*, i32 width, i32 height)
{
    log_debug("xdg_toplevel::configure_bounds");
    log_debug("  bounds = ({}, {})", width, height);
}

static
void wroc_listen_toplevel_wm_capabilities(void* data, xdg_toplevel*, wl_array* capabilities)
{
    log_debug("xdg_toplevel::wm_capabilities");

    for (auto[i, capability] : wroc_to_span<xdg_toplevel_state>(capabilities) | std::views::enumerate) {
        log_debug("  capabilities[] = {}", i, core_enum_to_string(capability));
    }
}

const xdg_toplevel_listener wroc_xdg_toplevel_listener {
    .configure        = wroc_listen_toplevel_configure,
    .close            = wroc_listen_toplevel_close,
    .configure_bounds = wroc_listen_toplevel_configure_bounds,
    .wm_capabilities  = wroc_listen_toplevel_wm_capabilities,
};

// -----------------------------------------------------------------------------

static
void wroc_listen_toplevel_decoration_configure(void* data, zxdg_toplevel_decoration_v1*, u32 mode)
{
    if (mode == ZXDG_TOPLEVEL_DECORATION_V1_MODE_CLIENT_SIDE) {
        log_warn("Parent compositor requested client-side decorations");
    }
}

const zxdg_toplevel_decoration_v1_listener wroc_zxdg_toplevel_decoration_v1_listener {
    .configure = wroc_listen_toplevel_decoration_configure,
};

// -----------------------------------------------------------------------------

static
void locked(void* data, zwp_locked_pointer_v1*)
{
    log_debug("Wayland backend - pointer locked");
    static_cast<wroc_wayland_output*>(data)->locked = true;
}

static
void unlocked(void* data, zwp_locked_pointer_v1*)
{
    log_debug("Wayland backend - pointer unlocked");
    static_cast<wroc_wayland_output*>(data)->locked = false;
}

const zwp_locked_pointer_v1_listener wroc_zwp_locked_pointer_v1_listener {
    .locked   = locked,
    .unlocked = unlocked,
};

void wroc_wayland_backend_update_pointer_constraint(wroc_wayland_output* output)
{
    if (output->locked_pointer) return;

    auto* backend = static_cast<wroc_wayland_backend*>(server->backend.get());
    if (!backend->pointer) {
        log_warn("Could not create pointer constraint, pointer not acquired yet");
        return;
    }

    log_info("Locking pointer...");

    output->locked_pointer = zwp_pointer_constraints_v1_lock_pointer(
        backend->zwp_pointer_constraints_v1,
        output->wl_surface,
        backend->pointer->wl_pointer,
        nullptr,
        ZWP_POINTER_CONSTRAINTS_V1_LIFETIME_PERSISTENT);
    zwp_locked_pointer_v1_add_listener(output->locked_pointer, &wroc_zwp_locked_pointer_v1_listener, output);
}

void wroc_wayland_backend::create_output()
{
    if (!wl_compositor) {
        log_error("No wl_compositor interface bound");
        return;
    }

    if (!xdg_wm_base) {
        log_error("No xdg_wm_base interface bound");
        return;
    }

    auto output = core_create<wroc_wayland_output>();

    auto id = next_window_id++;

    output->desc.physical_size_mm = {};
    output->desc.model = "Unknown";
    output->desc.make = "Unknown";
    output->desc.name = std::format("WL-{}", id);
    output->desc.description = std::format("Wayland output {}", id);

    outputs.emplace_back(output);

    output->wl_surface = wl_compositor_create_surface(wl_compositor);
    output->xdg_surface = xdg_wm_base_get_xdg_surface(xdg_wm_base, output->wl_surface);
    xdg_surface_add_listener(output->xdg_surface, &wroc_xdg_surface_listener, output.get());

    output->toplevel = xdg_surface_get_toplevel(output->xdg_surface);
    xdg_toplevel_add_listener(output->toplevel, &wroc_xdg_toplevel_listener, output.get());

    xdg_toplevel_set_app_id(output->toplevel, PROGRAM_NAME);
    xdg_toplevel_set_title(output->toplevel, output->desc.name.c_str());

    if (zxdg_decoration_manager_v1) {
        output->decoration = zxdg_decoration_manager_v1_get_toplevel_decoration(zxdg_decoration_manager_v1, output->toplevel);
        zxdg_toplevel_decoration_v1_add_listener(output->decoration, &wroc_zxdg_toplevel_decoration_v1_listener, output.get());
        zxdg_toplevel_decoration_v1_set_mode(output->decoration, ZXDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE);
    } else {
        log_warn("Server side decorations are not supported, backend outputs will remain undecorated");
    }

    wroc_wayland_backend_update_pointer_constraint(output.get());

    {
        auto region = wl_compositor_create_region(wl_compositor);
        wl_region_add(region, 0, 0, INT32_MAX, INT32_MAX);
        wl_surface_set_opaque_region(output->wl_surface, region);
        wl_region_destroy(region);
    }

    output->syncobj_surface = wp_linux_drm_syncobj_manager_v1_get_surface(wp_linux_drm_syncobj_manager_v1, output->wl_surface);

    wl_surface_commit(output->wl_surface);
}

static
wl_buffer* get_image_proxy(wroc_wayland_backend* backend, gpu_image* image)
{
    if (auto* found = backend->buffer_cache.find(image)) return found;

    auto size = image->extent();
    auto format = image->format();

    auto dma_params = gpu_image_export(image);
    u32 mod_hi = dma_params.modifier >> 32;
    u32 mod_lo = dma_params.modifier & ~0u;

    auto buffer_params = zwp_linux_dmabuf_v1_create_params(backend->zwp_linux_dmabuf_v1);
    for (u32 plane_idx = 0; plane_idx < dma_params.planes.count; ++plane_idx) {
        auto& plane = dma_params.planes[plane_idx];
        zwp_linux_buffer_params_v1_add(buffer_params, plane.fd.get(), plane_idx, plane.offset, plane.stride, mod_hi, mod_lo);
    }
    auto buffer = zwp_linux_buffer_params_v1_create_immed(buffer_params, size.x, size.y, format->drm, 0);
    zwp_linux_buffer_params_v1_destroy(buffer_params);

    return backend->buffer_cache.insert(image, buffer);
}

static
wp_linux_drm_syncobj_timeline_v1* get_semaphore_proxy(wroc_wayland_backend* backend, gpu_semaphore* semaphore)
{
    if (auto* found = backend->syncobj_cache.find(semaphore)) return found;

    auto fd = gpu_semaphore_export_syncobj(semaphore);
    auto syncobj = wp_linux_drm_syncobj_manager_v1_import_timeline(backend->wp_linux_drm_syncobj_manager_v1, fd);
    close(fd);

    return backend->syncobj_cache.insert(semaphore, syncobj);
}

static
void on_present_frame(void* data, wl_callback*, u32 time)
{
    auto* backend = static_cast<wroc_wayland_backend*>(server->backend.get());
    auto* output = static_cast<wroc_wayland_output*>(data);

    // `time` is low resolution and these events are sent with low latency,
    // so it's more accurate to just use the current clock time.
    auto timestamp = backend->current_dispatch_time;

    for (auto& feedback : output->pending_feedback) {
        wroc_post_event(wroc_output_event {
            .type = wroc_event_type::output_commit,
            .timestamp = timestamp,
            .output = output,
            .commit = {
                .id = feedback.commit_id,
                .start = feedback.commit_time,
            },
        });
    }
    output->pending_feedback.clear();

    wl_callback_destroy(output->frame_callback);
    output->frame_callback = nullptr;

    if (!output->frame_available) {
        output->frame_available = true;
        wroc_post_event(wroc_output_event{
            .type = wroc_event_type::output_frame,
            .output = output,
        });
    }
}

wroc_output_commit_id wroc_wayland_output::commit(
    gpu_image* image,
    gpu_syncpoint acquire,
    gpu_syncpoint release,
    flags<wroc_output_commit_flag> flags)
{
    core_assert(frame_available);

    auto* backend = static_cast<wroc_wayland_backend*>(server->backend.get());

    auto* wl_buffer = get_image_proxy(backend, image);
    auto* acquire_syncpoint = get_semaphore_proxy(backend, acquire.semaphore);
    auto* release_syncpoint = get_semaphore_proxy(backend, release.semaphore);

    wl_surface_attach(wl_surface, wl_buffer, 0, 0);
    wl_surface_damage_buffer(wl_surface, 0, 0, INT32_MAX, INT32_MAX);

    wp_linux_drm_syncobj_surface_v1_set_acquire_point(syncobj_surface, acquire_syncpoint, acquire.value >> 32, acquire.value & ~0u);
    wp_linux_drm_syncobj_surface_v1_set_release_point(syncobj_surface, release_syncpoint, release.value >> 32, release.value & ~0u);

    auto commit_id = ++last_commit_id;

    if (!frame_callback) {
        frame_callback = wl_surface_frame(wl_surface);
        constexpr static wl_callback_listener listener {
            .done = on_present_frame,
        };
        wl_callback_add_listener(frame_callback, &listener, this);
    }

    if (flags.contains(wroc_output_commit_flag::vsync)) {
        frame_available = false;
    } else {
        core_event_loop_enqueue(server->event_loop.get(), [output = weak(this)] {
            if (!output) return;
            wroc_post_event(wroc_output_event{
                .type = wroc_event_type::output_frame,
                .output = output.get(),
            });
        });
    }

    pending_feedback.emplace_back(wroc_wayland_commit_feedback {
        .commit_id = commit_id,
        .commit_time = std::chrono::steady_clock::now(),
    });

    wl_surface_commit(wl_surface);

    wl_display_flush(backend->wl_display);

    return commit_id;
}

wroc_wayland_output::~wroc_wayland_output()
{
    wp_linux_drm_syncobj_surface_v1_destroy(syncobj_surface);

    if (frame_callback) wl_callback_destroy(frame_callback);

    if (locked_pointer) zwp_locked_pointer_v1_destroy(locked_pointer);

    if (decoration)  zxdg_toplevel_decoration_v1_destroy(decoration);
    if (toplevel)    xdg_toplevel_destroy(toplevel);
    if (xdg_surface) xdg_surface_destroy(xdg_surface);
    if (wl_surface)  wl_surface_destroy(wl_surface);
}

void wroc_wayland_backend::destroy_output(wroc_output* output)
{
    wroc_post_event(wroc_output_event {
        .type = wroc_event_type::output_removed,
        .output = output,
    });

    std::erase_if(outputs, [&](auto& o) { return o.get() == output; });
}
