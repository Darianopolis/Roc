#include "buffer.hpp"

#include "../surface/surface.hpp"

WAY_INTERFACE(wl_buffer) = {
    .destroy = way_simple_destroy,
};

auto WayBuffer::acquire(WaySurface* surface, WaySurfaceState* pending) -> Ref<GpuImage>
{
    if (!pending->buffer) return nullptr;

    if (pending->surface.damage) {
        auto bounds = pending->surface.damage.bounds();

        // Apply buffer transform
        auto transform = pending->set.contains(WaySurfaceStateComponent::buffer_transform)
            ? pending->buffer_transform
            : surface->current.buffer_transform;
        debug_assert(transform == WL_OUTPUT_TRANSFORM_NORMAL, "TODO: Support buffer transforms");

        // Apply buffer scale
        bounds.min *= pending->buffer_scale;
        bounds.max *= pending->buffer_scale;

        pending->buffer_damage.damage(bounds);
        pending->surface.damage.clear();
    }

    Flags<WayBufferAcquireFlags> flags = {};

    // Wait for acquire

    if (pending->acquire_point.syncobj) {
        if (syncboj_wait_pending) {
            return nullptr;
        }

        auto value = gpu_syncobj_get_value(pending->acquire_point.syncobj.get());
        if (pending->acquire_point.value > value) {
#if WAY_BUFFER_NOISY_WAITS
            log_warn("syncobj target value {} > {}, waiting...", pending->acquire_point.value, value);
#endif
            gpu_wait({pending->acquire_point.syncobj.get(), pending->acquire_point.value}, [surface = Weak(surface), buffer = Weak(this)](u64) {
                if (!surface || !buffer) return;
                buffer->syncboj_wait_pending = false;
                way_surface_try_flush(surface.get());
            });
            return nullptr;
        }

        flags |= WayBufferAcquireFlags::wait_handled;
    }

    // Check for buffer ready

    debug_assert(!pending->image);
    return pending->buffer->do_acquire(surface, pending->buffer_damage, flags, std::move(pending->release_point));
}

void WayBuffer::release(WayTimelinePoint&& point)
{
    if (point.syncobj) {
        gpu_syncobj_signal_value(point.syncobj.get(), point.value);
        point.syncobj = nullptr;
    }
    way_send<wl_buffer_send_release>(_resource);
}
