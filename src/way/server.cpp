#include "server.hpp"

#include "buffer/buffer.hpp"
#include "data/data.hpp"
#include "output/output.hpp"
#include "seat/seat.hpp"
#include "shell/shell.hpp"
#include "surface/surface.hpp"
#include "client.hpp"

static
int get_loop_fd(wl_display* display)
{
    return wl_event_loop_get_fd(wl_display_get_event_loop(display));
}

WayServer::~WayServer()
{
    seats.clear();

    exec_fd_unlisten(exec, get_loop_fd(wl_display));
    wl_display_terminate(wl_display);
    wl_display_destroy(wl_display);
}

auto way_create(ExecContext* exec, Gpu* gpu, WindowManager* wm) -> Ref<WayServer>
{
    auto server = ref_create<WayServer>();

    server->epoch = std::chrono::steady_clock::now();

    server->exec = exec;
    server->gpu = gpu;
    server->wm = wm;
    server->userdata_id = uid_allocate();

    server->wl_display = wl_display_create();

    wl_display_set_default_max_buffer_size(server->wl_display, 4096);

    server->socket_name = wl_display_add_socket_auto(server->wl_display);

    exec_fd_listen(exec, get_loop_fd(server->wl_display), FdEventBit::readable,
        [server = server.get()](int fd, Flags<FdEventBit> events) {
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

    server->sampler = gpu_sampler_create(gpu, {
        .mag = VK_FILTER_NEAREST,
        .min = VK_FILTER_LINEAR,
    });

    server->client.created.data = server.get();
    server->client.created.listener.notify = way_on_client_create;
    wl_display_add_client_created_listener(server->wl_display, &server->client.created.listener);
    wm_add_output_listener(wm, [server = server.get()](WmOutputEvent* event) {
        switch (event->type) {
            break;case WmEventType::output_frame:
                for (auto* client : server->client.list) {
                    for (auto* surface : client->surfaces) {
                        way_surface_on_redraw(surface);
                    }
                }

            break;default:
                ;
        }
    });

    return server;
}

auto way_get_elapsed(WayServer* server) -> std::chrono::steady_clock::duration
{
    return std::chrono::steady_clock::now() - server->epoch;
}

wl_global* way_global_interface(WayServer* server, const wl_interface* interface, i32 version, wl_global_bind_func_t bind, WayObject* data)
{
    debug_assert(version <= interface->version);
    return wl_global_create(server->wl_display, interface, version, data ?: server, bind);
}

auto way_next_serial(WayServer* server) -> WaySerial
{
    return WaySerial(wl_display_next_serial(server->wl_display));
}

void way_queue_client_flush(WayServer* server)
{
    // TODO: Queue to run at end of event
    wl_display_flush_clients(server->wl_display);
}
