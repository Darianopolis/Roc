#include "buffer.hpp"

#include "../surface/surface.hpp"

#include <core/process.hpp>

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

    Ref<GpuBuffer> imported;

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

#define WAY_NOISY_SHM_POOL_IMPORT 0

static
auto try_prepare_import(fd_t fd, i32 initial_size) -> i32
{
    auto seals = unix_check<fcntl>(fd, F_GET_SEALS);
    if (seals.err()) {
        log_error("wl_shm_pool :: file does not support sealing, falling back to staged transfers ({})", FmtBytes{num_cast<usz>(initial_size)});
        return 0;
    }

    // Ensure SHRINK seal if possible
    if (!(seals.value & F_SEAL_SHRINK)) {
        if (seals.value & F_SEAL_SEAL || unix_check<fcntl>(fd, F_ADD_SEALS, F_SEAL_SHRINK).err()) {
            log_error("wl_shm_pool :: file cannot be sealed with SHRINK, falling back to staged transfers ({})", FmtBytes{num_cast<usz>(initial_size)});
            return 0;
        }
    }

    // Compute page-aligned size
    auto pagesize = process_get_pagesize();
    i32 aligned_size = align_up_power2(initial_size, pagesize);

    // Memfd already page aligned, trivial to map whole buffer
    if (aligned_size == initial_size) {
#if WAY_NOISY_SHM_POOL_IMPORT
        log_debug("wl_shm_pool :: file already meets page-size alignment ({})", FmtBytes{num_cast<usz>(initial_size)});
#endif
        return initial_size;
    }

    if (!(seals.value & F_SEAL_GROW) && unix_check<ftruncate>(fd, aligned_size).ok()) {
        // Memfd can grow, expand to aligned size
#if WAY_NOISY_SHM_POOL_IMPORT
        log_debug("wl_shm_pool :: file expanded by {:4} bytes to meet page-size alignment ({})", aligned_size - initial_size, FmtBytes{num_cast<usz>(initial_size)});
#endif
        return aligned_size;
    }

    // Memfd cannot grow, map up to last full page
    i32 mappable = aligned_size - num_cast<i32>(pagesize);
#if WAY_NOISY_SHM_POOL_IMPORT
    log_warn("wl_shm_pool :: file size is frozen, import truncated by {:4} bytes ({})", initial_size - mappable, FmtBytes{num_cast<usz>(initial_size)});
#endif
    return mappable;
}

static
void create_pool(wl_client* client, wl_resource* resource, u32 id, fd_t fd, i32 size)
{
    i32 importable_size = try_prepare_import(fd, size);

    auto pool = ref_create<WayShmPool>();
    pool->server = way_get_userdata<WayServer>(resource);
    pool->fd = Fd(fd);
    pool->resource = way_resource_create_refcounted(wl_shm_pool, client, resource, id, pool.get());
    pool_map(pool.get(), num_cast<usz>(size));

    if (importable_size) {
        pool->imported = gpu_buffer_import_from_memfd(pool->server->gpu, num_cast<usz>(importable_size), GpuBufferFlag::host, fd, 0);
    }
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
            GpuImageUsage::sampled | GpuImageUsage::transfer_src | GpuImageUsage::transfer_dst);
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
    if (shm_buffer == buffer) log_info( "REUSING shm buffer image {}",  candidate->base()->extent);
    else                      log_debug("STEALING shm buffer image {}", candidate->base()->extent);
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
            .usage = GpuImageUsage::transfer_dst | GpuImageUsage::sampled,
        });

        damage.damage({{}, vec_cast<i32>(extent), minmax});

#if NOISY_SHM_BUFFER_IMAGES
        log_warn("ALLOCATING shm buffer image {}", extent);
