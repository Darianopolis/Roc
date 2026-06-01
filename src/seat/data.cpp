#include "internal.hpp"

#include <core/log.hpp>

static
void offer_selection_to_focus(Seat* seat, SeatDataSource* source)
{
    if (auto* focus = seat_keyboard_get_focus(seat->keyboard.get())) {
        seat_offer_selection(seat, focus->client, source);
    }
}

static
auto make_offer(SeatDataSource* source) -> Ref<SeatDataOffer>
{
    auto offer = ref_create<SeatDataOffer>();
    offer->source = source;
    source->offers.emplace(offer.get());
    source->current_action = {};
    return offer;
}

void seat_offer_selection(Seat* seat, SeatClient* client, SeatDataSource* source)
{
    seat_post_event(seat, client, ptr_to(SeatEvent {
        .data {
            .type = SeatEventType::selection,
            .seat = seat,
        },
    }));
}

auto seat_get_selection(Seat* seat) -> Ref<SeatDataOffer>
{
    return seat->selection ? make_offer(seat->selection) : nullptr;
}

// -----------------------------------------------------------------------------

static
void send_cancel(SeatDataSource* source)
{
    source->cancelled = true;
    source->impl->cancel();
}

SeatDataOffer::~SeatDataOffer()
{
    if (source) {
        if (source->drag_accepted) {
            log_warn("Drag was accepted but offer destroyed, cancelling...");
            send_cancel(source);
        }
        source->offers.erase(this);
    }
}

auto seat_data_offer_get_actions(SeatDataOffer* offer) -> Flags<SeatDndAction>
{
    if (!offer->source) return {};
    return offer->source->supported_actions;
}

auto seat_data_offer_get_mime_types(SeatDataOffer* offer) -> std::span<const std::string>
{
    if (!offer->source) return {};
    return offer->source->offered;
}

void seat_data_offer_accept(SeatDataOffer* offer, const char* mime_type)
{
    if (!offer->source) return;
}

static
void set_action(SeatDataSource* source, SeatDndAction action)
{
    source->action_received = true;
    source->current_action = action;
    source->impl->action_update(action);
}

auto seat_data_offer_set_actions(SeatDataOffer* offer, Flags<SeatDndAction> actions, SeatDndAction preferred) -> SeatDndAction
{
    if (!offer->source) return {};
    auto* source = offer->source;

    SeatDndAction selected = {};

    if (source->supported_actions.contains(preferred)) {
        selected = preferred;
    } else {
        actions &= source->supported_actions;
        if      (actions.contains(SeatDndAction::copy)) selected = SeatDndAction::copy;
        else if (actions.contains(SeatDndAction::move)) selected = SeatDndAction::move;
        else if (actions.contains(SeatDndAction::ask )) selected = SeatDndAction::ask;
    }

    set_action(source, selected);
    return selected;
}

void seat_data_offer_receive(SeatDataOffer* offer, std::string_view mime_type, fd_t fd)
{
    log_debug("seat_data_offer_receive({})", mime_type);

    if (!offer->source) {
        log_error("  offer is disconnected!");
        return;
    }
    auto* source = offer->source;

    log_debug("  sending!");

    source->impl->send(mime_type, fd);
}

void seat_data_offer_finish(SeatDataOffer* offer)
{
    if (!offer->source) return;
    auto* source = offer->source;

    source->impl->dnd_finished();
}

// -----------------------------------------------------------------------------

static
auto make_source(Seat* seat, SeatDataSourceInterface* interface, std::span<const std::string_view> mime_types)
{
    auto source = ref_create<SeatDataSource>();
    source->seat = seat;
    source->impl = interface;
    for (auto& mime : mime_types) {
        source->offered.emplace(mime);
    }
    return source;
}

static
void disconnect_offers(SeatDataSource* source)
{
    for (auto* offer : source->offers) {
        offer->source = nullptr;
    }
    source->offers.clear();
}

void seat_clear_selection(Seat* seat)
{
    if (seat->selection) {
        disconnect_offers(seat->selection);
        send_cancel(seat->selection);
        seat->selection = nullptr;
    }
}

auto seat_set_selection(Seat* seat, const SeatDataSourceCreateInfo& info) -> Ref<SeatDataSource>
{
    seat_clear_selection(seat);

    auto source = make_source(seat, info.interface, info.mime_types);
    seat->selection = source.get();

    offer_selection_to_focus(seat, source.get());

    return source;
}

void update_drag_visual(SeatPointer* pointer, SceneNode* visual)
{
    if (pointer->drag_visual.get() == visual) return;

    if (pointer->drag_visual) {
        scene_node_unparent(pointer->drag_visual.get());
    }

    if (visual) {
        scene_tree_place_below(pointer->tree.get(), nullptr, visual);
    }
    pointer->drag_visual = visual;
}

