#include "protocol.hpp"

#include "core/math.hpp"

const u32 wroc_wl_shm_version = 2;

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
        break;default:                  return gpu_drm_format(shm);
    }
}

static
void update_mapping(wroc_shm_pool* pool, usz size)
{
    auto mapping = core_create<wroc_shm_mapping>();
    mapping->size = size;
    mapping->data = unix_check<mmap>(nullptr, size, PROT_READ | PROT_WRITE, MAP_SHARED, pool->fd, 0).value;
    if (!mapping->data) {
        wroc_post_error(pool->resource, WL_SHM_ERROR_INVALID_FD, "mmap failed");
        return;
    }

    pool->mapping = mapping;
}

wroc_shm_mapping::~wroc_shm_mapping()
{
    munmap(data, size);
}

static
void wroc_wl_whm_create_pool(wl_client* client, wl_resource* resource, u32 id, int fd, i32 size)
{
    // TODO: We should enforce a limit on the number of open files a client can have to keep under 1024 for the whole process

    auto* new_resource = wroc_resource_create(client, &wl_shm_pool_interface, wl_resource_get_version(resource), id);
    auto* pool = core_create_unsafe<wroc_shm_pool>();
    pool->resource = new_resource;
    pool->fd = fd;
    wroc_resource_set_implementation_refcounted(new_resource, &wroc_wl_shm_pool_impl, pool);
    update_mapping(pool, size);
}

const struct wl_shm_interface wroc_wl_shm_impl = {
    .create_pool = wroc_wl_whm_create_pool,
    .release     = wroc_simple_resource_destroy_callback,
};

void wroc_wl_shm_bind_global(wl_client* client, void* data, u32 version, u32 id)
{
    auto* new_resource = wroc_resource_create(client, &wl_shm_interface, version, id);
    wroc_resource_set_implementation(new_resource, &wroc_wl_shm_impl, nullptr);

    for (auto&[format, _] : server->renderer->shm_formats) {
        wroc_send(wl_shm_send_format, new_resource, from_drm(format->drm));
    }
};

// -----------------------------------------------------------------------------

static
void wroc_wl_shm_pool_create_buffer(wl_client* client, wl_resource* resource, u32 id, i32 offset, i32 width, i32 height, i32 stride, u32 _format)
{
    auto* pool = wroc_get_userdata<wroc_shm_pool>(resource);

    auto format = gpu_format_from_drm(to_drm(wl_shm_format(_format)));
    if (!format) {
        wroc_post_error(resource, WL_SHM_ERROR_INVALID_FORMAT, "Format {} is not supported", core_to_string(wl_shm_format(_format)));
        return;
    }

    i32 needed = stride * height + offset;
    if (!pool->mapping) {
        wroc_post_error(resource, WL_SHM_ERROR_INVALID_FD, "Tried to map buffer from shm pool with previous mapping failure");
        return;
    }
    if (needed > pool->mapping->size) {
        wroc_post_error(resource, WL_SHM_ERROR_INVALID_STRIDE, "buffer mapped storage exceeds pool limits");
        return;
    }

    auto* new_resource = wroc_resource_create(client, &wl_buffer_interface, wl_resource_get_version(resource), id);

    auto* shm_buffer = core_create_unsafe<wroc_shm_buffer>();
    shm_buffer->type = wroc_buffer_type::shm;
    shm_buffer->resource = new_resource;
    shm_buffer->extent = {width, height};

    shm_buffer->pool = pool;
    shm_buffer->stride = stride;
    shm_buffer->format = format;
    shm_buffer->offset = offset;

    wroc_resource_set_implementation_refcounted(new_resource, &wroc_wl_buffer_impl, shm_buffer);

    shm_buffer->image = gpu_image_create(server->gpu, {
        .extent = shm_buffer->extent,
        .format = shm_buffer->format,
        .usage = gpu_image_usage::texture | gpu_image_usage::transfer
    });

    log_debug("Created shared buffer {}, format = {}", core_to_string(shm_buffer->extent), shm_buffer->format->name);
}

static
void wroc_wl_shm_pool_resize(wl_client* client, wl_resource* resource, i32 size)
{
    auto* pool = wroc_get_userdata<wroc_shm_pool>(resource);

    pool->mapping = nullptr;
    update_mapping(pool, size);
}

const struct wl_shm_pool_interface wroc_wl_shm_pool_impl = {
    .create_buffer = wroc_wl_shm_pool_create_buffer,
    .destroy       = wroc_simple_resource_destroy_callback,
    .resize        = wroc_wl_shm_pool_resize,
};

wroc_shm_pool::~wroc_shm_pool()
{
    core_assert(core_allocation_from(mapping.get())->ref_count == 1);
    mapping = nullptr;
    close(fd);
}

void wroc_shm_buffer::on_commit(wroc_surface* surface)
{
    pending_transfer = true;
}

bool wroc_shm_buffer::is_ready(wroc_surface* surface)
{
    if (pending_transfer) {
        auto mapping = pool->mapping;

        auto queue = gpu_get_queue(image->context(), gpu_queue_type::graphics);
        auto commands = gpu_begin(queue);

        const auto& info = image->format()->info;
        usz block_w = (image->extent().x + info.block_extent.width  - 1) / info.block_extent.width;
        usz block_h = (image->extent().y + info.block_extent.height - 1) / info.block_extent.height;
        usz image_size = block_w * block_h * info.texel_block_size;

        gpu_cmd_copy_memory_to_image(commands.get(), image.get(),
            {core_byte_offset_pointer<void>(mapping->data, offset), image_size},
            {{image->extent()}});

        struct shm_transfer_guard : wroc_object
        {
            ref<wroc_buffer_lock> lock;
            // Protect mapping for duration of transfer
            // This must be destroyed before buffer, in case buffer is holding last reference to shm_pool
            ref<wroc_shm_mapping> mapping;

            ~shm_transfer_guard()
            {
                // Release buffer as soon as transfer has completed
                lock->buffer->release();
            }
        };
        auto transfer_guard = core_create<shm_transfer_guard>();
        transfer_guard->lock = lock();
        transfer_guard->mapping = mapping;
        gpu_protect(commands.get(), transfer_guard.get());

        gpu_submit(commands.get(), {});

        pending_transfer = false;
    }

    return true;
}

void wroc_shm_buffer::on_unlock()
{
}
