#pragma once

#include "way.hpp"
#include "util.hpp"

#include <wayland-server-core.h>

UNIX_ERROR_BEHAVIOUR(wl_event_loop_dispatch, negative_one)

// -----------------------------------------------------------------------------

DECLARE_TAGGED_INTEGER(WaySerial, u32);

// -----------------------------------------------------------------------------

struct WaySeat;
struct WayClient;

struct WayServer : WayObject
{
    ExecContext* exec;

    std::chrono::steady_clock::time_point epoch;

    Gpu* gpu;
    WindowManager* wm;
    Uid userdata_id;

    Ref<SceneClient> seat_listener;

    wl_display* wl_display;
    std::string socket_name;

    Ref<GpuSampler> sampler;

    RefVector<WaySeat> seats;

    struct {
        WayListener created;
        std::vector<WayClient*> list;
    } client;

    struct {
        Fd  format_table;
        usz format_table_size;
        std::vector<u16> tranche_formats;
    } dmabuf;

    ~WayServer();
};

auto way_get_elapsed(WayServer*) -> std::chrono::steady_clock::duration;

// -----------------------------------------------------------------------------

auto way_next_serial(WayServer* server) -> WaySerial;

void way_queue_client_flush(WayServer* server);

void way_send_event(WayServer* server, const char* fn_name, auto fn, auto&& resource, auto&&... args)
{
    if (resource) {
        fn(resource, args...);
        way_queue_client_flush(server);
    } else {
        log_error("Failed to dispatch {}, resource is null", fn_name);
    }
}

#define way_send(Server, Fn, Resource, ...) \
    way_send_event(Server, #Fn, Fn, Resource __VA_OPT__(,) __VA_ARGS__)

template<typename ...Args>
void way_post_error(WayServer* server, wl_resource* resource, u32 code, std::format_string<Args...> fmt, Args&&... args)
{
    if (!resource) return;
    auto message = std::vformat(fmt.get(), std::make_format_args(args...));
    log_error("{}", message);
    wl_resource_post_error(resource, code, "%s", message.c_str());
    way_queue_client_flush(server);
}

// -----------------------------------------------------------------------------

wl_global* way_global_interface(WayServer*, const wl_interface*, i32 version, wl_global_bind_func_t, WayObject* data = nullptr);
#define way_global(Server, Interface, ...) \
    way_global_interface(Server, &Interface##_interface, way_##Interface##_version, way_##Interface##_bind_global __VA_OPT__(,) __VA_ARGS__)
