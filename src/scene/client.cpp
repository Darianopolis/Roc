#include "internal.hpp"

CORE_OBJECT_EXPLICIT_DEFINE(scene_client);

scene_client::~scene_client()
{
    core_assert(input_regions == 0);

    // All client windows must be destroyed before the client
    for (auto* window : ctx->windows) {
        core_assert(window->client != this);
    }

    if (auto* kb = scene_get_keyboard(ctx); kb->focus.client == this) {
        scene_keyboard_set_focus(kb, {});
    }

    if (auto* ptr = scene_get_pointer(ctx); ptr->focus.client == this) {
        scene_pointer_set_focus(ptr, {});
    }

    std::erase(ctx->clients, this);
}

auto scene_client_create(scene_context* ctx) -> ref<scene_client>
{
    auto client = core_create<scene_client>();
    client->ctx = ctx;
    ctx->clients.emplace_back(client.get());
    return client;
}

void scene_client_set_event_handler(scene_client* client, std::move_only_function<scene_event_handler_fn>&& event_handler)
{
    client->event_handler = std::move(event_handler);
}

void scene_client_post_event(scene_client* client, scene_event* event)
{
    client->event_handler(event);
}
