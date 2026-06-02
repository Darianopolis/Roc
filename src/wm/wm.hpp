#pragma once

#include <core/object.hpp>
#include <scene/scene.hpp>
#include <seat/seat.hpp>

struct ExecContext;
struct Gpu;
struct IoContext;

struct WmServer;
struct WmWindow;
struct WmPointerConstraint;

struct WmServerCreateInfo
{
    ExecContext* exec;
    Gpu*         gpu;

    SeatModifier main_mod;
};

auto wm_create(const WmServerCreateInfo&) -> Ref<WmServer>;

auto wm_get_gpu(WmServer*) -> Gpu*;
auto wm_get_exec(WmServer*) -> ExecContext*;
auto wm_get_scene_renderer(WmServer*) -> SceneRenderer*;
auto wm_get_scene(WmServer*) -> SceneTree*;

enum class WmLayer
{
    background,
    window,
    overlay,
};

auto wm_get_layer(WmServer*, WmLayer) -> SceneTree*;

// -----------------------------------------------------------------------------

struct WmOutput;
struct WmOutputInterface
{
    void(*request_frame)(void*);
};

auto wm_output_create(WmServer*, void*, WmOutputInterface) -> Ref<WmOutput>;
void wm_output_set_pixel_size(WmOutput*, vec2u32);
auto wm_output_frame(WmOutput*) -> bool;

auto wm_output_get_viewport(WmOutput*) -> rect2f32;
auto wm_output_get_workarea(WmOutput*) -> rect2f32;

// -----------------------------------------------------------------------------

struct WmInputDevice;
struct WmInputDeviceInterface
{
    void(*update_leds)(void*, Flags<libinput_led>);
};

struct WmInputDeviceChannel
{
    u32 type;   // evdev type
    u32 code;   // evdev code
    f32 value;  // normalized channel value
};

auto wm_input_device_create(WmServer*, void*, WmInputDeviceInterface) -> Ref<WmInputDevice>;
void wm_input_device_push_events(WmInputDevice*, bool quiet, std::span<WmInputDeviceChannel const>);

// -----------------------------------------------------------------------------

enum class WmEventType
{
    window_created,
    window_destroyed,
    window_mapped,
    window_unmapped,

    window_request_resize,
    window_request_close,

    output_added,
    output_configured,
    output_removed,
    output_layout,
    output_frame,

    seat_event,

    pointer_constraint_enabled,
    pointer_constraint_disabled,
};

struct WmWindowEvent
{
    WmEventType type;
    WmWindow* window;
    union {
        vec2f32 size;
    };
};

struct WmOutputEvent
{
    WmEventType type;
    WmOutput* output;
    u64 frame_id;
};

struct WmSeatEvent
{
    WmEventType type;
    SeatEvent* event;
};

struct WmPointerConstraintEvent
{
    WmEventType type;
    WmPointerConstraint* constraint;
};

union WmEvent
{
    WmEventType type;
    WmWindowEvent window;
    WmOutputEvent output;
    WmSeatEvent seat;
    WmPointerConstraintEvent pointer_constraint;
};

struct WmClient;
auto wm_connect(WmServer*) -> Ref<WmClient>;
void wm_listen(WmClient*, std::move_only_function<void(WmClient*, WmEvent*)>);

auto wm_get_seat_client(WmClient*) -> SeatClient*;

void wm_set_cursor( WmClient*, SceneNode* visual);
void wm_set_xcursor(WmClient*, const char* semantic);

// -----------------------------------------------------------------------------

void wm_begin_selection(WmServer*, SeatPointer*, std::move_only_function<void(rect2f32)>);

// -----------------------------------------------------------------------------

auto wm_window_create(WmClient*) -> Ref<WmWindow>;

void wm_window_set_focus(WmWindow*, SeatFocus*);

void wm_window_set_title( WmWindow*, std::string_view title);
void wm_window_set_app_id(WmWindow*, std::string_view app_id);

void wm_window_map(  WmWindow*);
void wm_window_unmap(WmWindow*);
void wm_window_raise(WmWindow*);

auto wm_window_is_mapped(WmWindow*) -> bool;

auto wm_window_get_tree(WmWindow*) -> SceneTree*;

void wm_window_request_close(WmWindow*);

auto wm_window_place_auto(WmWindow*) -> vec2f32;

/*
 * Update the window frame size
 *
 * Calling this implicitly acknowledges the most recent `window.request_resize` event.
 */
void wm_window_set_size(WmWindow*, vec2f32 size);

auto wm_window_get_frame(WmWindow*) -> rect2f32;

auto wm_window_is_focused(WmWindow*) -> bool;
void wm_window_focus(     WmWindow*);

void wm_window_set_fullscreen(WmWindow*, WmOutput*);
auto wm_window_get_fullscreen(WmWindow*) -> WmOutput*;

auto wm_window_is_movable(  WmWindow*) -> bool;
auto wm_window_is_resizable(WmWindow*) -> bool;

auto wm_find_window_for(WmServer*, SeatFocus*)    -> WmWindow*;
auto wm_find_window_at( WmServer*, vec2f32 point) -> WmWindow*;

// -----------------------------------------------------------------------------

enum class WmPointerConstraintType
{
    locked,
    confined
};

auto wm_constrain_pointer(WmWindow*, SceneInputRegion*, region2f32, WmPointerConstraintType) -> Ref<WmPointerConstraint>;
void wm_pointer_constraint_set_region(WmPointerConstraint*, region2f32);

// -----------------------------------------------------------------------------

auto wm_get_seat( WmServer*) -> Seat*;
auto wm_get_seats(WmServer*) -> std::span<Seat* const>;

// -----------------------------------------------------------------------------

void wm_request_frame(WmServer*);

auto wm_list_outputs(WmServer*) -> std::span<WmOutput* const>;

struct WmFindOutputResult
{
    WmOutput* output;
    vec2f32   position;
};

auto wm_find_output_at(WmServer*, vec2f32 point) -> WmFindOutputResult;
auto wm_find_output_for(WmServer*, WmWindow*) -> WmOutput*;
