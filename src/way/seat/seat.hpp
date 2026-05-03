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

struct WaySeat
{
    WayServer* server;
    Seat* seat;

    wl_global* global;

    WayKeymap     keymap;
    SeatKeyboard* keyboard;

    SeatPointer* pointer;

    struct {
        Weak<WaySurface> pointer;
        Weak<WaySurface> keyboard;
    } focus;

    ~WaySeat();
};

struct WayClientSeat
{
    WayClient* client;
    WaySeat*   seat;

    WayResourceList keyboards;
    WayResourceList pointers;
    WayResourceList relative_pointers;
    WayResourceList data_devices;

    struct {
        Ref<WayDataOffer> offer;
    } drag;

    ~WayClientSeat();
};

// -----------------------------------------------------------------------------

struct WayCursorSurface : WaySurfaceAddon
{
    virtual void commit(WayCommitId) final override {};
    virtual void apply( WayCommitId) final override;
};

// -----------------------------------------------------------------------------

void way_seat_handle_event(WayClient*, SeatEvent*);

void way_seat_init(         WayServer*);
void way_seat_keyboard_init(WaySeat*);
void way_seat_get_keyboard(wl_client*, wl_resource*, u32 id);
void way_seat_get_pointer( wl_client*, wl_resource*, u32 id);

void way_seat_on_keyboard_enter(WayClientSeat*, SeatEvent*);
void way_seat_on_keyboard_leave(WayClientSeat*, SeatEvent*);
void way_seat_on_key(           WayClientSeat*, SeatEvent*);
void way_seat_on_modifier(      WayClientSeat*, SeatEvent*);

void way_seat_on_pointer_enter(WayClientSeat*, SeatEvent*);
void way_seat_on_pointer_leave(WayClientSeat*, SeatEvent*);
void way_seat_on_motion(       WayClientSeat*, SeatEvent*);
void way_seat_on_button(       WayClientSeat*, SeatEvent*);
void way_seat_on_scroll(       WayClientSeat*, SeatEvent*);

// -----------------------------------------------------------------------------

struct WmPointerConstraint;

void way_pointer_constraint_on_active(WayClient*, WmPointerConstraint*, bool active);

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
