#include "../internal.hpp"

// -----------------------------------------------------------------------------

#include <wayland-client-core.h>

#include <wayland/client/xdg-shell.h>
#include <wayland/client/xdg-decoration-unstable-v1.h>
#include <wayland/client/relative-pointer-unstable-v1.h>
#include <wayland/client/pointer-constraints-unstable-v1.h>
#include <wayland/client/linux-dmabuf-v1.h>
#include <wayland/client/linux-drm-syncobj-v1.h>

UNIX_ERROR_BEHAVIOUR(wl_display_dispatch_timeout, negative_one)

// -----------------------------------------------------------------------------

struct IoWayland;
struct IoWaylandOutput;
struct IoWaylandKeyboard;
struct IoWaylandPointer;

// -----------------------------------------------------------------------------

template<typename K, typename V>
struct IoWaylandProxyCache
{
    using Vptr = std::unique_ptr<V, void(*)(V*)>;
    struct entry { Weak<K> key; Vptr value; };

    void(*destroy)(V*);
    std::vector<entry> entries;

    V* find(K* needle)
    {
        V* found = nullptr;
        std::erase_if(entries, [&](const auto& entry) {
            if (!entry.key) return true;
            if (entry.key.get() == needle) found = entry.value.get();
            return false;
        });
        return found;
    }

    V* insert(K* key, V* value)
    {
        return entries.emplace_back(key, Vptr(value, destroy)).value.get();
    }
};

template<typename T>
std::span<T> io_to_span(wl_array* array)
{
    usz count = array->size / sizeof(T);
    return std::span<T>(static_cast<T*>(array->data), count);
}

// -----------------------------------------------------------------------------

#define IO_WL_INTERFACE(Name) struct Name* Name = {}
#define IO_WL_LISTENER(Name) const Name##_listener io_##Name##_listener
#define IO_WL_STUB(Type, Name) \
    .Name = [](void*, Type* t, auto...) { log_error("TODO - " #Type "{{{}}}::" #Name, (void*) t); }
#define IO_WL_STUB_QUIET(Name) \
    .Name = [](auto...) {}

struct IoWayland
{
    IO_WL_INTERFACE(wl_display);
    IO_WL_INTERFACE(wl_registry);
    IO_WL_INTERFACE(wl_compositor);
    IO_WL_INTERFACE(xdg_wm_base);
    IO_WL_INTERFACE(wl_seat);
    IO_WL_INTERFACE(zxdg_decoration_manager_v1);
    IO_WL_INTERFACE(zwp_relative_pointer_manager_v1);
    IO_WL_INTERFACE(zwp_pointer_constraints_v1);
    IO_WL_INTERFACE(zwp_linux_dmabuf_v1);
    IO_WL_INTERFACE(wp_linux_drm_syncobj_manager_v1);

    struct {
        std::vector<std::pair<GpuFormat, GpuDrmModifier>> table;
        GpuFormatSet set;
    } format;

    RefVector<IoWaylandOutput> outputs;

    std::chrono::steady_clock::time_point current_dispatch_time;

    Ref<IoWaylandKeyboard> keyboard;
    Ref<IoWaylandPointer>  pointer;

    bool in_keyboard_enter;

    IoWaylandProxyCache<GpuSyncobj, wp_linux_drm_syncobj_timeline_v1> syncobj_cache { wp_linux_drm_syncobj_timeline_v1_destroy };
    IoWaylandProxyCache<GpuImage, wl_buffer> buffer_cache  { wl_buffer_destroy };

    ~IoWayland();
};

// -----------------------------------------------------------------------------

struct IoWaylandOutput : IoOutputBase
{
    IO_WL_INTERFACE(wl_surface);
    IO_WL_INTERFACE(xdg_surface);
    IO_WL_INTERFACE(xdg_toplevel);
    IO_WL_INTERFACE(zxdg_toplevel_decoration_v1);
    IO_WL_INTERFACE(zwp_locked_pointer_v1);
    IO_WL_INTERFACE(wp_linux_drm_syncobj_surface_v1);

    wl_callback* frame_callback = {};
    bool pointer_locked = false;

    struct {
        vec2u32 size;
    } configure;

    virtual auto info() -> IoOutputInfo final override
    {
        return {
            .size = size,
            .formats = &io->wayland->format.set,
        };
    }

    struct release_slot
    {
        Ref<GpuImage>   image;
        Ref<GpuSyncobj> syncobj;
        u64             point;
    };

    std::vector<release_slot> release_slots;

    virtual void commit(GpuImage*, GpuSyncpoint done, Flags<IoOutputCommitFlag>) final override;

    ~IoWaylandOutput();
};

inline
auto get_impl(IoOutput* output) -> IoWaylandOutput*
{
    return dynamic_cast<IoWaylandOutput*>(output);
}

// -----------------------------------------------------------------------------

struct IoWaylandKeyboard : IoInputDeviceBase
{
    IO_WL_INTERFACE(wl_keyboard);

    ~IoWaylandKeyboard();
};

struct IoWaylandPointer : IoInputDeviceBase
{
    IO_WL_INTERFACE(wl_pointer);
    IO_WL_INTERFACE(zwp_relative_pointer_v1);

    Weak<IoOutput> current_output;
    u32 last_serial;

    ~IoWaylandPointer();
};

// -----------------------------------------------------------------------------

template<typename T>
void io_wl_destroy(auto fn, T* t)
{
    if (t) fn(t);
}

#define IO_WL_DESTROY(T) if (T) T##_destroy(T)

// -----------------------------------------------------------------------------

extern IO_WL_LISTENER(xdg_surface);
extern IO_WL_LISTENER(xdg_wm_base);
extern IO_WL_LISTENER(wl_pointer);
extern IO_WL_LISTENER(wl_keyboard);
extern IO_WL_LISTENER(wl_seat);
extern IO_WL_LISTENER(wl_registry);
extern IO_WL_LISTENER(zxdg_toplevel_decoration_v1);
extern IO_WL_LISTENER(xdg_toplevel);
extern IO_WL_LISTENER(zwp_relative_pointer_v1);
extern IO_WL_LISTENER(zwp_locked_pointer_v1);
extern IO_WL_LISTENER(zwp_linux_dmabuf_feedback_v1);
