#include "internal.hpp"

#include <core/math.hpp>
#include <core/log.hpp>

static
auto in_cycle(WmServer* server, SeatPointer* pointer, WmWindow* window) -> bool
{
    if (!window->mapped) return false;

    // Consider only leaf windows
    if (window->children.is_linked()) return false;

    if (!pointer) return true;

    // Consider window and all parent windows for cursor intersection
    for (WmWindow* w = window; w; w = w->parent) {
        if (rect_contains(wm_window_get_frame(w), seat_pointer_get_position(pointer))) {
            return true;
        }
    }

    return false;
}

static
auto next_window_in_cycle(WmServer* server, SeatPointer* pointer, bool forward, WmWindow* current) -> WmWindow*
{
    if (current && !in_cycle(server, pointer, current)) {
        current = nullptr;
    }

    auto iter = std::ranges::find(server->windows, current);
    if (iter == server->windows.end()) {
        for (auto* window : server->windows | std::views::reverse) {
            if (!in_cycle(server, pointer, window)) continue;
            return window;
        }
        return nullptr;
    }

    auto orig = iter;
    for (;;) {
        if (forward) {
            if (iter == server->windows.begin()) iter = server->windows.end();
            iter--;
        } else {
            iter++;
            if (iter == server->windows.end()) iter = server->windows.begin();
        }

        if (in_cycle(server, pointer, *iter)) {
            return *iter;
        }

        if (iter == orig) {
            // We wrapped around without finding any surface in cycle
            return nullptr;
        }
    }
}

static
void cycle_next_window(WmServer* server, SeatPointer* pointer, bool forward)
{
    server->focus.cycled = next_window_in_cycle(server, pointer, forward, server->focus.cycled.get());
}

static
void focus_cycle(WmServer* server, Seat* seat, SeatPointer* pointer, bool forward)
{
    if (server->mode != WmInteractionMode::focus_cycle && !forward && pointer) {
        auto* window = next_window_in_cycle(server, pointer, true, nullptr);
        if (window && window != wm_find_window_for(server, seat_keyboard_get_focus(seat_get_keyboard(seat)))) {
            wm_focus(server, window);
            return;
        }
    }

    seat_pointer_set_sticky_focus(seat_get_pointer(seat), true);

    bool extra_cycle = server->mode != WmInteractionMode::focus_cycle
        && (pointer ||  bool(seat_keyboard_get_focus(seat_get_keyboard(seat))));

    wm_interaction_set_mode(server, WmInteractionMode::focus_cycle);
    server->focus.seat = seat;

    cycle_next_window(server, pointer, forward);
    if (extra_cycle) {
        cycle_next_window(server, pointer, forward);
    }
    wm_arrange_windows(server);
}

static
void focus_cycle_end(WmServer* server)
{
    seat_pointer_set_sticky_focus(seat_get_pointer(server->focus.seat), false);

    wm_interaction_set_mode(server, WmInteractionMode::none);

    if (server->focus.cycled) {
        wm_focus(server, server->focus.cycled.get());
    }

    server->focus.cycled = nullptr;
    wm_arrange_windows(server);
}

static
auto filter_event(WmServer* server, SeatEvent* event) -> SeatEventFilterResult
{
    if (server->mode != WmInteractionMode::none && server->mode != WmInteractionMode::focus_cycle) return {};

    switch (event->type) {
        break;case SeatEventType::pointer_scroll: {
            if (!event->pointer.scroll.delta.y) return {};

            auto seat = seat_pointer_get_seat(event->pointer.pointer);
            auto mods = seat_get_modifiers(seat);
            if (!mods.contains(server->main_mod)) return {};

            focus_cycle(server, seat, event->pointer.pointer, event->pointer.scroll.delta.y < 0);
            return SeatEventFilterResult::capture;
        }
        break;case SeatEventType::keyboard_key: {
            if (!event->keyboard.key.pressed) return {};

            if (event->keyboard.key.code != KEY_TAB) return {};

            auto seat = seat_pointer_get_seat(event->pointer.pointer);
            auto mods = seat_keyboard_get_modifiers(event->keyboard.keyboard);
            if (!mods.contains(server->main_mod)) return {};

            focus_cycle(server, seat, nullptr, !mods.contains(SeatModifier::shift));
            return SeatEventFilterResult::capture;
        }
        break;case SeatEventType::keyboard_modifier: {
            auto mods = seat_keyboard_get_modifiers(event->keyboard.keyboard);
            if (server->mode == WmInteractionMode::focus_cycle && !mods.contains(server->main_mod)) {
                focus_cycle_end(server);
            }
        }
        break;case SeatEventType::pointer_motion:
            if (server->mode == WmInteractionMode::focus_cycle) {
                return SeatEventFilterResult::capture;
            }
        break;case SeatEventType::pointer_button:
            if (server->mode == WmInteractionMode::focus_cycle && event->pointer.button.pressed) {
                if (server->focus.cycled && !rect_contains(wm_window_get_frame(server->focus.cycled.get()),
                                                           seat_pointer_get_position(event->pointer.pointer))) {
                    server->focus.cycled = nullptr;
                }
                focus_cycle_end(server);
                return SeatEventFilterResult::capture;
            }
        break;default:
            ;
    }

    return SeatEventFilterResult::passthrough;
}

// -----------------------------------------------------------------------------

void wm_init_focus_cycle(WmServer* server)
{
    server->focus.filter = seat_add_event_filter(wm_get_seat(server), [server](SeatEvent* event) {
        return filter_event(server, event);
    });
}

// -----------------------------------------------------------------------------

void wm_arrange_windows(WmServer* server)
{
    // TODO: More generic system for adjusting window arrangement

    ankerl::unordered_dense::set<WmWindow*> highlighted;
    if (server->mode == WmInteractionMode::focus_cycle) {
        for (auto* window : server->windows) {
            if (window == server->focus.cycled.get()) {
                while (window) {
                    highlighted.emplace(window);
                    window = window->parent;
                }
                break;
            }
        }
    } else {
        for (auto* window : server->windows) {
            if (wm_window_is_focused(window)) {
                while (window) {
                    highlighted.emplace(window);
                    window = window->parent;
                }
                break;
            }
        }
    }

    std::vector<SceneNode*> order;
    order.reserve(server->windows.size());

    auto place_windows = [&](this auto&& place_windows, Link<WmWindow>& windows) -> void {
        auto place = [&](WmWindow* window) {
            bool faded = server->mode == WmInteractionMode::focus_cycle && !highlighted.contains(window);
            f32 opacity = faded ? 0.1f : 1.f;
            scene_tree_set_opacity(window->root_tree.get(), opacity);

            order.emplace_back(window->root_tree.get());
            place_windows(window->children);
        };

        WmWindow* last = nullptr;
        for (auto l = windows.next; l != &windows; l = l->next) {
            auto* window = LINK_GET(WmWindow, link, l);
            if (!window->mapped) continue;
            if (highlighted.contains(window)) {
                last = window;
                continue;
            }

            place(window);
        }
        if (last) place(last);
    };

    place_windows(server->root_windows);

    scene_tree_replace(wm_get_layer(server, WmLayer::window), order);
}
