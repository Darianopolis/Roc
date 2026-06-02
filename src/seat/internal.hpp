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

    SeatDataSource* selection;

    std::vector<SeatEventFilter*> event_filters;

    ~Seat();
};

void seat_clear_data(Seat*);

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

    Weak<SceneNode> cursor_visual;

    SeatDataSource* drag;
    Weak<SceneNode> drag_visual;
    Weak<SeatFocus> drag_focus;
};

void seat_pointer_update_drag(SeatPointer*);
void seat_pointer_end_drag(SeatPointer*);

// -----------------------------------------------------------------------------

struct SeatDataSource
{
    Weak<Seat> seat;

    Weak<SceneNode> drag_visual;

    SeatDataSourceInterface* impl;

    Flags<SeatDndAction> supported_actions;
    std::flat_set<std::string> offered;

    SeatDndAction current_action;
    bool          action_received;

    bool drag_accepted;
    bool cancelled;

    std::flat_set<SeatDataOffer*> offers;

    ~SeatDataSource();
};

struct SeatDataOffer
{
    SeatDataSource* source;

    ~SeatDataOffer();
};

void seat_offer_selection(Seat*, SeatClient*, SeatDataSource*);

// -----------------------------------------------------------------------------

struct SeatEventFilter
{
    Weak<Seat> seat;

    std::move_only_function<SeatEventFilterResult(SeatEvent*)> filter;

    ~SeatEventFilter();
};
