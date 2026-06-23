#include "internal.hpp"

auto wm_create(const WmServerCreateInfo& info) -> Ref<WmServer>
{
    auto server = ref_create<WmServer>();

    server->exec = info.exec;
    server->gpu = info.gpu;

    server->seat_manager = seat_manager_create();

    server->scene_renderer = scene_renderer_create(server->gpu);

    server->image_pool = gpu_image_pool_create(server->gpu);

    server->scene_root = scene_tree_create();
    server->scene_primary_tree = scene_tree_create();
    scene_tree_place_above(server->scene_root.get(), nullptr, server->scene_primary_tree.get());
    for (auto layer : enum_values<WmLayer>()) {
        auto* parent = layer == WmLayer::cursor ? server->scene_root.get() : server->scene_primary_tree.get();
        auto* tree = (server->scene_layers[layer] = scene_tree_create()).get();
        scene_tree_place_above(parent, nullptr, tree);
    }

    debug_assert(info.main_mod);
    server->main_mod = info.main_mod;

    server->window_system_id = uid_allocate();

    wm_init_io(server.get());
    wm_init_seat(server.get());

    wm_cursor_init(server.get());

    wm_pointer_constraints_init(server.get());

    wm_init_hotkeys(server.get());
    wm_init_movesize(server.get());
    wm_init_zone(server.get());
    wm_init_focus_cycle(server.get());
    wm_init_selection(server.get());

    wm_decoration_init(server.get());

    return server;
}

auto wm_get_seat_manager(WmServer* server) -> SeatManager*
{
    return server->seat_manager.get();
}

auto wm_get_gpu(WmServer* server) -> Gpu*
{
    return server->gpu;
}

auto wm_get_exec(WmServer* server) -> ExecContext*
{
    return server->exec;
}

auto wm_get_scene_renderer(WmServer* server) -> SceneRenderer*
{
    return server->scene_renderer.get();
}

auto wm_get_scene(WmServer* server) -> SceneTree*
{
    return server->scene_root.get();
}

auto wm_get_layer(WmServer* server, WmLayer layer) -> SceneTree*
{
    return server->scene_layers[layer].get();
}

void wm_interaction_set_mode(WmServer* server, WmInteractionMode mode)
{
    server->mode = mode;
    wm_cursor_visual_update(server);
}
