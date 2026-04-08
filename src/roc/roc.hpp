#pragma once

#include "scene/scene.hpp"
// #include "ui/ui.hpp"
// #include "way/way.hpp"
#include "io/io.hpp"
#include "seat/seat.hpp"

struct Roc
{
    ExecContext* exec;
    Gpu*         gpu;
    Scene*       scene;
    // WayServer*   way;
    IoContext*   io;

    SeatModifier main_mod;

    std::filesystem::path app_share;
    std::filesystem::path wallpaper;
};

auto roc_init_launcher(          Roc*) -> Ref<void>;
auto roc_init_log_viewer(        Roc*) -> Ref<void>;
auto roc_init_simple_test_client(Roc*) -> Ref<void>;
auto roc_init_ui_demo_window(    Roc*) -> Ref<void>;
auto roc_init_background(        Roc*) -> Ref<void>;