static
void invalidate_drag_action(SeatDataSource* source)
{
    if (!source) return;
    source->action_received = false;
}

static
void drag_leave(SeatPointer* pointer)
{
    if (auto* drag_focus = std::exchange(pointer->drag_focus, nullptr).get()) {
        invalidate_drag_action(pointer->drag);
        seat_post_event(pointer->seat, drag_focus->client, ptr_to(SeatEvent {
            .data = {
                .type = SeatEventType::drag_leave,
                .seat = seat_pointer_get_seat(pointer),
                .drag = {
                    .focus = drag_focus,
                }
            },
        }));
    }

    disconnect_offers(pointer->drag);

    set_action(pointer->drag, SeatDndAction{});
}

static
void cancel_drag(SeatPointer* pointer)
{
    if (!pointer->drag) return;

    drag_leave(pointer);

    send_cancel(pointer->drag);

    update_drag_visual(pointer, nullptr);
    pointer->drag = nullptr;
}

auto seat_start_drag(Seat* seat, const SeatDataSourceCreateInfo& info) -> Ref<SeatDataSource>
{
    log_debug("START DRAG");

    auto* pointer = seat_get_pointer(seat);

    if (seat_pointer_get_pressed(pointer).empty()) {
        log_warn("Can't start drag as pointer does not have implicit drag");
        return nullptr;
    }

    if (pointer->drag) {
        cancel_drag(pointer);
    }

    auto source = make_source(seat, info.interface, info.mime_types);
    source->supported_actions = info.drag_actions;

    source->drag_visual = info.drag_visual;
    update_drag_visual(pointer, info.drag_visual);

    pointer->drag = source.get();

    seat_pointer_focus(pointer, nullptr);

    return source;
}

void seat_pointer_end_drag(SeatPointer* pointer)
{
    log_debug("END DRAG");

    if (!pointer->drag) return;

    auto* source = pointer->drag;

    auto action = source->current_action;
    if (!source->action_received) {
        log_warn("Action was not been received for source, setting to NONE");
        action = {};
    }

    log_debug("  action = {}", action);
    log_debug("  drag_focus = {}", (void*)pointer->drag_focus.get());

    if (action == SeatDndAction{} || !pointer->drag_focus) {
        log_warn("  cancelling!");
        cancel_drag(pointer);
        return;
    }

    log_debug("  performing drop");
    source->drag_accepted = true;

    auto drag_focus = std::exchange(pointer->drag_focus, nullptr).get();
    seat_post_event(pointer->seat, drag_focus->client, ptr_to(SeatEvent {
        .data = {
            .type = SeatEventType::drag_drop,
            .seat = seat_pointer_get_seat(pointer),
            .drag = {
                .focus = drag_focus,
                .action = action,
            }
        },
    }));

    if (!source->cancelled) {
        source->impl->dnd_drop_performed();
    }

    update_drag_visual(pointer, nullptr);
    pointer->drag = nullptr;
}

void seat_pointer_update_drag(SeatPointer* pointer)
{
    auto* seat = seat_pointer_get_seat(pointer);

    SeatFocus* new_focus = nullptr;
    if (pointer->drag) {
        if (auto* region = scene_find_input_region_at(pointer->root, seat_pointer_get_position(pointer))) {
            new_focus = seat_find_focus_for_input_region(pointer->seat->manager, region);
        }
    }

    auto* old_focus = pointer->drag_focus.get();

    if (old_focus == new_focus && new_focus) {
        invalidate_drag_action(pointer->drag);
        seat_post_event(pointer->seat, seat_get_focus_client(new_focus), ptr_to(SeatEvent {
            .data = {
                .type = SeatEventType::drag_motion,
                .seat = seat,
                .drag = {
                    .focus = new_focus,
                }
            }
        }));

        return;
    }

    if (old_focus) {
        drag_leave(pointer);
    }

    if (new_focus) {
        pointer->drag_focus = new_focus;
        invalidate_drag_action(pointer->drag);
        seat_post_event(pointer->seat, new_focus->client, ptr_to(SeatEvent {
            .data = {
                .type = SeatEventType::drag_enter,
                .seat = seat,
                .drag = {
                    .offer = make_offer(pointer->drag).get(),
                    .focus = new_focus,
                }
            }
        }));
    }
}

void seat_clear_data(Seat* seat)
{
    seat_clear_selection(seat);
    cancel_drag(seat->pointer.get());
}

SeatDataSource::~SeatDataSource()
{
    if (seat) {
        if (seat->selection == this) {
            seat_clear_selection(seat.get());
        }

        auto* pointer = seat_get_pointer(seat.get());
        if (pointer->drag == this) {
            cancel_drag(pointer);
        }
    }

    disconnect_offers(this);
}
