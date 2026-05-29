#include "internal.hpp"

#include <core/math.hpp>
#include <core/color.hpp>
#include <core/log.hpp>

WmWindow::~WmWindow()
{
    wm_window_post_event(ptr_to(WmWindowEvent {
        .type = WmEventType::window_destroyed,
        .window = this,
    }));

    root_tree->userdata = {};
    wm_window_unmap(this);
    std::erase(client->wm->windows, this);
}

static constexpr vec2f32 border_size    = vec2f32(2, 2);
static constexpr auto    border_normal  = color_from_hex("#4C4C4C");
static constexpr auto    border_focused = color_from_hex("#6666FF");

auto wm_window_create(WmClient* client) -> Ref<WmWindow>
{
    auto* wm = client->wm;

    auto window = ref_create<WmWindow>();
    window->client = client;

    wm->windows.emplace_back(window.get());

    window->root_tree = scene_tree_create();
    window->root_tree->userdata = {wm->window_system_id, window.get()};

    window->borders = scene_texture_create();
    scene_tree_place_above(window->root_tree.get(), nullptr, window->borders.get());
    scene_texture_set_tint(window->borders.get(), border_normal);

    window->client_tree = scene_tree_create();
    scene_tree_place_above(window->root_tree.get(), nullptr, window->client_tree.get());

    wm_window_post_event(ptr_to(WmWindowEvent {
        .type = WmEventType::window_created,
        .window = window.get(),
    }));

    return window;
}

auto wm_window_get_tree(WmWindow* window) -> SceneTree*
{
    return window->client_tree.get();
}

void wm_window_set_title(WmWindow* window, std::string_view title)
{
    window->title = title;
}

void wm_window_set_app_id(WmWindow* window, std::string_view app_id)
{
    window->app_id = app_id;
}

void wm_window_post_event(WmWindowEvent* event)
{
    wm_client_post_event(event->window->client, reinterpret_cast<WmEvent*>(event));
}

void wm_window_request_reposition(WmWindow* window, rect2f32 frame, vec2f32 gravity)
{
    window->relative = 1.f - ((gravity + 1.f) * 0.5f);
    window->anchor = frame.origin + (frame.extent * window->relative);

    wm_window_post_event(ptr_to(WmWindowEvent {
        .type = WmEventType::window_request_resize,
        .window = window,
        .size = frame.extent,
    }));
}

void wm_window_request_close(WmWindow* window)
{
    wm_window_post_event(ptr_to(WmWindowEvent {
        .type = WmEventType::window_request_close,
        .window = window,
    }));
}

void wm_window_set_size(WmWindow* window, vec2f32 size)
{
    window->extent = size;

    vec2f32 origin = window->anchor - (size * window->relative);

    if (window->oneshot_output_constraint) {
        aabb2f32 constraint = wm_output_get_workarea(window->oneshot_output_constraint.get());
        aabb2f32 constrained = aabb_constrain<f32>({origin, size, xywh}, constraint);
        window->oneshot_output_constraint = nullptr;

        origin = constrained.min;

        bool constrained_left   = constrained.min.x == constraint.min.x;
        bool constrained_top    = constrained.min.y == constraint.min.y;
        bool constrained_right  = constrained.max.x == constraint.max.x;
        bool constrained_bottom = constrained.max.y == constraint.max.y;

        // Update anchor and relative based on what edges are constrained

        window->relative.x = (0.5f * (f32(constrained_right) - f32(constrained_left))) + 0.5f;
        window->relative.y = (constrained_bottom && !constrained_top) ? 1 : 0;

        rect2f32 rect = constrained;
        window->anchor = rect.origin + (rect.extent * window->relative);
    }

    scene_tree_set_translation(window->root_tree.get(), origin);
    scene_texture_set_dst(window->borders.get(), {-border_size, size + border_size * 2.f, xywh});
}

auto wm_window_get_frame(WmWindow* window) -> rect2f32
{
    return {
        scene_tree_get_position(window->root_tree.get()),
        window->extent,
        xywh
    };
}

void wm_window_map(WmWindow* window)
{
    if (window->mapped) return;

    window->mapped = true;
    wm_arrange_windows(window->client->wm);

    wm_window_post_event(ptr_to(WmWindowEvent {
        .type = WmEventType::window_mapped,
        .window = window,
    }));
}

auto wm_window_is_mapped(WmWindow* window) -> bool
{
    return window->mapped;
}

void wm_window_raise(WmWindow* window)
{
    if (!window->mapped) return;

    auto* wm = window->client->wm;

    std::erase(wm->windows, window);
    wm->windows.emplace_back(window);

    wm_arrange_windows(wm);
}

static
void try_revert_focus(WmServer* wm, WmWindow* window)
{
    for (auto* seat : wm_get_seats(wm)) {
        auto* keyboard = seat_get_keyboard(seat);
        if (seat_focus_contains(window->focus.get(), seat_keyboard_get_focus(keyboard))) {
            // TODO: Refactor to share this logic with `focus.cpp`
            bool found_new_focus = false;
            for (auto* new_window : wm->windows | std::views::reverse) {
                if (new_window == window) continue;
                if (!new_window->mapped) continue;
                seat_keyboard_focus(keyboard, new_window->focus.get());
                found_new_focus = true;
                break;
            }
            if (!found_new_focus) {
                seat_keyboard_focus(keyboard, nullptr);
            }
        }

        auto* pointer = seat_get_pointer(seat);
        seat_pointer_move(pointer, seat_pointer_get_position(pointer), {}, {});
    }
}

