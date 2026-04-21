#pragma once

#include "seat.hpp"

// -----------------------------------------------------------------------------

struct SeatManager
{
    std::vector<Seat*> seats;
};

// -----------------------------------------------------------------------------

struct Seat
{
    SeatManager* manager;

    Ref<SeatKeyboard> keyboard;
    Ref<SeatPointer> pointer;

    Ref<SeatDataSource> selection;

    std::vector<SeatEventFilter*> input_event_filters;

    ~Seat();
};

// -----------------------------------------------------------------------------

struct SeatClient
{
    SeatManager* manager;

    std::move_only_function<SeatEventHandlerFn> event_handler;

    std::vector<SeatInputRegion*> input_regions;

    ~SeatClient();
};

void seat_client_post_event(SeatClient*, SeatEvent*);

// -----------------------------------------------------------------------------

struct SeatInputDevice
{
    Seat* seat;

    SeatInputRegion* focus;
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

auto seat_find_input_region_at(SceneTree*, vec2f32 pos) -> SeatInputRegion*;

inline
auto seat_get_focus_client(SeatInputRegion* focus)
{
    return focus ? focus->client : nullptr;
}

// -----------------------------------------------------------------------------

struct SeatDataSource
{
    SeatClient* client;

    std::flat_set<std::string> offered;

    SeatDataSourceOps ops;

    ~SeatDataSource();
};

void seat_offer_selection(SeatClient*, SeatDataSource*);

// -----------------------------------------------------------------------------

struct SeatEventFilter
{
    Weak<Seat> seat;

    std::move_only_function<SeatEventFilterResult(SeatEvent*)> filter;

    ~SeatEventFilter();
};
