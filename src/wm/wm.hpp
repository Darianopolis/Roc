#pragma once

#include "core/object.hpp"
#include "scene/scene.hpp"

struct ExecContext;
struct Gpu;
struct WindowManager;
struct IoContext;

struct WmWindow;
struct WmOutput;

struct WindowManagerCreateInfo
{
    ExecContext* exec;
    Gpu*         gpu;
    IoContext*   io;

    SceneModifier main_mod;
};

auto wm_create(const WindowManagerCreateInfo&) -> Ref<WindowManager>;

auto wm_get_scene(WindowManager*) -> Scene*;

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

union WmEvent
{
    WmEventType type;
    WmWindowEvent window;
    WmOutputEvent output;
};

// -----------------------------------------------------------------------------

auto wm_window_create(WindowManager*) -> Ref<WmWindow>;

using WmWindowListener = std::move_only_function<void(WmWindowEvent*)>;
void wm_window_set_event_listener(WmWindow*, WmWindowListener);

void wm_window_add_input_region(WmWindow*, SceneInputRegion*);

void wm_window_set_title(WmWindow*, std::string_view title);

void wm_window_map(  WmWindow*);
void wm_window_unmap(WmWindow*);
void wm_window_raise(WmWindow*);

auto wm_window_get_tree(WmWindow*) -> SceneTree*;

void wm_window_request_reposition(WmWindow*, rect2f32 frame, vec2f32 gravity);
void wm_window_request_close(     WmWindow*);

void wm_window_set_frame(WmWindow*, rect2f32 frame);
auto wm_window_get_frame(WmWindow*) -> rect2f32;

auto wm_find_window_at(WindowManager*, vec2f32 point) -> WmWindow*;

// -----------------------------------------------------------------------------

void wm_request_frame(WindowManager*);

auto wm_list_outputs(WindowManager*) -> std::span<WmOutput* const>;

auto wm_output_get_viewport(WmOutput*) -> rect2f32;

using WmOutputListener = std::move_only_function<void(WmOutputEvent*)>;
void wm_add_output_listener(WindowManager*, WmOutputListener);

struct WmFindOutputResult
{
    WmOutput* output;
    vec2f32   position;
};

auto wm_find_output_at(WindowManager*, vec2f32 point) -> WmFindOutputResult;
