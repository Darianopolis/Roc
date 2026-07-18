#pragma once

#include "../util.hpp"
#include "../surface/surface.hpp"

#include <gpu/gpu.hpp>

#include <wayland/server/linux-dmabuf-v1.h>
#include <wayland/server/linux-drm-syncobj-v1.h>

enum class WayBufferAcquireFlags : u32
{
    wait_handled = 1 << 0,
};

struct WaySurface;

struct WayBuffer
{
    WayResource _resource;

    vec2u32 extent;

    bool syncboj_wait_pending = false;

    // Sent on apply, should return a GpuImage when the buffer is ready to display
    [[nodiscard]] virtual auto do_acquire(WaySurface*, WayDamageRegion, Flags<WayBufferAcquireFlags>, WayTimelinePoint) -> Ref<GpuImage> = 0;

    auto acquire(WaySurface*, WaySurfaceState* pending) -> Ref<GpuImage>;
    void release(WayTimelinePoint&&);

protected:
    ~WayBuffer() = default;
};

#define WAY_BUFFER_NOISY_WAITS 0

// -----------------------------------------------------------------------------

void way_dmabuf_init(WayServer*);

// -----------------------------------------------------------------------------

WAY_INTERFACE_DECLARE(wl_buffer);

WAY_INTERFACE_DECLARE(wl_shm, 2);
WAY_INTERFACE_DECLARE(wl_shm_pool);

WAY_INTERFACE_DECLARE(zwp_linux_dmabuf_v1, 5);
WAY_INTERFACE_DECLARE(zwp_linux_buffer_params_v1);
WAY_INTERFACE_DECLARE(zwp_linux_dmabuf_feedback_v1);
