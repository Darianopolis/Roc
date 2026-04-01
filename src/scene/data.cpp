#include "internal.hpp"

auto scene_data_source_create(SceneClient* client, SceneDataSourceOps&& ops) -> Ref<SceneDataSource>
{
    auto source = ref_create<SceneDataSource>();
    source->ops = std::move(ops);
    source->client = client;
    return source;
}

void scene_data_source_offer(SceneDataSource* source, const char* mime_type)
{
    source->offered.insert(mime_type);
}

static
void offer_selection(SceneSeat* seat, SceneDataSource* source)
{
    // TODO: Only offer to clients with focus
    for (auto* client : seat->scene->clients) {
        scene_offer_selection(client, source);
    }
}

void scene_seat_set_selection(SceneSeat* seat, SceneDataSource* source)
{
    if (seat->selection) {
        seat->selection->ops.cancel();
    }
    seat->selection = source;
    offer_selection(seat, source);
}

auto scene_seat_get_selection(SceneSeat* seat) -> SceneDataSource*
{
    return seat->selection.get();
}

SceneDataSource::~SceneDataSource()
{
}

void scene_offer_selection(SceneClient* client, SceneDataSource* source)
{
    scene_client_post_event(client, ptr_to(SceneEvent {
        .type = SceneEventType::selection,
        .data {
            .source =source,
        },
    }));
}

auto scene_data_source_get_offered(SceneDataSource* source) -> std::span<const std::string>
{
    return source->offered;
}

// -----------------------------------------------------------------------------

void scene_data_source_receive(SceneDataSource* source, const char* mime_type, int fd)
{
    log_debug("scene_data_source_send({}, {}, {})", (void*)source, mime_type, fd);
    source->ops.send(mime_type, fd);
}
