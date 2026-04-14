#include "internal.hpp"

SeatClient::~SeatClient()
{
    // TODO: Allow deletion of client resources in any order safely

    debug_assert(input_regions == 0);
}

auto seat_client_create() -> Ref<SeatClient>
{
    auto client = ref_create<SeatClient>();
    return client;
}

void seat_client_set_event_handler(SeatClient* client, std::move_only_function<SeatEventHandlerFn>&& event_handler)
{
    client->event_handler = std::move(event_handler);
}

void seat_client_post_event(SeatClient* client, SeatEvent* event)
{
    client->event_handler(event);
}
