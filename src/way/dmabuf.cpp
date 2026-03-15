#include "internal.hpp"

static
void create_params(wl_client* client, wl_resource* resource, u32 params_id);

// -----------------------------------------------------------------------------

static
auto get_formats(way_server* server)
{
    // TODO: Intersect against io_output capabilities

    return gpu_get_formats()
        | std::views::transform([server](auto format) -> std::pair<gpu_format, std::span<const gpu_drm_modifier>> {
            auto props = gpu_get_format_props(server->gpu, format, gpu_image_usage::texture | gpu_image_usage::transfer_src);
            return { format, props->mods };
        });
}

void way_dmabuf_init(way_server* server)
{
    way_global(server, zwp_linux_dmabuf_v1);

    struct tranche_entry
    {
        u32 format;
        u32 padding;
        u64 modifier;
    };

    auto& feedback = server->dmabuf;

    // Enumerate formats

    std::vector<tranche_entry> entries;
    for (auto[format, modifiers] : get_formats(server)) {
        for (auto modifier : modifiers) {
            entries.emplace_back(tranche_entry {
                .format = format->drm,
                .modifier = modifier,
            });
        }
    }

    // Copy to shared memory

    usz size = entries.size() * sizeof(tranche_entry);

    auto fd = core::fd::adopt(core::check<memfd_create>(PROGRAM_NAME "-formats", MFD_ALLOW_SEALING | MFD_CLOEXEC).value);
    core::check<ftruncate>(fd.get(), size);

    auto mapped = core::check<mmap>(nullptr, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd.get(), 0).value;
    std::memcpy(mapped, entries.data(), size);
    munmap(mapped, size);

    // Seal file to prevent further writes
    core::check<fcntl>(fd.get(), F_ADD_SEALS, F_SEAL_WRITE | F_SEAL_SHRINK | F_SEAL_GROW);

    // Generate indices
    // TODO: Move LINEAR modifiers into lower tranche?

    feedback.format_table = fd;
    feedback.format_table_size = size;

    feedback.tranche_formats.resize(entries.size());
    for (u16 i = 0; i < entries.size(); ++i) {
        feedback.tranche_formats[i] = i;
    }
}

static
void send_feedback(way_server* server, wl_resource* resource)
{
    wl_array dev_id = {
        .size  = sizeof(server->gpu->drm.id),
        .alloc = sizeof(server->gpu->drm.id),
        .data  = &server->gpu->drm.id,
    };

    auto& feedback = server->dmabuf;

    way_send(server, zwp_linux_dmabuf_feedback_v1_send_main_device,  resource, &dev_id);
    way_send(server, zwp_linux_dmabuf_feedback_v1_send_format_table, resource, feedback.format_table.get(), feedback.format_table_size);

    way_send(server, zwp_linux_dmabuf_feedback_v1_send_tranche_target_device, resource, &dev_id);
    way_send(server, zwp_linux_dmabuf_feedback_v1_send_tranche_flags,   resource, 0);
    way_send(server, zwp_linux_dmabuf_feedback_v1_send_tranche_formats, resource, core::ptr_to(way_to_wl_array<u16>(feedback.tranche_formats)));
    way_send(server, zwp_linux_dmabuf_feedback_v1_send_tranche_done,    resource);

    way_send(server, zwp_linux_dmabuf_feedback_v1_send_done, resource);
}

static
void get_default_feedback(wl_client* client, wl_resource* resource, u32 id)
{
    send_feedback(way_get_userdata<way_server>(resource),
        way_resource_create_unsafe(zwp_linux_dmabuf_feedback_v1, client, resource, id, nullptr));
}

static
void get_surface_feedback(wl_client* client, wl_resource* resource, u32 id, wl_resource* surface)
{
    send_feedback(way_get_userdata<way_server>(resource),
        way_resource_create_unsafe(zwp_linux_dmabuf_feedback_v1, client, resource, id, nullptr));
}

WAY_INTERFACE(zwp_linux_dmabuf_feedback_v1) = {
    .destroy = way_simple_destroy,
};

WAY_INTERFACE(zwp_linux_dmabuf_v1) = {
    .destroy = way_simple_destroy,
    .create_params = create_params,
    .get_default_feedback = get_default_feedback,
    .get_surface_feedback = get_surface_feedback,
};

WAY_BIND_GLOBAL(zwp_linux_dmabuf_v1, bind)
{
    auto resource = way_resource_create_unsafe(zwp_linux_dmabuf_v1, bind.client, bind.version, bind.id, bind.server);

    if (bind.version >= ZWP_LINUX_DMABUF_V1_GET_DEFAULT_FEEDBACK_SINCE_VERSION) {
        return;
    }

    // Deprecated method of exposing formats, only use with legacy clients

    auto send_modifier = [&](u32 format, u64 modifier) {
        u32 modifier_hi =  modifier >> 32;
        u32 modifier_lo = modifier & 0xFFFF'FFFF;
        way_send(bind.server, zwp_linux_dmabuf_v1_send_modifier, resource, format, modifier_hi, modifier_lo);
    };

    for (auto[format, modifiers] : get_formats(bind.server)) {
        way_send(bind.server, zwp_linux_dmabuf_v1_send_format, resource, format->drm);

        for (auto modifier : modifiers) {
            send_modifier(format->drm, modifier);
        }
    }
}

// -----------------------------------------------------------------------------

struct way_dma_params
{
    way_server* server;

    way_resource resource;

    gpu_dma_params params;
    u32 planes_set;
};

