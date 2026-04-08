#include "roc.hpp"

#include "wm/wm.hpp"

// #include "way/way.hpp"
// #include "way/surface/surface.hpp"

int main()
{
    Roc roc = {};

    // Config

    roc.app_share = std::filesystem::path(getenv("HOME")) / ".local/share" / PROGRAM_NAME;
    roc.main_mod = SeatModifier::alt;
    roc.wallpaper = getenv("WALLPAPER");

    // Systems

    auto exec  = exec_create();
    auto gpu   = gpu_create(  exec.get(), {});
    auto io    = io_create(   exec.get(), gpu.get());
    auto scene = scene_create(exec.get(), gpu.get());
    // auto way   = way_create(exec.get(), gpu.get(), scene.get());
    auto wm    = wm_create({
        .exec = exec.get(),
        .gpu = gpu.get(),
        .io = io.get(),
        .scene = scene.get(),
        .modifier = roc.main_mod,
    });

    roc.exec = exec.get();
    roc.gpu = gpu.get();
    roc.scene = scene.get();
    // roc.way = way.get();
    roc.io = io.get();

    // Applets

    // auto _ = roc_init_background(&roc);
    // auto _ = roc_init_launcher(&roc);
    // auto _ = roc_init_log_viewer(&roc);

    // Test client

    auto initial_size = vec2f32{256, 256};

    auto tree = scene_tree_create();

    auto canvas = scene_texture_create();
    scene_texture_set_tint(canvas.get(), {255, 0, 255, 255});
    scene_texture_set_dst(canvas.get(), {{}, initial_size, xywh});
    scene_tree_place_below(tree.get(), nullptr, canvas.get());

    struct TestFocus : SeatFocus
    {
        virtual void keyboard_key(SeatKeyboard*, SeatInputCode code, bool pressed)
        {
            log_warn("TEST key: {} - {}", libevdev_event_code_get_name(EV_KEY, code), pressed);
        }
    };
    auto focus = ref_create<TestFocus>();
    auto input = seat_input_region_create(focus.get());
    seat_input_region_set_region(input.get(), {{{}, initial_size, xywh}});
    scene_tree_place_above(tree.get(), nullptr, input.get());

    auto inner = scene_tree_create();
    scene_tree_set_translation(inner.get(), {64, 64});
    scene_tree_place_above(tree.get(), nullptr, inner.get());

    auto square = scene_texture_create();
    scene_texture_set_tint(square.get(), {0, 255, 255, 255});
    scene_texture_set_dst(square.get(), {{}, {128, 128}, xywh});
    scene_tree_place_above(inner.get(), nullptr, square.get());

    scene_tree_place_above(wm_get_layer(wm.get(), WmLayer::foreground), nullptr, tree.get());

    // Run

    io_run(io.get());
}
