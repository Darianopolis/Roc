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
void offer_selection(scene_context* ctx, scene_data_source* source)
{
    for (auto* client : ctx->clients) {
        scene_offer_selection(client, source);
    }
}

void scene_set_selection(scene_context* ctx, scene_data_source* source)
{
    if (ctx->selection) {
        ctx->selection->ops.cancel();
    }
    ctx->selection = source;
    offer_selection(ctx, source);
}

auto scene_get_selection(scene_context* ctx) -> scene_data_source*
{
    return ctx->selection.get();
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

void scene_data_source_send(scene_data_source* source, const char* mime_type, int fd)
{
    log_debug("scene_data_source_send({}, {}, {})", (void*)source, mime_type, fd);
    source->ops.send(mime_type, fd);
}
