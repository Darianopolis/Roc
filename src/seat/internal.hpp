#pragma once

#include "seat.hpp"

// -----------------------------------------------------------------------------

struct SeatManager
{
    std::vector<Seat*> seats;
    std::vector<SeatClient*> clients;
};

// -----------------------------------------------------------------------------

struct Seat
{
    SeatManager* manager;

    std::string name;

    Ref<SeatKeyboard> keyboard;
    Ref<SeatPointer> pointer;

    Ref<SeatDataSource> selection;

    std::vector<SeatEventFilter*> event_filters;

    ~Seat();
};

// -----------------------------------------------------------------------------

struct SeatClient
{
    SeatManager* manager;

    std::move_only_function<SeatEventHandlerFn> event_handler;

    std::vector<SeatFocus*> foci;

    ~SeatClient();
};

void seat_post_event(Seat*, SeatClient*, SeatEvent*);

// -----------------------------------------------------------------------------

struct SeatInputDevice
{
    Seat* seat;

    SeatFocus* focus;
};

auto seat_post_input_event(Weak<SeatInputDevice>, SeatEvent*) -> bool;

// -----------------------------------------------------------------------------

struct SeatKeyboard : SeatInputDevice, SeatKeyboardInfo
{
    CountingSet<u32> pressed;

    Flags<SeatModifier> depressed;
    Flags<SeatModifier> latched;
    Flags<SeatModifier> locked;

    EnumMap<SeatModifier, xkb_mod_mask_t> mod_masks;

    ~SeatKeyboard();
};

// -----------------------------------------------------------------------------

struct SeatPointer : SeatInputDevice
{
    CountingSet<u32> pressed;

    SeatCursorManager* cursor_manager;
    SceneTree* root;

    Ref<SceneTree> tree;
};

// -----------------------------------------------------------------------------

inline
auto seat_get_focus_client(SeatFocus* focus)
{
    return focus ? focus->client : nullptr;
}

// -----------------------------------------------------------------------------

void seat_offer_selection(Seat*, SeatClient*, SeatDataSource*);

// -----------------------------------------------------------------------------

struct SeatEventFilter
{
    Weak<Seat> seat;

    std::move_only_function<SeatEventFilterResult(SeatEvent*)> filter;

    ~SeatEventFilter();
};
