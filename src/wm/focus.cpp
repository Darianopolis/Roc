#include "internal.hpp"

#include <core/math.hpp>
#include <core/log.hpp>

static
auto in_cycle(WmServer* wm, SeatPointer* pointer, WmWindow* window) -> bool
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
auto next_window_in_cycle(WmServer* wm, SeatPointer* pointer, bool forward, WmWindow* current) -> WmWindow*
{
    if (current && !in_cycle(wm, pointer, current)) {
        current = nullptr;
    }

    auto iter = std::ranges::find(wm->windows, current);
    if (iter == wm->windows.end()) {
        for (auto* window : wm->windows | std::views::reverse) {
            if (!in_cycle(wm, pointer, window)) continue;
            return window;
        }
        return nullptr;
    }

    auto orig = iter;
    for (;;) {
        if (forward) {
            if (iter == wm->windows.begin()) iter = wm->windows.end();
            iter--;
        } else {
            iter++;
            if (iter == wm->windows.end()) iter = wm->windows.begin();
        }

        if (in_cycle(wm, pointer, *iter)) {
            return *iter;
        }

        if (iter == orig) {
            // We wrapped around without finding any surface in cycle
            return nullptr;
        }
    }
}

static
void cycle_next_window(WmServer* wm, SeatPointer* pointer, bool forward)
{
    wm->focus.cycled = next_window_in_cycle(wm, pointer, forward, wm->focus.cycled.get());
}

static
void focus_cycle(WmServer* wm, Seat* seat, SeatPointer* pointer, bool forward)
{
    if (wm->mode != WmInteractionMode::focus_cycle && !forward && pointer) {
        auto* window = next_window_in_cycle(wm, pointer, true, nullptr);
        if (window && window != wm_find_window_for(wm, seat_keyboard_get_focus(seat_get_keyboard(seat)))) {
            wm_focus(wm, window);
            return;
        }
    }

    seat_pointer_set_sticky_focus(seat_get_pointer(seat), true);

    bool extra_cycle = wm->mode != WmInteractionMode::focus_cycle
        && (pointer ||  bool(seat_keyboard_get_focus(seat_get_keyboard(seat))));

    wm_interaction_set_mode(wm, WmInteractionMode::focus_cycle);
    wm->focus.seat = seat;

    cycle_next_window(wm, pointer, forward);
    if (extra_cycle) {
        cycle_next_window(wm, pointer, forward);
    }
    wm_arrange_windows(wm);
}

static
void focus_cycle_end(WmServer* wm)
{
    seat_pointer_set_sticky_focus(seat_get_pointer(wm->focus.seat), false);

    wm_interaction_set_mode(wm, WmInteractionMode::none);

    if (wm->focus.cycled) {
        wm_focus(wm, wm->focus.cycled.get());
    }

    wm->focus.cycled = nullptr;
    wm_arrange_windows(wm);
}

static
auto filter_event(WmServer* wm, SeatEvent* event) -> SeatEventFilterResult
{
    if (wm->mode != WmInteractionMode::none && wm->mode != WmInteractionMode::focus_cycle) return {};

    switch (event->type) {
        break;case SeatEventType::pointer_scroll: {
            if (!event->pointer.scroll.delta.y) return {};

            auto seat = seat_pointer_get_seat(event->pointer.pointer);
            auto mods = seat_get_modifiers(seat);
            if (!mods.contains(wm->main_mod)) return {};

            focus_cycle(wm, seat, event->pointer.pointer, event->pointer.scroll.delta.y < 0);
            return SeatEventFilterResult::capture;
        }
        break;case SeatEventType::keyboard_key: {
            if (!event->keyboard.key.pressed) return {};

            if (event->keyboard.key.code != KEY_TAB) return {};

            auto seat = seat_pointer_get_seat(event->pointer.pointer);
            auto mods = seat_keyboard_get_modifiers(event->keyboard.keyboard);
            if (!mods.contains(wm->main_mod)) return {};

            focus_cycle(wm, seat, nullptr, !mods.contains(SeatModifier::shift));
            return SeatEventFilterResult::capture;
        }
        break;case SeatEventType::keyboard_modifier: {
            auto mods = seat_keyboard_get_modifiers(event->keyboard.keyboard);
            if (wm->mode == WmInteractionMode::focus_cycle && !mods.contains(wm->main_mod)) {
                focus_cycle_end(wm);
            }
        }
        break;case SeatEventType::pointer_motion:
            if (wm->mode == WmInteractionMode::focus_cycle) {
                return SeatEventFilterResult::capture;
            }
        break;case SeatEventType::pointer_button:
            if (wm->mode == WmInteractionMode::focus_cycle && event->pointer.button.pressed) {
                return SeatEventFilterResult::capture;
            }
        break;default:
            ;
    }

    return SeatEventFilterResult::passthrough;
}

// -----------------------------------------------------------------------------

void wm_init_focus_cycle(WmServer* wm)
{
    wm->focus.filter = seat_add_event_filter(wm_get_seat(wm), [wm](SeatEvent* event) {
        return filter_event(wm, event);
    });
}

// -----------------------------------------------------------------------------

void wm_arrange_windows(WmServer* wm)
{
    // TODO: More generic system for adjusting window arrangement

    ankerl::unordered_dense::set<WmWindow*> highlighted;
    if (wm->mode == WmInteractionMode::focus_cycle) {
        for (auto* window : wm->windows) {
            if (window == wm->focus.cycled.get()) {
                while (window) {
                    highlighted.emplace(window);
                    window = window->parent;
                }
                break;
            }
        }
    } else {
        for (auto* window : wm->windows) {
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
    order.reserve(wm->windows.size());

    auto place_windows = [&](this auto&& place_windows, Link<WmWindow>& windows) -> void {
        auto place = [&](WmWindow* window) {
            bool faded = wm->mode == WmInteractionMode::focus_cycle && !highlighted.contains(window);
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

    place_windows(wm->root_windows);

    scene_tree_replace(wm_get_layer(wm, WmLayer::window), order);
}
