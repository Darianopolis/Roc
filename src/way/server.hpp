#pragma once

#include "way.hpp"
#include "util.hpp"

#include <core/log.hpp>

#include <wayland-server-core.h>

UNIX_ERROR_BEHAVIOUR(wl_event_loop_dispatch, negative_one)

// -----------------------------------------------------------------------------

DECLARE_TAGGED_INTEGER(WaySerial, u32);

// -----------------------------------------------------------------------------

struct WaySeat;
struct WayClient;

struct WayServer
{
    ExecContext* exec;

    std::chrono::steady_clock::time_point epoch;

    Gpu* gpu;
    WmServer* wm;
    Uid userdata_id;

    wl_display* wl_display;
    std::string socket_name;

    Ref<GpuSampler> sampler;

    RefVector<WaySeat> seats;

    struct {
        WayListener created;
    } client;

    struct {
        Fd  format_table;
        usz format_table_size;
        std::vector<u16> tranche_formats;
    } dmabuf;

#if WAY_CHECKED_USERDATA
    ankerl::unordered_dense::map<void*, WayUserdataEntry> userdata_types;
#endif

    ~WayServer();
};

auto way_get_elapsed(WayServer*) -> std::chrono::steady_clock::duration;

// -----------------------------------------------------------------------------

auto way_next_serial(WayServer*) -> WaySerial;

void way_queue_flush(wl_resource*);

template<auto Fn>
void way_send(wl_resource* resource, auto&&... args)
{
    if (resource) {
        Fn(resource, args...);
        way_queue_flush(resource);
    } else {
        log_error("Failed to dispatch {}, resource is null", __PRETTY_FUNCTION__);
    }
}

template<typename ...Args>
void way_post_error(wl_resource* resource, u32 code, std::format_string<Args...> fmt, Args&&... args)
{
    if (!resource) return;
    auto message = std::vformat(fmt.get(), std::make_format_args(args...));
    log_error("{}", message);
    wl_resource_post_error(resource, code, "%s", message.c_str());
    way_queue_flush(resource);
}

// -----------------------------------------------------------------------------

auto way_global_interface(WayServer*, const wl_interface*, i32 version, wl_global_bind_func_t, WayUserdata = nullptr) -> wl_global*;
#define way_global(Server, Interface, ...) \
    way_global_interface(Server, &Interface##_interface, way_##Interface##_version, way_##Interface##_bind_global __VA_OPT__(,) __VA_ARGS__)