#endif
    }

    damage.clip_to({{}, vec_cast<i32>(extent), minmax});

    bool immediate_release = true;

    if (damage) {
        rect2i32 rect = damage.bounds();
#if NOISY_SHM_BUFFER_IMAGES
        log_trace("  damage {}", rect);
#endif

        // Compute transfer source properties
        auto read_start = gpu_image_compute_linear_offset(format, vec_cast<u32>(rect.origin), stride);
        auto read_end   = gpu_image_compute_linear_offset(format, vec_cast<u32>(rect.origin + rect.extent - 1), stride) + format->texel_block_size;

#if NOISY_SHM_BUFFER_IMAGES
        auto total_copy_size = u32(rect.extent.x * rect.extent.y) * format->texel_block_size;
#endif

        // Attempt to copy as much directly from imported buffer
        if (pool->imported && stride % format->texel_block_size == 0 && offset + read_start < pool->imported->size) {

            auto fast_read_end = read_end;
            rect2i32 fast_rect = rect;

            if (offset + fast_read_end > pool->imported->size) {
                auto remainder = offset + fast_read_end - pool->imported->size;
                auto rows = std::min((remainder + stride - 1) / stride, num_cast<usz>(fast_rect.extent.y));
                fast_read_end -= num_cast<u32>(rows * stride);
                fast_rect.extent.y -= num_cast<i32>(rows);
            }

            if (fast_rect.extent.y) {
                gpu_copy_buffer_to_image(image.get(), pool->imported.get(), {{
                    {
                        .image_extent = vec_cast<u32>(fast_rect.extent),
                        .image_offset = fast_rect.origin,
                        .buffer_offset = num_cast<u32>(offset + read_start),
                        .buffer_row_length = stride / format->texel_block_size,
                    }
                }});

#if NOISY_SHM_BUFFER_IMAGES
                log_debug("Performing direct copy of {}", FmtBytes{u32(fast_rect.extent.x * fast_rect.extent.y) * format->texel_block_size});
#endif

                read_start = fast_read_end;
                rect.origin.y += fast_rect.extent.y;
                rect.extent.y -= fast_rect.extent.y;

                struct BufferGuard
                {
                    Weak<WayShmBuffer> buffer;
                    WayTimelinePoint release_point;

                    ~BufferGuard()
                    {
                        if (!buffer) return;
                        buffer->release(std::move(release_point));
                    }
                };

                gpu_protect(gpu_record(image->base()->gpu), ref_create<BufferGuard>(this, release_point));
                immediate_release = false;
            }
        }

        // Perform remaining copy based upload
        if (read_end > read_start) {

            // Compute transfer staging properties
            debug_assert(format->texels_per_block == 1, "TODO");
            auto copy_stride = format->texel_block_size * num_cast<u32>(rect.extent.x);
            auto copy_size = copy_stride * num_cast<u32>(rect.extent.y);

#if NOISY_SHM_BUFFER_IMAGES
            log_warn("Performing staged copy of {} remaining bytes ({:.2f}%)", FmtBytes{copy_size}, 100.0 * f64(copy_size) / total_copy_size);
#endif

            // Reserve transfer region
            auto* gpu = image->base()->gpu;
            auto cmd = gpu_record(gpu);
            auto transfer_offset = gpu_reserve_transfer(cmd, copy_size, 16);
            auto out = gpu->transfer.buffer->host<std::byte>(transfer_offset);

            // Compute transfer source properties
            auto in = byte_offset_pointer<std::byte>(pool->data, offset + read_start);

            if (copy_stride == stride) {
                // Source rows are contiguous in memory, perform single copy
                std::memcpy(out, in, read_end - read_start);
            } else {
                // Source rows are discontiguous (copy does not span full width), copy by row
                for (u32 i = 0; i < num_cast<u32>(rect.extent.y); ++i) {
                    std::memcpy(out + i * copy_stride, in + i * stride, copy_stride);
                }
            }

            gpu_copy_buffer_to_image(image.get(), gpu->transfer.buffer.get(), {{
                {
                    .image_extent = vec_cast<u32>(rect.extent),
                    .image_offset = rect.origin,
                    .buffer_offset = num_cast<u32>(transfer_offset),
                }
            }});
        }
    }
#if NOISY_SHM_BUFFER_IMAGES
    else {
        log_warn("  damage was empty");
    }
#endif

    if (immediate_release) {
        release(std::move(release_point));
    }

    return image;
}
