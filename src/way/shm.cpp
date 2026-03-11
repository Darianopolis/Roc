#include "internal.hpp"

way_shm_mapping::~way_shm_mapping()
{
    munmap(data, size);
}

static
void update_mapping(way_shm_pool* pool, usz size)
{
    auto mapping = core_create<way_shm_mapping>();
    mapping->size = size;
    mapping->data = mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_SHARED, pool->fd.get(), 0);
    if (mapping->data == MAP_FAILED) {
        way_post_error(pool->server, pool->resource, WL_SHM_ERROR_INVALID_FD, "mmap failed");
        return;
    }

    pool->mapping = mapping;
}

static
void create_pool(wl_client* client, wl_resource* resource, u32 id, int fd, i32 size)
{
    auto pool = core_create<way_shm_pool>();
    pool->server = way_get_userdata<way_server>(resource);
    pool->fd = core_fd_adopt(fd);
    pool->resource = way_resource_create_refcounted(wl_shm_pool, client, resource, id, pool.get());
    update_mapping(pool.get(), size);
}

WAY_INTERFACE(wl_shm) = {
    .create_pool = create_pool,
    .release = way_simple_destroy,
};

WAY_BIND_GLOBAL(wl_shm)
{
    auto* server = way_get_userdata<way_server>(data);

    auto resource = way_resource_create_unsafe(wl_shm, client, version, id, server);

    way_send(server, wl_shm_send_format, resource, WL_SHM_FORMAT_ARGB8888);
    way_send(server, wl_shm_send_format, resource, WL_SHM_FORMAT_XRGB8888);
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

static
void create_buffer(wl_client* client, wl_resource* resource, u32 id, i32 offset, i32 width, i32 height, i32 stride, u32 _format)
{
    auto* pool = way_get_userdata<way_shm_pool>(resource);
    auto* server = pool->server;

    auto buffer = core_create<way_shm_buffer>();
    buffer->server = server;
    buffer->resource = way_resource_create_refcounted(wl_buffer, client, resource, id, buffer.get());

    buffer->format = gpu_format_from_drm(to_drm(wl_shm_format(_format)));
    buffer->extent = {width, height};
    buffer->pool = pool;
    buffer->stride = stride;
    buffer->offset = offset;

    if (!buffer->format) {
        way_post_error(server, resource, WL_SHM_ERROR_INVALID_FORMAT, "Format {} is not supported", core_enum_to_string(wl_shm_format(_format)));
        return;
    }

    buffer->image = gpu_image_create(server->gpu, {
        .extent = buffer->extent,
        .format = buffer->format,
        .usage = gpu_image_usage::texture | gpu_image_usage::transfer
    });
}

static
void pool_resize(wl_client* client, wl_resource* resource, i32 size)
{
    auto* pool = way_get_userdata<way_shm_pool>(resource);
    pool->mapping = nullptr;
    update_mapping(pool, size);
}

WAY_INTERFACE(wl_shm_pool) = {
    .create_buffer = create_buffer,
    .destroy = way_simple_destroy,
    .resize = pool_resize,
};

way_shm_pool::~way_shm_pool()
{
    core_assert(core_allocation_from(mapping.get())->ref_count == 1);
}

// -----------------------------------------------------------------------------

void way_shm_buffer::on_commit(way_surface* surface)
{
    pending_transfer = true;
}

bool way_shm_buffer::is_ready(way_surface* surface)
{
    if (pending_transfer) {
        auto mapping = pool->mapping;

        auto queue = gpu_get_queue(image->context(), gpu_queue_type::graphics);
        auto commands = gpu_commands_begin(queue);

        gpu_image_update(commands.get(), image.get(), static_cast<char*>(mapping->data) + offset);

        struct shm_transfer_guard : core_object
        {
            ref<way_buffer_lock> lock;
            // Protect mapping for duration of transfer
            // This must be destroyed before buffer, in case buffer is holding last reference to shm_pool
            ref<way_shm_mapping> mapping;

            ~shm_transfer_guard()
            {
                // Release buffer as soon as transfer has completed
                lock->buffer->release();
            }
        };
        auto transfer_guard = core_create<shm_transfer_guard>();
        transfer_guard->lock = lock();
        transfer_guard->mapping = mapping;
        gpu_commands_protect_object(commands.get(), transfer_guard.get());

        gpu_commands_submit(commands.get(), {});

        pending_transfer = false;
    }

    return true;
}

void way_shm_buffer::on_unlock()
{
}
