#pragma once

#include "wm.hpp"

#include <core/types.hpp>
#include <core/color.hpp>

#include <scene/scene.hpp>
#include <way/way.hpp>

struct WmConfig
{
    struct {
        vec2f32 size    = vec2f32(2, 2);
        vec4u8  normal  = color_from_hex("#4C4C4C");
        vec4u8  focused = color_from_hex("#6666FF");
    } border;

    struct {
        vec2u32 count = {6, 2};
        vec2f32 selection_leeway = {0.3f, 0.3f};
        i32     spacing = 8;
        vec4u8  color_initial  = color_from_hex("#99999999");
        vec4u8  color_selected = color_from_hex("#6666FF99");
    } zone;

    struct {
        struct {
            vec2i32 tl = {9, 9};
            vec2i32 br = {9, 9};
        } padding;
    } workarea;

    ankerl::unordered_dense::map<u32, u32> rebinds {
        {BTN_EXTRA, KEY_LEFTCTRL},
        {BTN_SIDE,  KEY_LEFTMETA},
    };
};

enum class WmInteractionMode
{
    none,
    move,
    size,
    zone,
    focus_cycle,
    selection,
};

struct ShellLauncher;

struct WmOutput
{
    WmServer* server;

    vec2u32 pixel_size;
    rect2f32 viewport;

    // TODO: Partial redraws
    bool needs_redraw = false;
    u64 frame_id = 0;
    bool bump_frame_id = true;

    void* userdata;
    WmOutputInterface interface;

    ~WmOutput();
};

struct WmInputDevice
{
    WmServer* server;

    void* userdata;
    WmInputDeviceInterface interface;

    ~WmInputDevice();
};

struct WmServer
{
    WmConfig config;

    ExecContext* exec;
    Gpu*         gpu;

    Ref<SeatManager> seat_manager;

    Ref<SceneRenderer> scene_renderer;
    Ref<SceneTree> scene;
    Listener<SceneDamageCallback> scene_damage_listener;
    EnumMap<WmLayer, Ref<SceneTree>> layers;

    SeatModifier main_mod;

    WmInteractionMode mode;

    Ref<ShellLauncher> launcher;

    Uid window_system_id;

    Link<WmWindow> root_windows;
    std::vector<WmWindow*> windows;

    std::vector<WmClient*> clients;

    WmPointerConstraint* active_pointer_constraint;
    std::vector<WmPointerConstraint*> pointer_constraints;
    Ref<SeatEventFilter> pointer_constraints_filter;

    struct {
        std::vector<WmOutput*> outputs;
        std::vector<WmInputDevice*> input_devices;
        u64 prev_frame_id = 0;
    } io;

    Ref<SeatCursorManager> cursor_manager;
    RefVector<Seat> seats;
    RefVector<SeatEventFilter> cursor_event_filter;

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

    struct {
        Ref<SeatEventFilter> filter;
        SeatPointer* pointer;
        Ref<SceneTexture> texture;

        vec2f32 initial_point;
        aabb2f32 rect;
        bool selecting = false;

        std::move_only_function<void(aabb2f32)> callback;
    } selection;
};

void wm_init_io(     WmServer*);
void wm_init_seat(   WmServer*);
void wm_init_hotkeys(WmServer*);

void wm_init_movesize(   WmServer*);
void wm_init_zone(       WmServer*);
void wm_init_focus_cycle(WmServer*);
void wm_init_selection(  WmServer*);

void wm_interaction_set_mode(WmServer*, WmInteractionMode);

// -----------------------------------------------------------------------------

void wm_cursor_visual_update(WmServer*);
void wm_cursor_init(WmServer*);

// -----------------------------------------------------------------------------

void wm_decoration_init(WmServer*);

// -----------------------------------------------------------------------------

void wm_arrange_windows(WmServer*);

// -----------------------------------------------------------------------------

struct WmCursorUnset  { constexpr auto operator==(const WmCursorUnset& ) const -> bool { return true; } };
struct WmCursorHidden { constexpr auto operator==(const WmCursorHidden&) const -> bool { return true; } };
using WmCursorVisual = std::variant<WmCursorUnset, WmCursorHidden, Weak<SceneNode>, std::string>;

struct WmClient
{
    WmServer* server;

    std::move_only_function<void(WmClient*, WmEvent*)> listener;

    Ref<SeatClient> seat_client;

    WmCursorVisual cursor = WmCursorUnset {};

    ~WmClient();
};

// -----------------------------------------------------------------------------

struct WmWindow
{
    WmClient* client;

    Link<WmWindow> link;

    WmWindow* parent;
    Link<WmWindow> children;

    vec2f32 anchor;
    vec2f32 relative;
    Weak<WmOutput> oneshot_output_constraint;

    vec2f32 extent;
    bool mapped;

    std::string app_id;
    std::string title;

    Ref<SceneTree> root_tree;

    Ref<SceneTexture> backdrop;
    RefVector<SceneTexture> borders;

    Weak<SeatFocus> focus;

    struct {
        WmOutput* output;
        rect2f32 restore;
    } fullscreen;

    ~WmWindow();
};

void wm_window_post_event(WmWindowEvent*);

void wm_window_request_reposition(WmWindow*, rect2f32 frame, vec2f32 gravity);

// -----------------------------------------------------------------------------

struct WmPointerConstraint
{
    WmServer* server;

    Weak<WmWindow> window;
    Weak<SceneInputRegion> input_region;

    WmPointerConstraintType type;

    region2f32 region;

    ~WmPointerConstraint();
};

void wm_pointer_constraints_init(WmServer*);
void wm_update_active_pointer_constraint(WmServer*);
auto wm_pointer_constraint_apply(WmServer*, vec2f32 position, vec2f32 delta) -> vec2f32;

// -----------------------------------------------------------------------------

auto wm_get_seat_manager(WmServer*) -> SeatManager*;

void wm_broadcast_event(WmServer*, WmEvent*);
void wm_client_post_event(WmClient*, WmEvent*);
