#pragma once

#include "wm.hpp"

#include "core/types.hpp"
#include "scene/scene.hpp"
#include "way/way.hpp"
#include "io/io.hpp"

enum class WmInteractionMode
{
    none,
    move,
    size,
    zone,
};

struct RocLauncher;

struct WmOutput {
    IoOutput* io;
    rect2f32 viewport;
};

struct WindowManager
{
    ExecContext* exec;
    Gpu*         gpu;

    Ref<Scene> scene;

    SceneModifier main_mod;

    WmInteractionMode mode;

    Ref<RocLauncher> launcher;

    Uid                    window_system_id;
    std::vector<WmWindow*> windows;

    struct {
        IoContext*          context;
        Ref<GpuImagePool>   pool;
        RefVector<WmOutput> outputs;

        std::vector<WmOutputListener> output_listeners;
    } io;

    struct {
        Ref<SceneClient> client;
    } seat;

    struct {
        Ref<SceneEventFilter> filter;
    } hotkeys;

    struct {
        Ref<SceneEventFilter> filter;
        ScenePointer* pointer;

        Weak<WmWindow> window;
        vec2f32  grab;
        rect2f32 frame;
        vec2f32  relative;
    } movesize;

    struct {
        Ref<SceneEventFilter> filter;
        ScenePointer* pointer;
        Ref<SceneTexture> texture;

        Weak<WmWindow> window;
        aabb2f64 initial_zone;
        aabb2f64 final_zone;
        bool     selecting = false;
    } zone;
};

void wm_init_io(      WindowManager*);
void wm_init_seat(    WindowManager*);
void wm_init_hotkeys( WindowManager*);
void wm_init_movesize(WindowManager*);
void wm_init_zone(    WindowManager*);

// -----------------------------------------------------------------------------

struct WmWindow
{
    WindowManager* wm;

    vec2f32 extent;
    bool mapped;

    std::string title;

    Ref<SceneTree> tree;

    WmWindowListener listener;

    std::vector<Weak<SceneInputRegion>> input_regions;

    ~WmWindow();
};

void wm_window_post_event(WmWindowEvent* event);

// -----------------------------------------------------------------------------

void wm_post_output_event(WindowManager*, WmOutputEvent*);
