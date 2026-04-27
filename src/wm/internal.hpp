#pragma once

#include "wm.hpp"

#include <core/types.hpp>
#include <scene/scene.hpp>
#include <way/way.hpp>
#include <io/io.hpp>

enum class WmInteractionMode
{
    none,
    move,
    size,
    zone,
    focus_cycle,
};

struct ShellLauncher;

struct WmOutput {
    IoOutput* io;
    rect2f32 viewport;
};

struct WmServer
{
    ExecContext* exec;
    Gpu*         gpu;

    Ref<SeatManager> seat_manager;

    Ref<Scene> scene;
    EnumMap<WmLayer, Ref<SceneTree>> layers;

    SeatModifier main_mod;

    WmInteractionMode mode;

    Ref<ShellLauncher> launcher;

    Uid                    window_system_id;
    std::vector<WmWindow*> windows;

    std::vector<WmClient*> clients;

    struct {
        IoContext*          context;
        Ref<GpuImagePool>   pool;
        RefVector<WmOutput> outputs;

        std::vector<IoInputDevice*> led_devices;
    } io;

    Ref<SeatCursorManager> cursor_manager;
    RefVector<Seat> seats;

    struct {
        Ref<SeatEventFilter> filter;
    } hotkeys;

    struct {
        RefVector<SeatEventFilter> filter;
    } decoration;

    struct {
        Ref<SeatEventFilter> filter;
        SeatPointer* pointer;

        Weak<WmWindow> window;
        vec2f32  grab;
        rect2f32 frame;
        vec2f32  relative;
    } movesize;

    struct {
        Ref<SeatEventFilter> filter;
        SeatPointer* pointer;
        Ref<SceneTexture> texture;

        Weak<WmWindow> window;
        aabb2f64 initial_zone;
        aabb2f64 final_zone;
        bool     selecting = false;
    } zone;

    struct {
        Ref<SeatEventFilter> filter;
        Seat* seat;
        Weak<WmWindow> cycled;
    } focus;
};

void wm_init_io(     WmServer*);
void wm_init_seat(   WmServer*);
void wm_init_hotkeys(WmServer*);

void wm_init_movesize(   WmServer*);
void wm_init_zone(       WmServer*);
void wm_init_focus_cycle(WmServer*);

// -----------------------------------------------------------------------------

void wm_decoration_init(WmServer*);

// -----------------------------------------------------------------------------

void wm_arrange_windows(WmServer*);

// -----------------------------------------------------------------------------

struct WmClient
{
    WmServer* wm;

    std::move_only_function<void(WmClient*, WmEvent*)> listener;

    Ref<SeatClient> seat_client;

    ~WmClient();
};

// -----------------------------------------------------------------------------

struct WmWindow
{
    WmClient* client;

    vec2f32 extent;
    bool mapped;

    std::string title;

    Ref<SceneTree> root_tree;
    Ref<SceneTree> client_tree;

    Ref<SceneTexture> borders;

    std::vector<Weak<SeatInputRegion>> input_regions;

    ~WmWindow();
};

void wm_window_post_event(WmWindowEvent* event);

// -----------------------------------------------------------------------------

auto wm_get_seat_manager(WmServer*) -> SeatManager*;

void wm_broadcast_event(WmServer*, WmEvent*);
void wm_client_post_event(WmClient*, WmEvent*);
