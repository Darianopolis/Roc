#include "wm.hpp"

auto wm_create(Gpu* gpu, Scene* scene, std::filesystem::path app_share) -> Ref<WindowManager>
{
    auto wm = ref_create<WindowManager>();
    wm->scene = scene;

    wm->ui = ui_create(gpu, scene, app_share / "wm");
    ui_set_frame_handler(wm->ui.get(), [wm = wm.get()] {
        wm_log_frame(wm);
    });

    wm->main_mod = SceneModifier::alt;

    wm_interaction_init(wm.get());
    wm_zone_init(       wm.get());
    wm_log_init(        wm.get());

    return wm;
}
