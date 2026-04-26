#pragma once

#include "../util.hpp"
#include "../surface/state.hpp"

#include <core/fd.hpp>
#include <seat/seat.hpp>

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
    Seat* scene;

    wl_global* global;

    struct {
        SeatKeyboard* scene;
        WayKeymap keymap;
    } keyboard;

    struct {
        SeatPointer* scene;
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

    WayResourceList relative_pointers;

    struct {
        Ref<WayDataOffer> offer;
    } drag;

    ~WaySeatClient();
};

// -----------------------------------------------------------------------------

struct WayCursorSurface : WaySurfaceAddon
{
    virtual void commit(WayCommitId) final override {};
    virtual void apply( WayCommitId) final override;
};

// -----------------------------------------------------------------------------

void way_seat_init(         WayServer*);
void way_seat_keyboard_init(WaySeat*);
void way_seat_get_keyboard(wl_client*, wl_resource*, u32 id);
void way_seat_get_pointer( wl_client*, wl_resource*, u32 id);

void way_seat_on_keyboard_enter(WaySeatClient*, SeatEvent*);
void way_seat_on_keyboard_leave(WaySeatClient*, SeatEvent*);
void way_seat_on_key(           WaySeatClient*, SeatEvent*);
void way_seat_on_modifier(      WaySeatClient*, SeatEvent*);

void way_seat_on_pointer_enter(WaySeatClient*, SeatEvent*);
void way_seat_on_pointer_leave(WaySeatClient*, SeatEvent*);
void way_seat_on_motion(       WaySeatClient*, SeatEvent*);
void way_seat_on_button(       WaySeatClient*, SeatEvent*);
void way_seat_on_scroll(       WaySeatClient*, SeatEvent*);

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

WAY_INTERFACE_DECLARE(zwp_relative_pointer_manager_v1, 1);
WAY_INTERFACE_DECLARE(zwp_relative_pointer_v1);

WAY_INTERFACE_DECLARE(zwp_pointer_constraints_v1, 1);
WAY_INTERFACE_DECLARE(zwp_locked_pointer_v1);
WAY_INTERFACE_DECLARE(zwp_confined_pointer_v1);
