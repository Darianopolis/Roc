#include "server.hpp"

#include "buffer/buffer.hpp"
#include "data/data.hpp"
#include "output/output.hpp"
#include "seat/seat.hpp"
#include "shell/shell.hpp"
#include "surface/surface.hpp"
#include "client.hpp"

#include <core/process.hpp>

static
auto get_loop_fd(wl_display* display) -> int
{
    return wl_event_loop_get_fd(wl_display_get_event_loop(display));
}

WayServer::~WayServer()
{
    seats.clear();

    client.created.unlink();

    fd_unlisten(exec, get_loop_fd(wl_display));
    wl_display_terminate(wl_display);
    wl_display_destroy(wl_display);
}

auto way_create(WmServer* wm, ExecContext* exec) -> Ref<WayServer>
{
    auto server = ref_create<WayServer>();

    server->epoch = std::chrono::steady_clock::now();

    server->exec = exec;
    server->gpu = wm_get_gpu(wm);
    server->wm = wm;
    server->userdata_id = uid_allocate();
    server->timer = timer_create(exec);

    auto prev_debug = env_get("WAYLAND_DEBUG");
    env_set("WAYLAND_DEBUG", env_get("ROC_WAYLAND_DEBUG"));
    server->wl_display = wl_display_create();
    env_set("WAYLAND_DEBUG", prev_debug);

    wl_display_set_default_max_buffer_size(server->wl_display, 4096);

    server->socket_name = wl_display_add_socket_auto(server->wl_display);

    fd_listen(exec, get_loop_fd(server->wl_display), FdEventBit::readable,
        [server = server.get()](fd_t, Flags<FdEventBit> events) {
            unix_check<wl_event_loop_dispatch>(wl_display_get_event_loop(server->wl_display), 0);
            wl_display_flush_clients(server->wl_display);
        });

    way_global(server.get(), wl_shm);
    way_global(server.get(), wl_compositor);
    way_global(server.get(), wl_subcompositor);
    way_global(server.get(), xdg_wm_base);
    way_global(server.get(), wl_data_device_manager);
    way_global(server.get(), wp_viewporter);
    way_global(server.get(), zwp_pointer_gestures_v1);
    way_global(server.get(), wp_cursor_shape_manager_v1);
    way_global(server.get(), zxdg_decoration_manager_v1);
    way_global(server.get(), org_kde_kwin_server_decoration_manager);
    way_output_init(server.get());
    way_dmabuf_init(server.get());

    way_seat_init(server.get());

    server->sampler = gpu_sampler_create(server->gpu, {
        .mag = VK_FILTER_NEAREST,
        .min = VK_FILTER_LINEAR,
    });

    server->client.created.set(server.get(), server.get());
    server->client.created.listener.notify = way_on_client_create;
    wl_display_add_client_created_listener(server->wl_display, &server->client.created.listener);

    return server;
}

auto way_server_get_socket(WayServer* server) -> const char*
{
    return server->socket_name.c_str();
}

auto way_get_elapsed(WayServer* server) -> std::chrono::steady_clock::duration
{
    return std::chrono::steady_clock::now() - server->epoch;
}

auto way_global_interface(WayServer* server,
    const wl_interface* interface, i32 version, wl_global_bind_func_t bind,
    WayUserdata data) -> wl_global*
{
    debug_assert(version <= interface->version);
#if WAY_CHECKED_USERDATA
    if (data.data) way_userdata_register(server, {data.data, data.type});
#endif
    return wl_global_create(server->wl_display, interface, version, data.data ?: server, bind);
}

auto way_next_serial(WayServer* server) -> WaySerial
{
    return WaySerial(wl_display_next_serial(server->wl_display));
}

void way_queue_flush(wl_resource* resource)
{
    way_client_queue_flush(way_client_from(wl_resource_get_client(resource)));
}

void way_clear(WayServer* server)
{
    u32 count = 0;
    wl_client* wl_client;
    wl_client_for_each(wl_client, wl_display_get_client_list(server->wl_display)) {
        pid_t pid;
        uid_t uid;
        gid_t gid;
        wl_client_get_credentials(wl_client, &pid, &uid, &gid);
        kill(pid, SIGTERM);
        count++;
    }
    log_warn("way.shutdown - sent SIGTERM to {} clients", count);
}