static
void create_params(wl_client* client, wl_resource* resource, u32 params_id)
{
    auto params = core::create<way_dma_params>();
    params->server = way_get_userdata<way_server>(resource);
    params->resource = way_resource_create_refcounted(zwp_linux_buffer_params_v1, client, resource, params_id, params.get());
}

static
void params_add(wl_client* client, wl_resource* resource, int _fd, u32 plane_idx, u32 offset, u32 stride, u32 modifier_hi, u32 modifier_lo)
{
    auto fd = core::fd::adopt(_fd);

    auto* params = way_get_userdata<way_dma_params>(resource);
    auto* server = params->server;

    if (plane_idx >= gpu_dma_max_planes) {
        way_post_error(server, resource, ZWP_LINUX_BUFFER_PARAMS_V1_ERROR_PLANE_IDX, "Invalid plane index");
        return;
    }

    if (params->planes_set & (1 << plane_idx)) {
        way_post_error(server, resource, ZWP_LINUX_BUFFER_PARAMS_V1_ERROR_PLANE_SET, "Plane already set");
        return;
    }

    auto drm_modifier = u64(modifier_hi) << 32 | modifier_lo;

    if (!params->planes_set) {
        params->params.modifier = drm_modifier;
    } else if (params->params.modifier != drm_modifier) {
        way_post_error(server, resource, ZWP_LINUX_BUFFER_PARAMS_V1_ERROR_INVALID_FORMAT, "All planes must use the same DRM modifier");
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
        if (core::fd::are_same(p.fd.get(), fd.get())) {
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

struct way_dma_buffer : way_buffer
{
    way_server* server;
    way_resource resource;

    core::Ref<gpu_image> image;

    std::optional<gpu_dma_params> params;

    virtual auto acquire(way_surface*, way_surface_state& from) -> core::Ref<gpu_image> final override;
};

static
auto create_buffer(way_dma_params* dma_params, u32 buffer_id, vec2u32 extent, gpu_format format,
                   core::Flags<zwp_linux_buffer_params_v1_flags> flags) -> way_dma_buffer*
{
    auto* server = dma_params->server;

    if (!format) {
        way_post_error(server, dma_params->resource, ZWP_LINUX_BUFFER_PARAMS_V1_ERROR_INVALID_FORMAT, "Invalid format");
        return nullptr;
    }

    auto& params = dma_params->params;

    if (dma_params->planes_set == ~0u) {
        way_post_error(server, dma_params->resource, ZWP_LINUX_BUFFER_PARAMS_V1_ERROR_INCOMPLETE, "Attempted to use buffer params with previous errors");
        return nullptr;
    }
    if (!dma_params->planes_set || std::popcount(dma_params->planes_set + 1) != 1) {
        way_post_error(server, dma_params->resource, ZWP_LINUX_BUFFER_PARAMS_V1_ERROR_INCOMPLETE, "Incomplete plane set");
        return nullptr;
    }

    params.planes.count = std::popcount(dma_params->planes_set);

    auto buffer = core::create<way_dma_buffer>();
    buffer->server = server;
    buffer->resource = way_resource_create_refcounted(wl_buffer,
        wl_resource_get_client(dma_params->resource), dma_params->resource, buffer_id, buffer.get());
    buffer->extent = extent;

    // TODO: Handle flags

    params.format = format;
    params.extent = extent;

    log_debug("DMA-BUF {} - {} : {}",
        core::to_string(buffer->extent), format->name, gpu_drm_modifier_get_name(params.modifier));
    buffer->image = gpu_image_import(server->gpu, params, gpu_image_usage::texture);

    dma_params->params = {};
    dma_params->planes_set = {};

    return buffer.get();
}

static
void create_buffer(wl_client* client, wl_resource* _params, i32 width, i32 height, u32 format, u32 flags)
{
    auto* params = way_get_userdata<way_dma_params>(_params);
    auto* server = params->server;

    auto buffer = create_buffer(params, 0, {width, height}, gpu_format_from_drm(format), zwp_linux_buffer_params_v1_flags(flags));
    if (buffer) {
        way_send(server, zwp_linux_buffer_params_v1_send_created, _params, buffer->resource);
    } else {
        way_send(server, zwp_linux_buffer_params_v1_send_failed, _params);
    }
}

static
void create_buffer_immed(wl_client* client, wl_resource* _params, u32 buffer_id, i32 width, i32 height, u32 format, u32 flags)
{
    auto* params = way_get_userdata<way_dma_params>(_params);
    create_buffer(params, buffer_id, {width, height}, gpu_format_from_drm(format), zwp_linux_buffer_params_v1_flags(flags));
}

WAY_INTERFACE(zwp_linux_buffer_params_v1) = {
    .destroy      = way_simple_destroy,
    .add          = params_add,
    .create       = create_buffer,
    .create_immed = create_buffer_immed,
};

// -----------------------------------------------------------------------------

auto way_dma_buffer::acquire(way_surface* surface, way_surface_state& packet) -> core::Ref<gpu_image>
{
    if (!params) {
        params = gpu_image_export(image.get());
    }

    for (auto& plane : std::span(params->planes).subspan(0, params->disjoint ? std::dynamic_extent : 1)) {
        core::check<poll>(core::ptr_to(pollfd { .fd = plane.fd.get(), .events = POLLIN }), 1, -1);
    }

    return gpu_lease_image(image.get(), [buffer = core::Weak(this)](core::Ref<gpu_image>) {
        if (buffer) {
            way_send(buffer->server, wl_buffer_send_release, buffer->resource);
        }
    });
}
