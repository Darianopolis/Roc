#pragma once

#include "../util.hpp"

#include "core/fd.hpp"

#include "scene/scene.hpp"

#include <wayland/server/pointer-gestures-unstable-v1.h>
#include <wayland/server/relative-pointer-unstable-v1.h>
#include <wayland/server/pointer-constraints-unstable-v1.h>
#include <wayland/server/cursor-shape-v1.h>

struct WayServer;
struct WaySurface;
struct WayClient;
struct WayDataOffer;

struct WayKeymap
{
    Fd fd;
    u32 size;
};

// -----------------------------------------------------------------------------

struct WaySeat : WayObject
{
    WayServer* server;
    SceneSeat* scene;

    wl_global* global;

    struct {
        SceneKeyboard* scene;
        WayKeymap keymap;
    } keyboard;

    struct {
        ScenePointer* scene;
    } pointer;

    struct {
        Weak<WaySurface> pointer;
        Weak<WaySurface> keyboard;
    } focus;

    ~WaySeat();
};

struct WaySeatClient : WayObject
{
    WaySeat* seat;
    WayClient* client;

    WayResourceList keyboards;
    WayResourceList pointers;
    WayResourceList data_devices;

    struct {
        Ref<WayDataOffer> offer;
    } drag;

    ~WaySeatClient();
};

// -----------------------------------------------------------------------------

void way_seat_init(WayServer*);

void way_seat_on_keyboard_enter(WaySeatClient*, SceneEvent*);
void way_seat_on_keyboard_leave(WaySeatClient*, SceneEvent*);
void way_seat_on_key(           WaySeatClient*, SceneEvent*);
void way_seat_on_modifier(      WaySeatClient*, SceneEvent*);

void way_seat_on_pointer_enter(WaySeatClient*, SceneEvent*);
void way_seat_on_pointer_leave(WaySeatClient*, SceneEvent*);
void way_seat_on_motion(       WaySeatClient*, SceneEvent*);
void way_seat_on_button(       WaySeatClient*, SceneEvent*);
void way_seat_on_scroll(       WaySeatClient*, SceneEvent*);

// -----------------------------------------------------------------------------

WAY_INTERFACE_DECLARE(zwp_pointer_gestures_v1, 3);
WAY_INTERFACE_DECLARE(zwp_pointer_gesture_swipe_v1);
WAY_INTERFACE_DECLARE(zwp_pointer_gesture_pinch_v1);
WAY_INTERFACE_DECLARE(zwp_pointer_gesture_hold_v1);

WAY_INTERFACE_DECLARE(wp_cursor_shape_manager_v1, 2);
WAY_INTERFACE_DECLARE(wp_cursor_shape_device_v1);

WAY_INTERFACE_DECLARE(wl_seat, 9);
WAY_INTERFACE_DECLARE(wl_keyboard);
WAY_INTERFACE_DECLARE(wl_pointer);
