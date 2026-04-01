#include "internal.hpp"

SceneClient::~SceneClient()
{
    // TODO: Allow deletion of client resources in any order safely

    debug_assert(input_regions == 0);

    // All client windows must be destroyed before the client
    for (auto* window : scene->windows) {
        debug_assert(window->client != this);
    }

    // Focus must have been dropped before the client can safely be destroyed
    for (auto* seat : scene_get_seats(scene)) {
        debug_assert(scene_get_focus_client(seat->keyboard->focus) != this);
        debug_assert(scene_get_focus_client(seat->pointer->focus)  != this);
    }

    std::erase(scene->clients, this);
}

auto scene_client_create(Scene* scene) -> Ref<SceneClient>
{
    auto client = ref_create<SceneClient>();
    client->scene = scene;
    scene->clients.emplace_back(client.get());
    return client;
}

void scene_client_set_event_handler(SceneClient* client, std::move_only_function<SceneEventHandlerFn>&& event_handler)
{
    client->event_handler = std::move(event_handler);

    for (auto* seat : scene_get_seats(client->scene)) {
        client->event_handler(ptr_to(SceneEvent {
            .type = SceneEventType::seat_add,
            .seat = seat,
        }));
    }
}

void scene_client_post_event(SceneClient* client, SceneEvent* event)
{
    client->event_handler(event);
}
