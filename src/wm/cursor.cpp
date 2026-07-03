#include "internal.hpp"

#include <core/log.hpp>

static
auto find_client(WmServer* server, SeatFocus* focus) -> WmClient*
{
    if (!focus) return nullptr;
    for (auto* client : server->clients) {
        if (client->seat_client.get() == focus->client) {
            return client;
        }
    }
    return nullptr;
}

static
void update_seat_cursor_visual_for_client(WmServer* server, Seat* seat, SeatPointer* pointer)
{
    auto* client = find_client(server, seat_pointer_get_focus(pointer));
    if (!client) {
        seat_pointer_set_xcursor(pointer, "default");
        return;
    }

    std::visit(OverloadSet {
        [&](WmCursorUnset) {
            seat_pointer_set_xcursor(pointer, "default");
        },
        [&](Weak<SceneNode> node) {
            if (node) {
                seat_pointer_set_cursor(pointer, node.get());
            } else if (find_client(server, seat_keyboard_get_focus(seat_get_keyboard(seat))) == client) {
                seat_pointer_set_cursor(pointer, nullptr);
            } else {
                // Client may only hide cursor when focused
                seat_pointer_set_xcursor(pointer, "default");
            }
        },
        [&](const std::string& name) {
            seat_pointer_set_xcursor(pointer, name.c_str());
        }
    }, client->cursor);
}

static
void update_seat_cursor_visual(WmServer* server, Seat* seat)
{
    auto* pointer = seat_get_pointer(seat);

    switch (server->mode) {
        break;case WmInteractionMode::none:
            update_seat_cursor_visual_for_client(server, seat, pointer);
        break;case WmInteractionMode::move:
            seat_pointer_set_xcursor(pointer, "move");
        break;case WmInteractionMode::size:
            if (server->movesize.relative.x == 0) {
                seat_pointer_set_xcursor(pointer, "ns-resize");
            } else if (server->movesize.relative.y == 0) {
                seat_pointer_set_xcursor(pointer, "ew-resize");
            } else if (server->movesize.relative.x == server->movesize.relative.y) {
                seat_pointer_set_xcursor(pointer, "nwse-resize");
            } else {
                seat_pointer_set_xcursor(pointer, "nesw-resize");
            }
        break;case WmInteractionMode::zone:
            if (server->zone.selecting) {
                seat_pointer_set_xcursor(pointer, "grabbing");
            } else {
                seat_pointer_set_xcursor(pointer, "grab");
            }
        break;case WmInteractionMode::focus_cycle:
            seat_pointer_set_xcursor(pointer, "default");
        break;case WmInteractionMode::selection:
            seat_pointer_set_xcursor(pointer, "crosshair");
    }
}

#define WM_NOISY_CURSOR_IMAGE 0

void wm_prepare_cursor_image(WmServer* server)
{
    static const vec2u32 extent { 64, 64 };
    static const auto format = gpu_format_from_drm(DRM_FORMAT_ARGB8888);
    static const GpuFormatModifierSet modifiers = {DRM_FORMAT_MOD_LINEAR};

    auto* pointer = seat_get_pointer(wm_get_seat(server));
    auto* pointer_tree = seat_pointer_get_tree(pointer);

    if (!server->cursor_needs_redraw) return;

#if WM_NOISY_CURSOR_IMAGE
    log_warn("Updating cursor image");
#endif

    server->cursor_needs_redraw = false;

    {
        aabb2f32 bounds{{INFINITY, INFINITY}, {-INFINITY, -INFINITY}, minmax};
        [&](this auto&& visit, SceneNode* node, vec2f32 translation) -> void {
            scene_visit(node, OverloadSet {
                [&](SceneTexture* texture) {
                    auto dst = texture->dst;
                    dst.origin += translation;
                    bounds = aabb_outer<f32>(bounds, dst);
                },
                [&](SceneTree* tree) {
                    if (!tree->enabled) return;
                    translation += tree->translation;
                    for (auto* child : tree->children) {
                        visit(child, translation);
                    }
                },
                [&](SceneInputRegion*) {}
            });
        }(pointer_tree, {});
#if WM_NOISY_CURSOR_IMAGE
        log_warn("  bounds: {}", bounds);
#endif

        server->cursor_image_bounds = bounds;
    }

    if (server->cursor_image_bounds.extent.x > extent.x || server->cursor_image_bounds.extent.y > extent.y) {
        log_warn("Cursor tree bounds {} are greater than cursor plane max extent {} - falling back to composition", server->cursor_image_bounds, extent);
        server->cursor_image_valid = false;
        return;
    }

    server->cursor_image = server->image_pool->acquire(GpuImageCreateInfo {
        .extent = extent,
        .format = format,
        .usage = GpuImageUsage::render,
        .flags = GpuImageFlag::host,
        .modifiers = &modifiers,
    });

    server->cursor_image_valid = true;

    if (server->cursor_image_bounds.origin == vec2f32{INFINITY, INFINITY}) {
        gpu_render(server->gpu, GpuRenderPassInfo {
            .target = server->cursor_image.get(),
            .clear_color = vec4f32{0.f, 0.f, 0.f, 0.f},
            .reads = ptr_to(ankerl::unordered_dense::set<void*>{server->cursor_image.get()}),
            .writes = ptr_to(ankerl::unordered_dense::set<void*>{server->cursor_image.get()}),
        }, [](GpuRenderPass*) {});
        server->cursor_image_bounds = {seat_pointer_get_position(pointer), {}, xywh};
    } else {
        scene_render(server->scene_renderer.get(), {
            .root = pointer_tree,
            .target = server->cursor_image.get(),
            .viewport = {server->cursor_image_bounds.origin, vec_cast<f32>(extent), xywh},
        });
    }
}

void wm_cursor_visual_update(WmServer* server)
{
    for (auto* seat : wm_get_seats(server)) {
        update_seat_cursor_visual(server, seat);
    }
}

void wm_cursor_init(WmServer* server)
{
    for (auto* seat : server->seats) {
        server->cursor_event_filter.emplace_back(seat_add_event_filter(seat, [server](SeatEvent* event) -> SeatEventFilterResult {
            if (       event->type == SeatEventType::keyboard_enter || event->type == SeatEventType::keyboard_leave
                    || event->type == SeatEventType::pointer_enter  || event->type == SeatEventType::pointer_leave) {
                wm_cursor_visual_update(server);
                exec_enqueue(server->exec, [server = Weak(server)] {
                    if (!server) return;
                    wm_cursor_visual_update(server.get());
                });
            }
            return SeatEventFilterResult::passthrough;
        }));
    }

    server->cursor_damage_listener = seat_pointer_get_tree(seat_get_pointer(wm_get_seat(server)))->signals.damage.listen(
        [server](vec2f32 offset, const SceneDamage& damage) {
            for (auto* output : server->io.outputs) {
                output->cursor_damaged = true;
            }
            server->cursor_needs_redraw = true;
            exec_enqueue(server->exec, [server = Weak(server)] {
                if (!server) return;
                wm_cursor_visual_update(server.get());
            });
        });

    server->cursor_needs_redraw = true;
    wm_cursor_visual_update(server);
}

void wm_set_cursor(WmClient* client, WmCursorVisual visual)
{
    if (client->cursor != visual) {
        client->cursor = std::move(visual);
        wm_cursor_visual_update(client->server);
    }
}
