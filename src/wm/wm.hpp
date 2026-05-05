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

enum class WmLayer
{
    background,
    window,
    overlay,
};

auto wm_get_scene(WmServer*) -> Scene*;
auto wm_get_layer(WmServer*, WmLayer) -> SceneTree*;

// -----------------------------------------------------------------------------

struct WmOutput;
struct WmOutputInterface
{
    void(*request_frame)(void*);
};

auto wm_output_create(WmServer*, void*, WmOutputInterface) -> Ref<WmOutput>;
void wm_output_set_pixel_size(WmOutput*, vec2i32);
void wm_output_frame(WmOutput*);

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
    window_repositioned,

    window_reposition_requested,
    window_close_requested,

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
        struct {
            rect2f32 frame;
            vec2f32  gravity;
        } reposition;
    };
};

struct WmOutputEvent
{
    WmEventType type;
    WmOutput* output;
};

struct WmSeatEvent
{
    WmEventType type;
    SeatEvent*  event;
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

// -----------------------------------------------------------------------------

auto wm_window_create(WmClient*) -> Ref<WmWindow>;

void wm_window_set_focus(WmWindow*, SeatFocus*);

void wm_window_set_title(WmWindow*, std::string_view title);
void wm_window_set_app_id(WmWindow*, std::string_view app_id);

void wm_window_map(  WmWindow*);
void wm_window_unmap(WmWindow*);
void wm_window_raise(WmWindow*);

auto wm_window_get_tree(WmWindow*) -> SceneTree*;

void wm_window_request_reposition(WmWindow*, rect2f32 frame, vec2f32 gravity);
void wm_window_request_close(     WmWindow*);

void wm_window_set_frame(WmWindow*, rect2f32 frame);
auto wm_window_get_frame(WmWindow*) -> rect2f32;

auto wm_window_is_focused(WmWindow*) -> bool;
void wm_window_focus(     WmWindow*);
auto wm_find_window_for(WmServer*, SeatFocus*) -> WmWindow*;

auto wm_find_window_at(WmServer*, vec2f32 point) -> WmWindow*;

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

auto wm_output_get_viewport(WmOutput*) -> rect2f32;

struct WmFindOutputResult
{
    WmOutput* output;
    vec2f32   position;
};

auto wm_find_output_at(WmServer*, vec2f32 point) -> WmFindOutputResult;
