#include "buffer.hpp"

#include "../surface/surface.hpp"

static
auto from_drm(GpuDrmFormat drm) -> wl_shm_format
{
    switch (drm) {
        break;case DRM_FORMAT_XRGB8888: return WL_SHM_FORMAT_XRGB8888;
        break;case DRM_FORMAT_ARGB8888: return WL_SHM_FORMAT_ARGB8888;
        break;default:                  return wl_shm_format(drm);
    }
}

static
auto to_drm(wl_shm_format shm) -> GpuDrmFormat
{
    switch (shm) {
        break;case WL_SHM_FORMAT_XRGB8888: return DRM_FORMAT_XRGB8888;
        break;case WL_SHM_FORMAT_ARGB8888: return DRM_FORMAT_ARGB8888;
        break;default:                     return GpuDrmFormat(shm);
    }
}


// -----------------------------------------------------------------------------

struct WayShmPool
{
    WayServer* server;

    WayResource resource;

    Fd    fd;
    void* data;
    usz   size;

    ~WayShmPool();
};

static
void pool_unmap(WayShmPool* pool)
{
    if (pool->data) {
        unix_check<munmap>(pool->data, pool->size);
        pool->data = nullptr;
        pool->size = 0;
    }
}

static
void pool_map(WayShmPool* pool, usz size)
{
    pool_unmap(pool);

    auto res = unix_check<mmap>(nullptr, size, PROT_READ | PROT_WRITE, MAP_SHARED, pool->fd.get(), 0);
    if (res.ok()) {
        pool->data = res.value;
        pool->size = size;
    } else {
        way_post_error(pool->resource, WL_SHM_ERROR_INVALID_FD, "mmap failed: {}", strerror(res.error));
    }
}

static
void create_pool(wl_client* client, wl_resource* resource, u32 id, fd_t fd, i32 size)
{
    auto pool = ref_create<WayShmPool>();
    pool->server = way_get_userdata<WayServer>(resource);
    pool->fd = Fd(fd);
    pool->resource = way_resource_create_refcounted(wl_shm_pool, client, resource, id, pool.get());
    pool_map(pool.get(), num_cast<usz>(size));
}

WAY_INTERFACE(wl_shm) = {
    .create_pool = create_pool,
    .release = way_simple_destroy,
};

// -----------------------------------------------------------------------------

WAY_BIND_GLOBAL(wl_shm, bind)
{
    auto resource = way_resource_create_unsafe(wl_shm, bind.client, bind.version, bind.id, bind.server);

    for (auto format : gpu_get_formats()) {
        auto props = gpu_get_format_properties(bind.server->gpu, format,
            GpuImageUsage::texture | GpuImageUsage::transfer_src | GpuImageUsage::transfer_dst);
        if (props->for_mod(DRM_FORMAT_MOD_LINEAR)) {
            way_send<wl_shm_send_format>(resource, from_drm(format->drm));
        }
    }
}

// -----------------------------------------------------------------------------

struct WayShmBuffer : WayBuffer
{
    Ref<WayShmPool> pool;

    u32 offset;
    u32 stride;
    GpuFormat format;

    virtual auto do_acquire(WaySurface*, WayDamageRegion, Flags<WayBufferAcquireFlags>, WayTimelinePoint release_point) -> Ref<GpuImage> final override;
};

static
void create_buffer(wl_client* client, wl_resource* resource, u32 id, i32 offset, i32 width, i32 height, i32 stride, u32 _format)
{
    auto* pool = way_get_userdata<WayShmPool>(resource);

    auto buffer = ref_create<WayShmBuffer>();
    buffer->_resource = way_resource_create_refcounted(wl_buffer, client, resource, id, static_cast<WayBuffer*>(buffer.get()));

    buffer->format = gpu_format_from_drm(to_drm(wl_shm_format(_format)));
    buffer->extent = {num_cast<u32>(width), num_cast<u32>(height)};
    buffer->pool = pool;
    buffer->stride = num_cast<u32>(stride);
    buffer->offset = num_cast<u32>(offset);

    if (!buffer->format) {
        way_post_error(resource, WL_SHM_ERROR_INVALID_FORMAT, "Format {} is not supported", wl_shm_format(_format));
        return;
    }
}

static
void pool_resize(wl_client* client, wl_resource* resource, i32 size)
{
    auto* pool = way_get_userdata<WayShmPool>(resource);
    pool_map(pool, num_cast<usz>(size));
}

WAY_INTERFACE(wl_shm_pool) = {
    .create_buffer = create_buffer,
    .destroy = way_simple_destroy,
    .resize = pool_resize,
};

WayShmPool::~WayShmPool()
{
    pool_unmap(this);
}

// -----------------------------------------------------------------------------

#define NOISY_SHM_BUFFER_IMAGES 0

static
auto try_steal(WayShmBuffer* buffer, WaySurface* surface) -> GpuImage*
{
    // If the last attached buffer is a wl_shm buffer...
    auto* shm_buffer = dynamic_cast<WayShmBuffer*>(surface->current.buffer.get());
    if (!shm_buffer) return nullptr;

    // ...try to steal the previously acquired image...
    auto* candidate = surface->current.image.get();

    // ...if compatible with the newly attached buffer
    if (candidate->base()->extent != buffer->extent) return nullptr;
    if (candidate->base()->format != buffer->format) return nullptr;

#if NOISY_SHM_BUFFER_IMAGES
    if (shm_buffer == buffer) log_info( "REUSING shm buffer image {}",  candidate->extent());
    else                      log_debug("STEALING shm buffer image {}", candidate->extent());
#endif

    return candidate;
}

auto WayShmBuffer::do_acquire(WaySurface* surface, WayDamageRegion damage, Flags<WayBufferAcquireFlags>, WayTimelinePoint release_point) -> Ref<GpuImage>
{
    Ref image = try_steal(this, surface);

    auto* server = pool->server;

    if (!image) {
        image = gpu_image_create(server->gpu, {
            .extent = extent,
            .format = format,
            .usage = GpuImageUsage::transfer_dst | GpuImageUsage::texture,
        });

        damage.damage({{}, vec_cast<i32>(extent), minmax});

#if NOISY_SHM_BUFFER_IMAGES
        log_warn("ALLOCATING shm buffer image {}", extent);
#endif
    }

    damage.clip_to({{}, vec_cast<i32>(extent), minmax});

    if (damage) {
        aabb2i32 aabb = damage.bounds();
        rect2i32 rect = aabb;
#if NOISY_SHM_BUFFER_IMAGES
        log_trace("  damage {}", rect);
#endif

        auto read_start = gpu_image_compute_linear_offset(format, vec_cast<u32>(aabb.min),     stride);
        auto read_end   = gpu_image_compute_linear_offset(format, vec_cast<u32>(aabb.max - 1), stride) + format->texel_block_size;

        debug_assert((offset + read_end) <= pool->size, "accessed {} > available {}", offset + read_end, pool->size);
        gpu_copy_memory_to_image(image.get(),
            as_bytes(byte_offset_pointer<void>(pool->data, offset + read_start), read_end - read_start),
            {{{
                .image_extent = vec_cast<u32>(rect.extent),
                .image_offset = rect.origin,
                .buffer_row_length = num_cast<u32>(stride) / format->texel_block_size,
            }}});
    }
#if NOISY_SHM_BUFFER_IMAGES
    else {
        log_warn("  damage was empty");
    }
#endif

    release(std::move(release_point));

    return image;
}
