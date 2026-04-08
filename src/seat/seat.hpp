#pragma once

#include "core/containers.hpp"
#include "core/enum.hpp"

#include "scene/scene.hpp"

// -----------------------------------------------------------------------------

using SeatInputCode = u32;

enum class SeatModifier : u32
{
    super = 1 << 0,
    shift = 1 << 1,
    ctrl  = 1 << 2,
    alt   = 1 << 3,
    num   = 1 << 4,
    caps  = 1 << 5,
};

// -----------------------------------------------------------------------------

struct SeatKeyboardCreateInfo
{
    const char* layout;
    i32         rate;
    i32         delay;
};

struct SeatKeyboard
{
    xkb_context* context;
    xkb_state*   state;
    xkb_keymap*  keymap;
    i32          rate;
    i32          delay;

    CountingSet<u32> pressed;

    Flags<SeatModifier> mods_depressed;
    Flags<SeatModifier> mods_latched;
    Flags<SeatModifier> mods_locked;
    EnumMap<SeatModifier, xkb_mod_mask_t> mod_masks;

    ~SeatKeyboard();
};

auto seat_keyboard_create(const SeatKeyboardCreateInfo&) -> Ref<SeatKeyboard>;
void seat_keyboard_press(  SeatKeyboard*, SeatInputCode);
void seat_keyboard_release(SeatKeyboard*, SeatInputCode);

// -----------------------------------------------------------------------------

struct SeatPosition
{
    SceneTree* root;
    vec2f32 translation;
};

struct SeatMotion
{
    vec2f32 accel;
    vec2f32 unaccel;
};

struct SeatPointer
{
    CountingSet<u32> pressed;

    SeatPosition position;
};

auto seat_pointer_create() -> Ref<SeatPointer>;
void seat_pointer_press(  SeatPointer*, SeatInputCode);
void seat_pointer_release(SeatPointer*, SeatInputCode);
void seat_pointer_scroll( SeatPointer*, vec2f32 delta);
void seat_pointer_move(   SeatPointer*, SeatPosition, SeatMotion);

// -----------------------------------------------------------------------------

struct SeatFocus;

struct Seat
{
    SeatKeyboard* keyboard;
    SeatPointer*  pointer;

    // These are tracked separately from keyboard and pointer now
    SeatFocus* keyboard_focus;
    SeatFocus* pointer_focus;
};

auto seat_create() -> Ref<Seat>;

struct SeatFocus
{
    virtual void keyboard_key(  SeatKeyboard*, SeatInputCode, bool pressed) {}
    virtual void keyboard_enter(SeatKeyboard*) {}
    virtual void keyboard_leave(SeatKeyboard*) {}

    virtual void pointer_button(SeatPointer*, SeatInputCode, bool pressed) {}
    virtual void pointer_motion(SeatPointer*, SeatMotion) {}
    virtual void pointer_scroll(SeatPointer*, vec2f32 delta) {}
    virtual void pointer_enter( SeatPointer*) {}
    virtual void pointer_leave( SeatPointer*) {}
};

// -----------------------------------------------------------------------------

struct SeatInputRegion : SceneNode
{
    Ref<SeatFocus> focus;

    region2f32 region;

    virtual void apply_damage(Scene*);

    ~SeatInputRegion();
};

auto seat_input_region_create(SeatFocus*) -> Ref<SeatInputRegion>;
void seat_input_region_set_region(SeatInputRegion*, region2f32);

// -----------------------------------------------------------------------------

struct SeatDataSource;

struct SeatDataSourceInterface
{
    virtual void cancel() {}
    virtual void send(const char* mime_type, int fd) = 0;
};

auto seat_data_source_create(SeatDataSourceInterface*) -> Ref<SeatDataSource>;

void seat_data_source_offer(      SeatDataSource*, const char* mime_type);
auto seat_data_source_get_offered(SeatDataSource*) -> std::span<const std::string>;

void seat_data_source_receive(SeatDataSource*, const char* mime_type, int fd);

void seat_set_selection(Seat*, SeatDataSource*);
auto seat_get_selection(Seat*) -> SeatDataSource*;
