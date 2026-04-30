#pragma once

#include <scene/scene.hpp>

struct SeatFocus;

// -----------------------------------------------------------------------------

struct SeatManager;

auto seat_manager_create() -> Ref<SeatManager>;

// -----------------------------------------------------------------------------

struct SeatClient;

auto seat_connect(SeatManager*) -> Ref<SeatClient>;

// -----------------------------------------------------------------------------

struct SeatCursorManager;

auto seat_cursor_manager_create(Gpu*, const char* theme, i32 size) -> Ref<SeatCursorManager>;

// -----------------------------------------------------------------------------

enum class SeatModifier : u32
{
    super = 1 << 0,
    shift = 1 << 1,
    ctrl  = 1 << 2,
    alt   = 1 << 3,
    num   = 1 << 4,
    caps  = 1 << 5,
};

enum class SeatModifierFlag
{
    ignore_locked = 1 << 0
};

using SeatInputCode = u32;

// -----------------------------------------------------------------------------

struct SeatKeyboard;
struct SeatPointer;
struct Seat;

// -----------------------------------------------------------------------------

auto seat_create(SeatManager*, SeatKeyboard*, SeatPointer*) -> Ref<Seat>;

auto seat_get_pointer( Seat*) -> SeatPointer*;
auto seat_get_keyboard(Seat*) -> SeatKeyboard*;

auto seat_get_modifiers(Seat*, Flags<SeatModifierFlag> = {}) -> Flags<SeatModifier>;

// -----------------------------------------------------------------------------

struct SeatPointerCreateInfo
{
    SeatCursorManager* cursor_manager;
    SceneTree* root;
    SceneTree* layer;
};

auto seat_pointer_create(const SeatPointerCreateInfo&) -> Ref<SeatPointer>;

void seat_pointer_focus(       SeatPointer*, SeatFocus*);
auto seat_pointer_get_position(SeatPointer*) -> vec2f32;
auto seat_pointer_get_pressed( SeatPointer*) -> std::span<const SeatInputCode>;
auto seat_pointer_get_focus(   SeatPointer*) -> SeatFocus*;
auto seat_pointer_get_seat(    SeatPointer*) -> Seat*;

void seat_pointer_button(SeatPointer*, SeatInputCode, bool pressd, bool quiet);
void seat_pointer_scroll(SeatPointer*, vec2f32 delta);
void seat_pointer_move(  SeatPointer*, vec2f32 position, vec2f32 rel_accel, vec2f32 rel_unaccel);

void seat_pointer_set_cursor( SeatPointer*, SceneNode*);
void seat_pointer_set_xcursor(SeatPointer*, const char* xcursor_semantic);

struct SeatKeyboardInfo
{
    xkb_context* context;
    xkb_state*   state;
    xkb_keymap*  keymap;
    i32          rate;
    i32          delay;
};

struct SeatKeyboardCreateInfo
{
    const char* layout;
    i32 rate;
    i32 delay;
};

auto seat_keyboard_create(const SeatKeyboardCreateInfo&) -> Ref<SeatKeyboard>;

void seat_keyboard_focus(        SeatKeyboard*, SeatFocus*);
auto seat_keyboard_get_modifiers(SeatKeyboard*, Flags<SeatModifierFlag> = {}) -> Flags<SeatModifier>;
auto seat_keyboard_get_pressed(  SeatKeyboard*) -> std::span<const SeatInputCode>;
auto seat_keyboard_get_sym(      SeatKeyboard*, SeatInputCode) -> xkb_keysym_t;
auto seat_keyboard_get_utf8(     SeatKeyboard*, SeatInputCode) -> std::string;
auto seat_keyboard_get_info(     SeatKeyboard*) -> const SeatKeyboardInfo&;
auto seat_keyboard_get_focus(    SeatKeyboard*) -> SeatFocus*;
auto seat_keyboard_get_seat(     SeatKeyboard*) -> Seat*;
auto seat_keyboard_get_leds(     SeatKeyboard*) -> Flags<libinput_led>;

auto seat_keyboard_key(SeatKeyboard*, SeatInputCode, bool pressd, bool quiet) -> Flags<xkb_state_component>;

// -----------------------------------------------------------------------------

struct SeatDataSource
{
    std::flat_set<std::string> offered;

    virtual void on_cancel() = 0;
    virtual void on_send(const char* mime_type, fd_t target) = 0;
};

void seat_data_source_offer(      SeatDataSource*, const char* mime_type);
auto seat_data_source_get_offered(SeatDataSource*) -> std::span<const std::string>;

void seat_data_source_receive(SeatDataSource*, const char* mime_type, fd_t fd);

void seat_set_selection(Seat*, SeatDataSource*);
auto seat_get_selection(Seat*) -> SeatDataSource*;

// -----------------------------------------------------------------------------

struct SeatFocus
{
    SeatClient* client;

    Ref<SceneInputRegion> input_region;

    SeatFocus* parent;
    std::vector<SeatFocus*> children;

    ~SeatFocus();
};

auto seat_focus_create(SeatClient*, SceneInputRegion*) -> Ref<SeatFocus>;

void seat_focus_set_parent(SeatFocus*, SeatFocus* parent);
auto seat_focus_contains(SeatFocus*, SeatFocus* target) -> bool;

auto seat_find_focus_for_input_region(SeatManager*, SceneInputRegion*) -> SeatFocus*;

// -----------------------------------------------------------------------------

enum class SeatEventType
{
    keyboard_enter,
    keyboard_leave,
    keyboard_key,
    keyboard_modifier,

    pointer_enter,
    pointer_leave,
    pointer_motion,
    pointer_button,
    pointer_scroll,

    selection,
};

struct SeatKeyboardEvent
{
    SeatEventType type;
    SeatKeyboard* keyboard;
    union {
        struct {
            SeatInputCode code;
            bool          pressed;
            bool          quiet;
        } key;
        SeatFocus* focus;
    };
};

struct SeatPointerEvent
{
    SeatEventType type;
    SeatPointer* pointer;
    union {
        struct {
            SeatInputCode code;
            bool          pressed;
            bool          quiet;
        } button;
        struct {
            vec2f32 rel_accel;
            vec2f32 rel_unaccel;
        } motion;
        struct {
            vec2f32 delta;
        } scroll;
        SeatFocus* focus;
    };
};

struct SeatDataEvent
{
    SeatEventType   type;
    SeatDataSource* source;
    Seat*           seat;
};

union SeatEvent
{
    SeatEventType     type;
    SeatKeyboardEvent keyboard;
    SeatPointerEvent  pointer;
    SeatDataEvent     data;
};

using SeatEventHandlerFn = void(SeatEvent*);

void seat_listen(SeatClient*, std::move_only_function<SeatEventHandlerFn>&&);

enum class SeatEventFilterResult
{
    passthrough,
    capture,
};

struct SeatEventFilter;

auto seat_add_event_filter(Seat*, std::move_only_function<SeatEventFilterResult(SeatEvent*)>) -> Ref<SeatEventFilter>;
