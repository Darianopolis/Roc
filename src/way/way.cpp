#include "internal.hpp"

way_server::~way_server()
{
    wl_event_loop_fd = nullptr;
    wl_display_terminate(wl_display);
    wl_display_destroy(wl_display);
}

auto way_create(core_event_loop* event_loop, gpu_context* gpu, scene_context* scene) -> ref<way_server>
{
    auto server = core_create<way_server>();

    server->epoch = std::chrono::steady_clock::now();

    server->event_loop = event_loop;
    server->gpu = gpu;
    server->scene = scene;
    server->scene_system = scene_register_system(scene);

    way_seat_init(server.get());

    server->wl_display = wl_display_create();

    wl_display_set_default_max_buffer_size(server->wl_display, 4096);

    server->socket_name = wl_display_add_socket_auto(server->wl_display);

    server->wl_event_loop_fd = core_fd_reference(wl_event_loop_get_fd(wl_display_get_event_loop(server->wl_display)));
    core_fd_add_listener(server->wl_event_loop_fd.get(), event_loop, core_fd_event_bit::readable,
        [server = server.get()](int fd, flags<core_fd_event_bit> events) {
            unix_check<wl_event_loop_dispatch>(wl_display_get_event_loop(server->wl_display), 0);
            wl_display_flush_clients(server->wl_display);
        });

    way_global(server.get(), wl_shm);
    way_global(server.get(), wl_compositor);
    way_global(server.get(), wl_subcompositor);
    way_global(server.get(), xdg_wm_base);
    way_global(server.get(), wl_seat);
    way_global(server.get(), wl_data_device_manager);
    way_global(server.get(), wp_viewporter);
    way_global(server.get(), zwp_pointer_gestures_v1);
    way_global(server.get(), wp_cursor_shape_manager_v1);
    way_output_init(server.get());
    way_dmabuf_init(server.get());

    server->sampler = gpu_sampler_create(gpu, {
        .mag = VK_FILTER_NEAREST,
        .min = VK_FILTER_LINEAR,
    });

    server->client.created.data = server.get();
    server->client.created.listener.notify = way_on_client_create;
    wl_display_add_client_created_listener(server->wl_display, &server->client.created.listener);

    return server;
}

auto way_get_elapsed(way_server* server) -> std::chrono::steady_clock::duration
{
    return std::chrono::steady_clock::now() - server->epoch;
}

wl_global* way_global_(way_server* server, const wl_interface* interface, i32 version, wl_global_bind_func_t bind, void* data)
{
    core_assert(version <= interface->version);
    return wl_global_create(server->wl_display, interface, version, data ?: server, bind);
}

wl_resource* way_resource_create_(wl_client* client, const wl_interface* interface, int version, int id, const void* impl, void* data, bool refcount)
{
    auto resource = wl_resource_create(client, interface, version, id);
    if (refcount) {
        core_add_ref(data);
        wl_resource_set_implementation(resource, impl, data, [](wl_resource* resource) {
            core_remove_ref(wl_resource_get_user_data(resource));
        });
    } else {
        wl_resource_set_implementation(resource, impl, data, nullptr);
    }
    return resource;
}

u32 way_next_serial(way_server* server)
{
    return wl_display_next_serial(server->wl_display);
}

void way_queue_client_flush(way_server* server)
{
    // TODO: Queue to run at end of event
    wl_display_flush_clients(server->wl_display);
}

void way_simple_destroy(wl_client* client, wl_resource* resource)
{
    wl_resource_destroy(resource);
}
