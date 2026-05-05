#include "internal.hpp"

auto wm_create(const WmServerCreateInfo& info) -> Ref<WmServer>
{
    auto wm = ref_create<WmServer>();

    wm->exec = info.exec;
    wm->gpu = info.gpu;

    wm->seat_manager = seat_manager_create();

    wm->scene = scene_create(wm->gpu);
    for (auto layer : enum_values<WmLayer>()) {
        auto* tree = (wm->layers[layer] = scene_tree_create()).get();
        scene_tree_place_above(scene_get_root(wm->scene.get()), nullptr, tree);
    }

    debug_assert(info.main_mod);
    wm->main_mod = info.main_mod;

    wm->window_system_id = uid_allocate();

    wm_init_io(wm.get());
    wm_init_seat(wm.get());

    wm_pointer_constraints_init(wm.get());

    wm_init_hotkeys(wm.get());
    wm_init_movesize(wm.get());
    wm_init_zone(wm.get());
    wm_init_focus_cycle(wm.get());

    wm_decoration_init(wm.get());

    return wm;
}

auto wm_get_seat_manager(WmServer* wm) -> SeatManager*
{
    return wm->seat_manager.get();
}

auto wm_get_scene(WmServer* wm) -> Scene*
{
    return wm->scene.get();
}

auto wm_get_layer(WmServer* wm, WmLayer layer) -> SceneTree*
{
    return wm->layers[layer].get();
}
