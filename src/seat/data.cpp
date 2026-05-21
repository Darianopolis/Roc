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

SeatDataOffer::~SeatDataOffer()
{
    if (source) {
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

    source->current_action = selected;
    source->impl->action_update(source->current_action);
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

auto seat_set_selection(Seat* seat, const SeatDataSourceCreateInfo& info) -> Ref<SeatDataSource>
{
    auto source = make_source(seat, info.interface, info.mime_types);
    if (seat->selection) {
        seat->selection->impl->cancel();
    }
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

auto seat_start_drag(Seat* seat, const SeatDataSourceCreateInfo& info) -> Ref<SeatDataSource>
{
    log_error("START DRAG");

    auto* pointer = seat_get_pointer(seat);

    if (seat_pointer_get_pressed(pointer).empty()) {
        log_warn("Can't start drag as pointer does not have implicit drag");
        return nullptr;
    }

    if (pointer->drag) {
        pointer->drag->impl->cancel();
        pointer->drag = nullptr;
    }

    auto source = make_source(seat, info.interface, info.mime_types);
    source->supported_actions = info.drag_actions;

    source->drag_visual = info.drag_visual;
    update_drag_visual(pointer, info.drag_visual);

    pointer->drag = source.get();

    return source;
}

void seat_pointer_end_drag(SeatPointer* pointer)
{
    log_error("END DRAG");

    if (!pointer->drag) return;

    auto action = pointer->drag->current_action;

    log_error("  action = {}", action);
    log_error("  drag_focus = {}", (void*)pointer->drag_focus.get());

    if (action != SeatDndAction{} && pointer->drag_focus) {
        pointer->drag->impl->dnd_drop_performed();

        log_warn("  performing drop");

        auto drag_client = seat_get_focus_client(pointer->drag_focus.get());
        seat_post_event(pointer->seat, drag_client, ptr_to(SeatEvent {
            .data = {
                .type = SeatEventType::drag_drop,
                .seat = seat_pointer_get_seat(pointer),
                .drag = {
                    .focus = pointer->drag_focus.get(),
                }
            },
        }));

        // Prevent a drag_leave event from being sent
        pointer->drag_focus = nullptr;
    } else {
        pointer->drag->impl->cancel();
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

    auto old_client = seat_get_focus_client(old_focus);
    auto new_client = seat_get_focus_client(new_focus);

    pointer->drag_focus = new_focus;

    if (old_client && old_client != new_client) {
        log_trace("seat_pointer_update_drag - leave");
        seat_post_event(pointer->seat, old_client, ptr_to(SeatEvent {
            .data = {
                .type = SeatEventType::drag_leave,
                .seat = seat,
                .drag = {
                    .focus = old_focus,
                }
            },
        }));
    }

    if (new_client) {
        log_trace("seat_pointer_update_drag - enter");
        seat_post_event(pointer->seat, new_client, ptr_to(SeatEvent {
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

SeatDataSource::~SeatDataSource()
{
    if (seat) {
        if (seat->selection == this) {
            seat->selection = nullptr;
        }

        auto* pointer = seat_get_pointer(seat);

        if (pointer->drag == this) {
            pointer->drag = nullptr;
            update_drag_visual(pointer, nullptr);
        }
    }

    for (auto* offer : offers) {
        offer->source = nullptr;
    }
}
