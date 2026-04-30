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

struct WayBuffer : WayObject
{
    WayResource _resource;

    vec2u32 extent;

    WayTimelinePoint release_point;

    // Sent on apply, should return a GpuImage when the buffer is ready to display
    [[nodiscard]] virtual auto do_acquire(WaySurface*, WayDamageRegion, Flags<WayBufferAcquireFlags>) -> Ref<GpuImage> = 0;

    auto acquire(WaySurface*, WaySurfaceState* pending) -> Ref<GpuImage>;
    void release();

protected:
    ~WayBuffer() = default;
};

// -----------------------------------------------------------------------------

struct WayShmPool : WayObject
{
    WayServer* server;

    WayResource resource;

    Fd    fd;
    void* data;
    usz   size;

    ~WayShmPool();
};

// -----------------------------------------------------------------------------

void way_dmabuf_init(WayServer*);

// -----------------------------------------------------------------------------

WAY_INTERFACE_DECLARE(wl_buffer);

WAY_INTERFACE_DECLARE(wl_shm, 2);
WAY_INTERFACE_DECLARE(wl_shm_pool);

WAY_INTERFACE_DECLARE(zwp_linux_dmabuf_v1, 5);
WAY_INTERFACE_DECLARE(zwp_linux_buffer_params_v1);
WAY_INTERFACE_DECLARE(zwp_linux_dmabuf_feedback_v1);
