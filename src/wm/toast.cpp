#include "internal.hpp"

#include <core/log.hpp>

static constexpr f32 line_height = 72;

static
void arrange_toasts(WmServer* server)
{
    vec2f32 position = vec_cast<f32>(server->config.workarea.padding.tl);

    for (auto& toast : server->toasts) {
        position.y += line_height;
        scene_tree_set_translation(toast.string.tree.get(), position);
    }
}

void wm_toast(WmServer* server, std::string_view message, std::chrono::steady_clock::time_point expiration)
{
    if (!server->toast_font) {
        server->toast_font = ui_font_load("/usr/share/fonts/noto/NotoSans-Regular.ttf", line_height);
    }

    if (!server->toast_timer) {
        server->toast_timer = timer_create(server->exec);
    }

    server->toasts.clear();

    auto& toast = server->toasts.emplace_back();
    toast.string = ui_string(server->gpu, server->toast_font.get(), message, {
        .color = {0, 1, 0, 1},
        .border {
            .color = {0, 0, 0, 1},
            .width = 2.f,
        },
    });
    toast.expiration = expiration;

    arrange_toasts(server);
    scene_tree_place_above(wm_get_layer(server, WmLayer::overlay), nullptr, toast.string.tree.get());

    timer_enqueue(server->toast_timer.get(), expiration, [server] {
        auto now = std::chrono::steady_clock::now();
        if (std::erase_if(server->toasts, [&](const auto& toast) {
            return toast.expiration <= now;
        })) {
            arrange_toasts(server);
        }
    });
}
