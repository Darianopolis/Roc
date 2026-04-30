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
        gpu_wait({pending->acquire_point.syncobj.get(), pending->acquire_point.value});
        flags |= WayBufferAcquireFlags::wait_handled;
    }

    if (pending->release_point.syncobj) {
        release_point = std::move(pending->release_point);
    }

    // Check for buffer ready

    debug_assert(!pending->image);
    return pending->buffer->do_acquire(surface, pending->buffer_damage, flags);
}

void WayBuffer::release()
{
    if (release_point.syncobj) {
        gpu_syncobj_signal_value(release_point.syncobj.get(), release_point.value);
    } else {
        way_send(wl_buffer, release, _resource);
    }
}
