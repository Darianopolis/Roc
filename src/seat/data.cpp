#include "internal.hpp"

void seat_data_source_offer(SeatDataSource* source, const char* mime_type)
{
    source->offered.insert(mime_type);
}

static
void offer_selection_to_focus(Seat* seat, SeatDataSource* source)
{
    if (auto* focus = seat_keyboard_get_focus(seat->keyboard.get())) {
        seat_offer_selection(seat, focus->client, source);
    }
}

void seat_set_selection(Seat* seat, SeatDataSource* source)
{
    if (seat->selection.get() == source) return;
    if (seat->selection) {
        seat->selection->on_cancel();
    }
    seat->selection = source;
    offer_selection_to_focus(seat, source);
}

auto seat_get_selection(Seat* seat) -> SeatDataSource*
{
    return seat->selection.get();
}

void seat_offer_selection(Seat* seat, SeatClient* client, SeatDataSource* source)
{
    seat_post_event(seat, client, ptr_to(SeatEvent {
        .data {
            .type = SeatEventType::selection,
            .source = source,
        },
    }));
}

auto seat_data_source_get_offered(SeatDataSource* source) -> std::span<const std::string>
{
    return source->offered;
}

// -----------------------------------------------------------------------------

void seat_data_source_receive(SeatDataSource* source, const char* mime_type, fd_t fd)
{
    log_debug("seat_data_source_send({}, {}, {})", (void*)source, mime_type, fd);
    source->on_send(mime_type, fd);
}
