#include "wroc.hpp"

#include "core/math.hpp"
#include "core/memory.hpp"

#include "protocol.hpp"

const u32 wroc_zwp_linux_dmabuf_v1_version = 5;

static
void wroc_dmabuf_create_params(wl_client* client, wl_resource* resource, u32 params_id)
{
    auto* new_resource = wroc_resource_create(client, &zwp_linux_buffer_params_v1_interface, wl_resource_get_version(resource), params_id);
    auto* params = core_create_unsafe<wroc_dma_buffer_params>();
    params->resource = new_resource;
    wroc_resource_set_implementation_refcounted(new_resource, &wroc_zwp_linux_buffer_params_v1_impl, params);
}

static
void wroc_dmabuf_send_tranches(wl_resource* feedback_resource);

static
void wroc_dmabuf_get_default_feedback(wl_client* client, wl_resource* resource, u32 id)
{
    auto* new_resource = wroc_resource_create(client, &zwp_linux_dmabuf_feedback_v1_interface, wl_resource_get_version(resource), id);
    wroc_resource_set_implementation(new_resource, &wroc_zwp_linux_dmabuf_feedback_v1_impl, nullptr);

    wroc_dmabuf_send_tranches(new_resource);
}

static
void wroc_dmabuf_get_surface_feedback(wl_client* client, wl_resource* resource, u32 id, wl_resource* surface)
{
    auto* new_resource = wroc_resource_create(client, &zwp_linux_dmabuf_feedback_v1_interface, wl_resource_get_version(resource), id);
    wroc_resource_set_implementation(new_resource, &wroc_zwp_linux_dmabuf_feedback_v1_impl, nullptr);

    // TODO: Surface optimized tranches?

    wroc_dmabuf_send_tranches(new_resource);
}

const struct zwp_linux_dmabuf_v1_interface wroc_zwp_linux_dmabuf_v1_impl = {
    .destroy              = wroc_simple_resource_destroy_callback,
    .create_params        = wroc_dmabuf_create_params,
    .get_default_feedback = wroc_dmabuf_get_default_feedback,
    .get_surface_feedback = wroc_dmabuf_get_surface_feedback,
};

static
void wroc_dmabuf_params_add(wl_client* client, wl_resource* resource, int _fd, u32 plane_idx, u32 offset, u32 stride, u32 modifier_hi, u32 modifier_lo)
{
    auto fd = core_fd_adopt(_fd);

    // TODO: We should enforce a limit on the number of open files a client can have to keep under 1024 for the whole process

    auto* params = wroc_get_userdata<wroc_dma_buffer_params>(resource);

    if (plane_idx >= gpu_dma_max_planes) {
        wroc_post_error(resource, ZWP_LINUX_BUFFER_PARAMS_V1_ERROR_PLANE_IDX, "Invalid plane index");
        return;
    }

    if (params->planes_set & (1 << plane_idx)) {
        wroc_post_error(resource, ZWP_LINUX_BUFFER_PARAMS_V1_ERROR_PLANE_SET, "Plane already set");
        return;
    }

    auto drm_modifier = u64(modifier_hi) << 32 | modifier_lo;

    if (!params->planes_set) {
        params->params.modifier = drm_modifier;
    } else if (params->params.modifier != drm_modifier) {
        wroc_post_error(resource, ZWP_LINUX_BUFFER_PARAMS_V1_ERROR_INVALID_FORMAT, "All planes must use the same DRM modifier");
        params->planes_set = ~0u;
        return;
    }

    params->planes_set |= (1 << plane_idx);

    auto& plane = params->params.planes[plane_idx];
    plane = gpu_dma_plane {
        .offset = offset,
        .stride = stride,
    };

    // Deduplicate file descriptors as we receive them

    for (auto& p : params->params.planes) {
        if (core_fd_are_same(p.fd.get(), fd.get())) {
            plane.fd = p.fd;
            break;
        } else {
            params->params.disjoint = true;
        }
    }
    if (!plane.fd) {
        plane.fd = std::move(fd);
    }
}

