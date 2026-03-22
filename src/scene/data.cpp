#include "internal.hpp"

auto scene_data_source_create(scene_client* client, scene_data_source_ops&& ops) -> ref<scene_data_source>
{
    auto source = core_create<scene_data_source>();
    source->ops = std::move(ops);
    source->client = client;
    return source;
}

void scene_data_source_offer(scene_data_source* source, const char* mime_type)
{
    source->offered.insert(mime_type);
}

static
void offer_selection(scene_seat* seat, scene_data_source* source)
{
    // TODO: Only offer to clients with focus
    for (auto* client : seat->ctx->clients) {
        scene_offer_selection(client, source);
    }
}

void scene_seat_set_selection(scene_seat* seat, scene_data_source* source)
{
    if (seat->selection) {
        seat->selection->ops.cancel();
    }
    seat->selection = source;
    offer_selection(seat, source);
}

auto scene_seat_get_selection(scene_seat* seat) -> scene_data_source*
{
    return seat->selection.get();
}

scene_data_source::~scene_data_source()
{
}

void scene_offer_selection(scene_client* client, scene_data_source* source)
{
    scene_client_post_event(client, ptr_to(scene_event {
        .type = scene_event_type::selection,
        .data {
            .source =source,
        },
    }));
}

auto scene_data_source_get_offered(scene_data_source* source) -> std::span<const std::string>
{
    return source->offered;
}

// -----------------------------------------------------------------------------

void scene_data_source_receive(scene_data_source* source, const char* mime_type, int fd)
{
    log_debug("scene_data_source_send({}, {}, {})", (void*)source, mime_type, fd);
    source->ops.send(mime_type, fd);
}
