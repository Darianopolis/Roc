#include "internal.hpp"

auto seat_data_source_create(SeatClient* client, SeatDataSourceOps&& ops) -> Ref<SeatDataSource>
{
    auto source = ref_create<SeatDataSource>();
    source->ops = std::move(ops);
    source->client = client;
    return source;
}

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
    if (seat->selection) {
        seat->selection->ops.cancel();
    }
    seat->selection = source;
    offer_selection_to_focus(seat, source);
}

auto seat_get_selection(Seat* seat) -> SeatDataSource*
{
    return seat->selection.get();
}

SeatDataSource::~SeatDataSource()
{
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

void seat_data_source_receive(SeatDataSource* source, const char* mime_type, int fd)
{
    log_debug("seat_data_source_send({}, {}, {})", (void*)source, mime_type, fd);
    source->ops.send(mime_type, fd);
}