static
wroc_dma_buffer* wroc_dmabuf_create_buffer(wl_client* client, wl_resource* params_resource, u32 buffer_id, i32 width, i32 height, u32 drm_format, u32 flags)
{
    auto format = gpu_format_from_drm(drm_format);

    if (!format) {
        wroc_post_error(params_resource, ZWP_LINUX_BUFFER_PARAMS_V1_ERROR_INVALID_FORMAT, "Invalid format");
        return nullptr;
    }

    auto* dma_params = wroc_get_userdata<wroc_dma_buffer_params>(params_resource);
    auto& params = dma_params->params;

    if (dma_params->planes_set == ~0u) {
        wroc_post_error(params_resource, ZWP_LINUX_BUFFER_PARAMS_V1_ERROR_INCOMPLETE, "Attempted to use buffer params with previous errors");
        return nullptr;
    }
    if (!dma_params->planes_set || std::popcount(dma_params->planes_set + 1) != 1) {
        wroc_post_error(params_resource, ZWP_LINUX_BUFFER_PARAMS_V1_ERROR_INCOMPLETE, "Incomplete plane set");
        return nullptr;
    }

    params.planes.count = std::popcount(dma_params->planes_set);

    auto* new_resource = wroc_resource_create(client, &wl_buffer_interface, 1, buffer_id);
    auto* buffer = core_create_unsafe<wroc_dma_buffer>();
    buffer->resource = new_resource;
    buffer->type = wroc_buffer_type::dma;

    wroc_resource_set_implementation_refcounted(new_resource, &wroc_wl_buffer_impl, buffer);

    // TODO: Handle zwp_linux_buffer_params_v1_flags(flags)

    params.format = format;
    params.extent = {width, height};

    buffer->extent = {width, height};
    log_debug("Importing DMA-BUF, size = {}, format = {}, mod = {}",
        core_to_string(buffer->extent), format->name, gpu_get_modifier_name(params.modifier));
    buffer->image = gpu_image_import(server->gpu, params, gpu_image_usage::texture);

    dma_params->params = {};
    dma_params->planes_set = {};

    return buffer;
}

static
void wroc_dmabuf_params_create_buffer(wl_client* client, wl_resource* params, i32 width, i32 height, u32 format, u32 flags)
{
    auto buffer = wroc_dmabuf_create_buffer(client, params, 0, width, height, format, flags);
    if (buffer) {
        wroc_send(zwp_linux_buffer_params_v1_send_created, params, buffer->resource);
    } else {
        wroc_send(zwp_linux_buffer_params_v1_send_failed, params);
    }
}

static
void wroc_dmabuf_params_create_buffer_immed(wl_client* client, wl_resource* params, u32 buffer_id, i32 width, i32 height, u32 format, u32 flags) {
    wroc_dmabuf_create_buffer(client, params, buffer_id, width, height, format, flags);
}

const struct zwp_linux_buffer_params_v1_interface wroc_zwp_linux_buffer_params_v1_impl = {
    .destroy      = wroc_simple_resource_destroy_callback,
    .add          = wroc_dmabuf_params_add,
    .create       = wroc_dmabuf_params_create_buffer,
    .create_immed = wroc_dmabuf_params_create_buffer_immed,
};

const struct zwp_linux_dmabuf_feedback_v1_interface wroc_zwp_linux_dmabuf_feedback_v1_impl = {
    .destroy = wroc_simple_resource_destroy_callback,
};

