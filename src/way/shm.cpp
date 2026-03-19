#include "internal.hpp"

static
void pool_unmap(way_shm_pool* pool)
{
    if (pool->data) {
        unix_check<munmap>(pool->data, pool->size);
        pool->data = nullptr;
        pool->size = 0;
    }
}

static
void pool_map(way_shm_pool* pool, usz size)
{
    pool_unmap(pool);

    auto res = unix_check<mmap>(nullptr, size, PROT_READ | PROT_WRITE, MAP_SHARED, pool->fd.get(), 0);
    if (res.ok()) {
        pool->data = res.value;
        pool->size = size;
    } else {
        way_post_error(pool->server, pool->resource, WL_SHM_ERROR_INVALID_FD, "mmap failed: {}", strerror(res.error));
    }
}

static
void create_pool(wl_client* client, wl_resource* resource, u32 id, int fd, i32 size)
{
    auto pool = core_create<way_shm_pool>();
    pool->server = way_get_userdata<way_server>(resource);
    pool->fd = core_fd_adopt(fd);
    pool->resource = way_resource_create_refcounted(wl_shm_pool, client, resource, id, pool.get());
    pool_map(pool.get(), size);
}

WAY_INTERFACE(wl_shm) = {
    .create_pool = create_pool,
    .release = way_simple_destroy,
};

WAY_BIND_GLOBAL(wl_shm, bind)
{
    auto resource = way_resource_create_unsafe(wl_shm, bind.client, bind.version, bind.id, bind.server);

    way_send(bind.server, wl_shm_send_format, resource, WL_SHM_FORMAT_ARGB8888);
    way_send(bind.server, wl_shm_send_format, resource, WL_SHM_FORMAT_XRGB8888);
}

// -----------------------------------------------------------------------------

inline
wl_shm_format from_drm(gpu_drm_format drm)
{
    switch (drm) {
        break;case DRM_FORMAT_XRGB8888: return WL_SHM_FORMAT_XRGB8888;
        break;case DRM_FORMAT_ARGB8888: return WL_SHM_FORMAT_ARGB8888;
        break;default:                  return wl_shm_format(drm);
    }
}

inline
gpu_drm_format to_drm(wl_shm_format shm)
{
    switch (shm) {
        break;case WL_SHM_FORMAT_XRGB8888: return DRM_FORMAT_XRGB8888;
        break;case WL_SHM_FORMAT_ARGB8888: return DRM_FORMAT_ARGB8888;
        break;default:                     return gpu_drm_format(shm);
    }
}

// -----------------------------------------------------------------------------

struct way_shm_buffer : way_buffer
{
    ref<way_shm_pool> pool;

    way_resource resource;

    i32 offset;
    i32 stride;
    gpu_format format;

    virtual auto acquire(way_surface*, way_surface_state& from) -> ref<gpu_image> final override;
};

static
void create_buffer(wl_client* client, wl_resource* resource, u32 id, i32 offset, i32 width, i32 height, i32 stride, u32 _format)
{
    auto* pool = way_get_userdata<way_shm_pool>(resource);
    auto* server = pool->server;

    auto buffer = core_create<way_shm_buffer>();
    buffer->resource = way_resource_create_refcounted(wl_buffer, client, resource, id, buffer.get());

    buffer->format = gpu_format_from_drm(to_drm(wl_shm_format(_format)));
    buffer->extent = {width, height};
    buffer->pool = pool;
    buffer->stride = stride;
    buffer->offset = offset;

    if (!buffer->format) {
        way_post_error(server, resource, WL_SHM_ERROR_INVALID_FORMAT, "Format {} is not supported", core_to_string(wl_shm_format(_format)));
        return;
    }
}

static
void pool_resize(wl_client* client, wl_resource* resource, i32 size)
{
    auto* pool = way_get_userdata<way_shm_pool>(resource);
    pool_map(pool, size);
}

WAY_INTERFACE(wl_shm_pool) = {
    .create_buffer = create_buffer,
    .destroy = way_simple_destroy,
    .resize = pool_resize,
};

way_shm_pool::~way_shm_pool()
{
    pool_unmap(this);
}

// -----------------------------------------------------------------------------

#define NOISY_SHM_BUFFER_IMAGES 1

static
auto try_steal(way_shm_buffer* buffer, way_surface* surface) -> gpu_image*
{
    // If the last attached buffer is a wl_shm buffer...
    auto* shm_buffer = dynamic_cast<way_shm_buffer*>(surface->current.buffer.get());
    if (!shm_buffer) return nullptr;

    // ...try to steal the previously acquired image...
    auto* candidate = surface->current.image.get();

    // ...if compatible with the newly attached buffer
    if (candidate->extent() != buffer->extent) return nullptr;
    if (candidate->format() != buffer->format) return nullptr;

#if NOISY_SHM_BUFFER_IMAGES
    if (shm_buffer == buffer) log_info( "REUSING shm buffer image {}",  core_to_string(candidate->extent()));
    else                      log_debug("STEALING shm buffer image {}", core_to_string(candidate->extent()));
#endif

    return candidate;
}

auto way_shm_buffer::acquire(way_surface* surface, way_surface_state& packet) -> ref<gpu_image>
{
    ref image = try_steal(this, surface);

    auto damage = packet.buffer_damage;

    auto* server = pool->server;

    if (!image) {
        image = gpu_image_create(server->gpu, {
            .extent = extent,
            .format = format,
            .usage = gpu_image_usage::transfer_dst | gpu_image_usage::texture,
        });

        damage.damage({{}, extent, core_minmax});

#if NOISY_SHM_BUFFER_IMAGES
        log_warn("ALLOCATING shm buffer image {}", core_to_string(extent));
#endif
    }

    damage.clip_to({{}, extent, core_minmax});

    if (damage) {
        aabb2i32 aabb = damage.bounds();
        rect2i32 rect = aabb;
#if NOISY_SHM_BUFFER_IMAGES
        log_trace("  damage {}", core_to_string(rect));
#endif

        auto* gpu = server->gpu;

        auto& info = format->info;
        auto read_start = gpu_image_compute_linear_offset(format, aabb.min,     stride);
        auto read_end   = gpu_image_compute_linear_offset(format, aabb.max - 1, stride) + info.texel_block_size;

        auto size = read_end - read_start;
        auto staging = gpu_buffer_create(gpu, size, gpu_buffer_flag::host);
        core_assert((offset + read_end) <= pool->size, "accessed {} > available {}", offset + read_end, pool->size);
        std::memcpy(staging->host_address, core_byte_offset_pointer<void>(pool->data, offset + read_start), size);

        gpu_copy_buffer_to_image(image.get(), staging.get(),
            {{
                .image_extent = rect.extent,
                .image_offset = rect.origin,
                .buffer_row_length = u32(stride) / info.texel_block_size,
            }});
    }
#if NOISY_SHM_BUFFER_IMAGES
    else {
        log_warn("  damage was empty");
    }
#endif

    way_send(server, wl_buffer_send_release, resource);

    return image;
}
