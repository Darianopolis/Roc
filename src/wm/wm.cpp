#include "internal.hpp"

auto wm_create(const WmCreateInfo& info) -> Ref<WindowManager>
{
    auto wm = ref_create<WindowManager>();

    wm->exec = info.exec;
    wm->gpu = info.gpu;
    wm->scene = info.scene;
    wm->io.context = info.io;

    debug_assert(info.modifier);
    wm->modifier = info.modifier;

    for (auto layer : magic_enum::enum_values<WmLayer>()) {
        wm->layers[layer] = scene_tree_create();
        scene_tree_place_above(scene_get_root(wm->scene), nullptr, wm->layers[layer].get());
    }

    wm_init_seat(wm.get());
    wm_init_io(  wm.get());

    // wm_init_hotkeys( wm.get());
    // wm_init_movesize(wm.get());
    // wm_init_zone(    wm.get());

    return wm;
}

auto wm_get_layer(WindowManager* wm, WmLayer layer) -> SceneTree*
{
    return wm->layers[layer].get();
}