bool wroc_dma_buffer::is_ready(wroc_surface* surface)
{
    if (!needs_wait) return true;

    bool ready = true;
    if (acquire_timeline) {
        ready = gpu_semaphore_get_value(acquire_timeline.get()) >= acquire_point;
        if (!ready) {
            surface->apply_queued = true;
            gpu_wait({acquire_timeline.get(), acquire_point}, [surface = weak(surface)](u64)  {
                if (surface) {
                    surface->apply_queued = false;
                    wroc_surface_flush_apply(surface.get());
                }
            });
        }
    } else {
        if (!params) {
            log_debug("First use of implicit sync with imported image, re-exporting DMA-BUF fds");
            params = gpu_image_export(image.get());
        }

        for (auto& plane : std::span(params->planes).subspan(0, params->disjoint ? std::dynamic_extent : 1)) {
            if (unix_check<poll>(ptr_to(pollfd { .fd = plane.fd.get(), .events = POLLIN }), 1, 0).value > 0) {
                continue;
            }

            ready = false;

            if (core_fd_get_listener(plane.fd.get())) {
                log_error("Can't register new DMA-BUF implicit listener - file descriptor already has listener registered");
            } else {
                surface->apply_queued = true;
                core_fd_add_listener(plane.fd.get(), server->event_loop.get(), core_fd_event_bit::readable,
                    [surface = weak(surface)](int, flags<core_fd_event_bit>) {
                        if (surface) {
                            surface->apply_queued = false;
                            wroc_surface_flush_apply(surface.get());
                        }
                    }, core_fd_listen_flag::oneshot);
            }

            break;
        }
    }

    if (ready) needs_wait = false;
    return ready;
}

void wroc_dma_buffer::on_commit(wroc_surface* surface)
{
    auto* syncobj_surface = wroc_surface_get_addon<wroc_syncobj_surface>(surface);

    acquire_timeline = nullptr;
    release_timeline = nullptr;

    if (syncobj_surface) {
        if (syncobj_surface->release_timeline) {
            release_timeline = std::exchange(syncobj_surface->release_timeline, nullptr);
            release_point = syncobj_surface->release_point;
        }

        if (syncobj_surface->acquire_timeline) {
            acquire_timeline = std::exchange(syncobj_surface->acquire_timeline, nullptr);
            acquire_point = syncobj_surface->acquire_point;
        }
    }

    needs_wait = true;

    if (!syncobj_surface) {
    } else if (!acquire_timeline && !release_timeline) {
        log_error("Surface has syncobj_surface attached, but did not submit any acquire or release points");
    } else if (!acquire_timeline) {
        wroc_post_error(resource, WP_LINUX_DRM_SYNCOBJ_SURFACE_V1_ERROR_NO_ACQUIRE_POINT, "Missing acquire point");
    } else if (!release_timeline) {
        wroc_post_error(resource, WP_LINUX_DRM_SYNCOBJ_SURFACE_V1_ERROR_NO_RELEASE_POINT, "Missing release point");
    } else if (acquire_timeline.get() == release_timeline.get()
            && acquire_point >= release_point) {
        wroc_post_error(resource, WP_LINUX_DRM_SYNCOBJ_SURFACE_V1_ERROR_CONFLICTING_POINTS, "Acquire and release use the same syncobj, but acquire point is not less than release point");
    }

#if 0

    // TODO: We should still include these barriers for correctness, even if the driver works
    //       fine without them (or at least seems too).

    gpu->vk.CmdPipelineBarrier2(commands->buffer, ptr_to(VkDependencyInfo {
        .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
        .imageMemoryBarrierCount = 1,
        .pImageMemoryBarriers = ptr_to(VkImageMemoryBarrier2 {
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
            .srcStageMask = VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT,
            .srcAccessMask = 0,
            // .dstStageMask = VK_PIPELINE_STAGE_2_COPY_BIT,
            // .dstAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT,
            .dstStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
            .dstAccessMask = VK_ACCESS_2_MEMORY_READ_BIT,
            .oldLayout = VK_IMAGE_LAYOUT_GENERAL,
            .newLayout = VK_IMAGE_LAYOUT_GENERAL,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_FOREIGN_EXT,
            .dstQueueFamilyIndex = queue->family,
            .image = dmabuf_image->image,
            .subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 },
        }),
    }));

    gpu->vk.CmdPipelineBarrier2(commands->buffer, ptr_to(VkDependencyInfo {
        .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
        .imageMemoryBarrierCount = 1,
        .pImageMemoryBarriers = ptr_to(VkImageMemoryBarrier2 {
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
            .srcStageMask = VK_PIPELINE_STAGE_2_COPY_BIT,
            .srcAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT,
            .dstStageMask = VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT,
            .dstAccessMask = 0,
            .oldLayout = VK_IMAGE_LAYOUT_GENERAL,
            .newLayout = VK_IMAGE_LAYOUT_GENERAL,
            .srcQueueFamilyIndex = queue->family,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_FOREIGN_EXT,
            .image = dmabuf_image->image,
            .subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 },
        }),
    }));
