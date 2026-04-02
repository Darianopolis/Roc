#pragma once

#include "wm.hpp"

#include "core/types.hpp"
#include "scene/scene.hpp"
#include "ui/ui.hpp"
#include "way/way.hpp"
#include "io/io.hpp"

enum class WmInteractionMode
{
    none,
    move,
    size,
    zone,
};

struct WmLauncher;

struct WmOutput {
    Ref<SceneOutput> scene;
    IoOutput*        io;
};

struct WindowManager
{
    Gpu*       gpu;
    Scene*     scene;
    WayServer* way;

    Ref<Ui> ui;

    SceneModifier main_mod;

    WmInteractionMode mode;
    Ref<SceneClient> client;
    Ref<SceneInputRegion> focus;

    Ref<WmLauncher> launcher;

    struct {
        IoContext* context;
        Ref<GpuImagePool> pool;
        Ref<SceneClient> client;
        std::vector<WmOutput> outputs;
    } io;

    struct {
        Ref<SceneClient> client;
    } seat;

    struct {
        Ref<SceneClient> client;
    } hotkeys;

    struct {
        Ref<SceneClient> client;
        Ref<GpuImage> image;
        Ref<GpuSampler> sampler;
        Ref<SceneTree> layer;
    } background;

    struct {
        ScenePointer* pointer;

        Weak<SceneWindow> window;

        vec2f32  grab;
        rect2f32 frame;
        vec2f32  relative;

    } movesize;

    struct {
        ScenePointer* pointer;

        Weak<SceneWindow> window;

        Ref<SceneTexture> texture;

        aabb2f64 initial_zone;
        aabb2f64 final_zone;
        bool     selecting = false;
    } zone;

    struct {
        bool show_details;
        i64 selected = -1;
    } log;
};

void wm_init_io(     WindowManager*);
void wm_init_seat(   WindowManager*);
void wm_init_hotkeys(WindowManager*);

void wm_init_interaction(WindowManager*);
void wm_init_zone(       WindowManager*);

void wm_movesize_handle_event(WindowManager*, SceneEvent*);
void wm_zone_handle_event(    WindowManager*, SceneEvent*);

void wm_log_frame(WindowManager*);
void wm_init_log_viewer( WindowManager*);

void wm_init_launcher(WindowManager*);
void wm_launcher_frame(WindowManager*);

void wm_init_background(WindowManager*, const std::filesystem::path&);
