#include "internal.hpp"

auto wm_create(const WmServerCreateInfo& info) -> Ref<WmServer>
{
    auto wm = ref_create<WmServer>();

    wm->exec = info.exec;
    wm->gpu = info.gpu;

    wm->seat_manager = seat_manager_create();

    wm->scene_renderer = scene_renderer_create(wm->gpu);

    wm->scene = scene_tree_create();
    for (auto layer : enum_values<WmLayer>()) {
        auto* tree = (wm->layers[layer] = scene_tree_create()).get();
        scene_tree_place_above(wm->scene.get(), nullptr, tree);
    }

    debug_assert(info.main_mod);
    wm->main_mod = info.main_mod;

    wm->window_system_id = uid_allocate();

    wm_init_io(wm.get());
    wm_init_seat(wm.get());

    wm_cursor_init(wm.get());

    wm_pointer_constraints_init(wm.get());

    wm_init_hotkeys(wm.get());
    wm_init_movesize(wm.get());
    wm_init_zone(wm.get());
    wm_init_focus_cycle(wm.get());
    wm_init_selection(wm.get());

    wm_decoration_init(wm.get());

    return wm;
}

auto wm_get_seat_manager(WmServer* wm) -> SeatManager*
{
    return wm->seat_manager.get();
}

auto wm_get_gpu(WmServer* wm) -> Gpu*
{
    return wm->gpu;
}

auto wm_get_exec(WmServer* wm) -> ExecContext*
{
    return wm->exec;
}

auto wm_get_scene_renderer(WmServer* wm) -> SceneRenderer*
{
    return wm->scene_renderer.get();
}

auto wm_get_scene(WmServer* wm) -> SceneTree*
{
    return wm->scene.get();
}

auto wm_get_layer(WmServer* wm, WmLayer layer) -> SceneTree*
{
    return wm->layers[layer].get();
}

void wm_interaction_set_mode(WmServer* wm, WmInteractionMode mode)
{
    wm->mode = mode;
    wm_cursor_visual_update(wm);
}
