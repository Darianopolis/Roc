#include "internal.hpp"

auto wm_create(const WindowManagerCreateInfo& info) -> Ref<WindowManager>
{
    auto wm = ref_create<WindowManager>();
    wm->gpu = info.gpu;
    wm->scene = info.scene;
    wm->way = info.way;
    wm->io.context = info.io;

    wm->main_mod = SceneModifier::alt;

    wm->ui = ui_create(info.gpu, info.scene, info.app_share / "wm");
    ui_set_frame_handler(wm->ui.get(), [wm = wm.get()] {
        wm_log_frame(wm);
        wm_launcher_frame(wm);
    });

    wm->main_mod = SceneModifier::alt;

    wm_init_io(         wm.get());
    wm_init_seat(       wm.get());
    wm_init_hotkeys(    wm.get());
    wm_init_background( wm.get(), info.wallpaper);
    wm_init_interaction(wm.get());
    wm_init_zone(       wm.get());
    wm_init_log_viewer( wm.get());
    wm_init_launcher(   wm.get());

    return wm;
}