void wm_window_unmap(WmWindow* window)
{
    if (!window->mapped) return;

    auto* wm = window->client->wm;

    window->mapped = false;
    wm_arrange_windows(wm);

    try_revert_focus(wm, window);

    wm_window_post_event(ptr_to(WmWindowEvent {
        .type = WmEventType::window_unmapped,
        .window = window,
    }));
}

// -----------------------------------------------------------------------------

static
void update_border_colors(WmServer* wm)
{
    for (auto* w : wm->windows) {
        scene_texture_set_tint(w->borders.get(), wm_window_is_focused(w) ? border_focused : border_normal);
    }
}

void wm_decoration_init(WmServer* wm)
{
    for (auto* seat : wm->seats) {
        wm->decoration.filter.emplace_back(seat_add_event_filter(seat, [wm](SeatEvent* event) -> SeatEventFilterResult {
            if (event->type == SeatEventType::keyboard_enter || event->type == SeatEventType::keyboard_leave) {
                update_border_colors(wm);
            }
            return SeatEventFilterResult::passthrough;
        }));
    }
}

// -----------------------------------------------------------------------------

auto wm_find_window_at(WmServer* wm, vec2f32 point) -> WmWindow*
{
    // TODO: This will ignore any `input_plane`s currently.
    //       Should we provide (optional) mappings from `input_plane` back to windows
    //       and then intersect against both `input_plane`s and `window` frames?

    WmWindow* window = nullptr;

    scene_iterate<SceneIterateDirection::front_to_back>(
        wm_get_layer(wm, WmLayer::window),
        scene_iterate_default,
        scene_iterate_default,
        [&](SceneTree* tree) {
            if (tree->userdata.id == wm->window_system_id) {
                auto w = static_cast<WmWindow*>(tree->userdata.data);
                if (rect_contains(wm_window_get_frame(w), point)) {
                    window = w;
                    return SceneIterateAction::stop;
                }
            }
            return SceneIterateAction::next;
        });

    return window;
}

// -----------------------------------------------------------------------------

void wm_window_set_focus(WmWindow* window, SeatFocus* focus)
{
    window->focus = focus;
}

void wm_window_focus(WmWindow* window)
{
    auto* wm = window->client->wm;

    auto* keyboard = seat_get_keyboard(wm_get_seat(wm));
    if (keyboard) {
        seat_keyboard_focus(keyboard, window->focus.get());
    }
    wm_window_raise(window);
}

auto wm_window_is_focused(WmWindow* window) -> bool
{
    auto* wm = window->client->wm;
    return std::ranges::any_of(wm->seats, [&](auto* seat) {
        auto* focus = seat_keyboard_get_focus(seat_get_keyboard(seat));
        if (seat_focus_contains(window->focus.get(), focus)) {
            return true;
        }
        return false;
    });;
}

auto wm_find_window_for(WmServer* wm, SeatFocus* focus) -> WmWindow*
{
    for (auto* window : wm->windows) {
        if (seat_focus_contains(window->focus.get(), focus)) {
            return window;
        }
    }
    return nullptr;
}

// -----------------------------------------------------------------------------

void wm_window_set_fullscreen(WmWindow* window, WmOutput* output)
{
    if (output == window->fullscreen.output) return;

    if (!window->fullscreen.output) {
        window->fullscreen.restore = {scene_tree_get_position(window->root_tree.get()), window->extent, xywh};
    }

    bool last_output = std::exchange(window->fullscreen.output, output);

    if (output) {
        scene_node_unparent(window->borders.get());
        wm_window_request_reposition(window, wm_output_get_viewport(output), vec2f32{1, 1});
    } else if (last_output) {
        scene_tree_place_below(window->root_tree.get(), nullptr, window->borders.get());
        wm_window_request_reposition(window, window->fullscreen.restore, vec2f32{1, 1});
    }
}

auto wm_window_get_fullscreen(WmWindow* window) -> WmOutput*
{
    return window->fullscreen.output;
}

auto wm_window_is_movable(WmWindow* window) -> bool
{
    return !window->fullscreen.output;
}

auto wm_window_is_resizable(WmWindow* window) -> bool
{
    return !window->fullscreen.output;
}

// -----------------------------------------------------------------------------

auto wm_window_place_auto(WmWindow* window) -> vec2f32
{
    auto* wm = window->client->wm;

    vec2f32 center_pos;

    if (window->parent) {
        // Child, position at center of parent.
        auto parent_bounds = wm_window_get_frame(window->parent);
        center_pos = parent_bounds.origin + (parent_bounds.extent) / 2.f;
    } else {
        // Non-child, spawn under mouse
        auto* seat = wm_get_seat(wm);
        auto cursor_pos = seat_pointer_get_position(seat_get_pointer(seat));
        center_pos = cursor_pos;
    }

    window->anchor = center_pos;
    window->relative = {0.5f, 0.5f};

    auto output = wm_find_output_at(wm, center_pos).output;
    window->oneshot_output_constraint = output;

    return wm_output_get_workarea(output).extent;
}