#endif
}

void wroc_dma_buffer::on_unlock()
{
    release();
    if (release_timeline) {
        gpu_semaphore_signal_value(release_timeline.get(), release_point);

        release_timeline = nullptr;
    }
}

void wroc_renderer_init_buffer_feedback(wroc_renderer* renderer)
{
    struct tranche_entry
    {
        u32 format;
        u32 padding;
        u64 modifier;
    };

    auto& feedback = renderer->buffer_feedback;

    // Enumerate formats

    std::vector<tranche_entry> entries;
    for (auto[format, modifiers] : renderer->dmabuf_formats) {
        for (auto modifier : modifiers) {
            entries.emplace_back(tranche_entry {
                .format = format->drm,
                .modifier = modifier,
            });
        }
    }

    // Copy to shared memory

    usz size = entries.size() * sizeof(tranche_entry);
    int rw, ro;
    if (!core_allocate_shm_file_pair(size, &rw, &ro)) {
        log_error("Failed to allocate shared memory pair");
        core_debugkill();
    }

    void* dst = unix_check<mmap>(nullptr, size, PROT_READ | PROT_WRITE, MAP_SHARED, rw, 0).value;
    close(rw);
    if (!dst) {
        log_error("mmap failed");
        close(ro);
        return;
    }

    std::memcpy(dst, entries.data(), size);

    // Generate indices
    // TODO: Move LINEAR modifiers into lower tranche?

    feedback.format_table = ro;
    feedback.format_table_size = size;

    feedback.tranche_formats.resize(entries.size());
    for (u16 i = 0; i < entries.size(); ++i) {
        feedback.tranche_formats[i] = i;
    }
}

static
void wroc_dmabuf_send_tranches(wl_resource* resource)
{
    wl_array dev_id = {
        .size  = sizeof(server->gpu->drm.id),
        .alloc = sizeof(server->gpu->drm.id),
        .data  = &server->gpu->drm.id,
    };

    auto& feedback = server->renderer->buffer_feedback;

    wroc_send(zwp_linux_dmabuf_feedback_v1_send_main_device, resource, &dev_id);
    wroc_send(zwp_linux_dmabuf_feedback_v1_send_format_table, resource, feedback.format_table, feedback.format_table_size);

    wroc_send(zwp_linux_dmabuf_feedback_v1_send_tranche_target_device, resource, &dev_id);
    wroc_send(zwp_linux_dmabuf_feedback_v1_send_tranche_flags, resource, 0);
    wroc_send(zwp_linux_dmabuf_feedback_v1_send_tranche_formats, resource, ptr_to(wroc_to_wl_array<u16>(feedback.tranche_formats)));
    wroc_send(zwp_linux_dmabuf_feedback_v1_send_tranche_done, resource);

    wroc_send(zwp_linux_dmabuf_feedback_v1_send_done, resource);
}

void wroc_zwp_linux_dmabuf_v1_bind_global(wl_client* client, void* data, u32 version, u32 id)
{
    auto* new_resource = wroc_resource_create(client, &zwp_linux_dmabuf_v1_interface, version, id);
    wroc_resource_set_implementation(new_resource, &wroc_zwp_linux_dmabuf_v1_impl, nullptr);

    if (version >= ZWP_LINUX_DMABUF_V1_GET_DEFAULT_FEEDBACK_SINCE_VERSION) {
        return;
    }

    // Deprecated method of exposing formats, only use with legacy clients

    auto send_modifier = [&](u32 format, u64 modifier) {
        u32 modifier_hi =  modifier >> 32;
        u32 modifier_lo = modifier & 0xFFFF'FFFF;
        wroc_send(zwp_linux_dmabuf_v1_send_modifier, new_resource, format, modifier_hi, modifier_lo);
    };

    for (auto[format, modifiers] : server->renderer->dmabuf_formats) {
        wroc_send(zwp_linux_dmabuf_v1_send_format, new_resource, format->drm);

        for (auto modifier : modifiers) {
            send_modifier(format->drm, modifier);
        }
    }
};
