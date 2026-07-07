#include "internal.hpp"

#include <core/math.hpp>
#include <core/color.hpp>
#include <core/log.hpp>

static
void set_parent_impl(WmWindow* window, WmWindow* parent)
{
    if (window->parent == parent) return;

    auto* server = window->client->server;

    window->parent = parent;

    if (parent) {
        parent->children.link_prev(&window->link);
    } else {
        server->root_windows.link_prev(&window->link);
    }
}

static
void flatten_window_list(WmServer* server)
{
    server->windows.clear();
    auto append = [&](this auto&& append, WmWindow* window) -> void {
        server->windows.emplace_back(window);
        for (auto* l = window->children.next; l != &window->children; l = l->next) {
            auto* child = LINK_GET(WmWindow, link, l);
            append(child);
        }
    };
    for (auto* l = server->root_windows.next; l != &server->root_windows; l = l->next) {
        auto* window = LINK_GET(WmWindow, link, l);
        append(window);
    }
}

WmWindow::~WmWindow()
{
    wm_window_post_event(ptr_to(WmWindowEvent {
        .type = WmEventType::window_destroyed,
        .window = this,
    }));

    while (children.is_linked()) {
        set_parent_impl(LINK_GET(WmWindow, link, children.next), parent);
    }

    link.unlink();
    flatten_window_list(client->server);

    root_tree->userdata = {};
    wm_window_unmap(this);
}

static
void update_border_colors(WmWindow* window)
{
    auto* server = window->client->server;
    for (auto* border : window->borders) {
        scene_texture_set_tint(border, wm_window_is_focused(window)
            ? server->config.border.focused
            : server->config.border.normal);
    }
}

auto wm_window_create(WmClient* client) -> Ref<WmWindow>
{
    auto* server = client->server;

    auto window = ref_create<WmWindow>();
    window->client = client;

    window->root_tree = scene_tree_create();
    window->root_tree->userdata = {server->window_system_id, window.get()};

    window->backdrop = scene_texture_create();
    scene_tree_place_above(window->root_tree.get(), nullptr, window->backdrop.get());
    scene_texture_set_tint(window->backdrop.get(), color_from_hex("#000000"));

    for (usz i = 0; i < 4; ++i) {
        auto border = scene_texture_create();
        scene_tree_place_above(window->root_tree.get(), nullptr, border.get());
        window->borders.emplace_back(border.get());
    }

    update_border_colors(window.get());

    wm_window_post_event(ptr_to(WmWindowEvent {
        .type = WmEventType::window_created,
        .window = window.get(),
    }));

    server->root_windows.link_prev(&window->link);
    flatten_window_list(server);

    return window;
}

void wm_window_set_parent(WmWindow* window, WmWindow* parent)
{
    set_parent_impl(window, parent);

    auto* server = window->client->server;
    flatten_window_list(server);
    wm_arrange_windows(server);
}

void wm_window_set_content(WmWindow* window, SceneNode* node)
{
    scene_tree_place_above(window->root_tree.get(), window->backdrop.get(), node);
}

void wm_window_set_overlay(WmWindow* window, SceneNode* node)
{
    scene_tree_place_above(window->root_tree.get(), nullptr, node);
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

static
void reposition(WmWindow* window)
{
    vec2f32 origin = window->anchor - (window->extent * window->relative);

    if (window->output_constraint && !window->fullscreen.output) {
        aabb2f32 constraint = wm_output_get_workarea(window->output_constraint.get());
        aabb2f32 constrained = aabb_constrain<f32>({origin, window->extent, xywh}, constraint);

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
}

void wm_window_request_reposition(WmWindow* window, rect2f32 frame, vec2f32 gravity)
{
    window->relative = 1.f - ((gravity + 1.f) * 0.5f);
    window->anchor = frame.origin + (frame.extent * window->relative);

    window->output_constraint = nullptr;

    wm_window_post_event(ptr_to(WmWindowEvent {
        .type = WmEventType::window_request_resize,
        .window = window,
        .size = vec_round(frame.extent),
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
    if (window->extent != size) {
        window->extent = size;

        scene_texture_set_dst(window->backdrop.get(), {{}, size, minmax});

        auto bs = window->client->server->config.border.size;
        scene_texture_set_dst(window->borders[0 /* left   */], {{ -bs.x,     0}, {            0, size.y       }, minmax});
        scene_texture_set_dst(window->borders[1 /* right  */], {{size.x,     0}, {size.x + bs.x, size.y       }, minmax});
        scene_texture_set_dst(window->borders[2 /* top    */], {{-bs.x,  -bs.y}, {size.x + bs.x,              }, minmax});
        scene_texture_set_dst(window->borders[3 /* bottom */], {{-bs.x, size.y}, {size.x + bs.x, size.y + bs.y}, minmax});
    }

    reposition(window);
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
    wm_arrange_windows(window->client->server);

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

    auto raise = [](this auto&& raise, WmWindow* window) -> void {
        if (window->parent) {
            window->parent->children.link_prev(&window->link);
            raise(window->parent);
        } else {
            auto* server = window->client->server;
            server->root_windows.link_prev(&window->link);
        }
    };

    raise(window);
    auto* server = window->client->server;
    flatten_window_list(server);
    wm_arrange_windows(server);
}

static
void try_revert_focus(WmServer* server, WmWindow* window)
{
    if (!wm_window_is_focused(window)) return;

    for (auto* new_window : server->windows | std::views::reverse) {
        if (new_window == window || !new_window->mapped) continue;
        wm_focus(server, new_window);
        break;
    }
}

void wm_window_unmap(WmWindow* window)
{
    if (!window->mapped) return;

    auto* server = window->client->server;

    window->mapped = false;
    wm_arrange_windows(server);

    try_revert_focus(server, window);

    wm_window_post_event(ptr_to(WmWindowEvent {
        .type = WmEventType::window_unmapped,
        .window = window,
    }));
}

// -----------------------------------------------------------------------------

static
void update_border_colors(WmServer* server)
{
    for (auto* w : server->windows) {
        update_border_colors(w);
    }
}

void wm_decoration_init(WmServer* server)
{
    for (auto* seat : server->seats) {
        server->decoration.filter.emplace_back(seat_add_event_filter(seat, [server](SeatEvent* event) -> SeatEventFilterResult {
            if (event->type == SeatEventType::keyboard_enter || event->type == SeatEventType::keyboard_leave) {
                update_border_colors(server);
            }
            return SeatEventFilterResult::passthrough;
        }));
    }
}

// -----------------------------------------------------------------------------

auto wm_find_window_at(WmServer* server, vec2f32 point) -> WmWindow*
{
    // TODO: This will ignore any `input_plane`s currently.
    //       Should we provide (optional) mappings from `input_plane` back to windows
    //       and then intersect against both `input_plane`s and `window` frames?

    WmWindow* window = nullptr;

    scene_iterate<SceneIterateDirection::front_to_back>(
        wm_get_layer(server, WmLayer::window),
        scene_iterate_default,
        scene_iterate_default,
        [&](SceneTree* tree) {
            if (tree->userdata.id == server->window_system_id) {
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

void wm_focus(WmServer* server, WmWindow* window)
{
    auto* keyboard = seat_get_keyboard(wm_get_seat(server));

    if (!window) {
        seat_keyboard_focus(keyboard, nullptr);
        return;
    }

    // Find the currently top-most leaf
    while (window->children.is_linked()) {
        window = LINK_GET(WmWindow, link, window->children.prev);
    }

    seat_keyboard_focus(keyboard, window->focus.get());
    wm_window_raise(window);
}

auto wm_window_is_focused(WmWindow* window) -> bool
{
    auto* server = window->client->server;
    return std::ranges::any_of(server->seats, [&](auto* seat) {
        auto* focus = seat_keyboard_get_focus(seat_get_keyboard(seat));
        if (seat_focus_contains(window->focus.get(), focus)) {
            return true;
        }
        return false;
    });;
}

auto wm_find_window_for(WmServer* server, SeatFocus* focus) -> WmWindow*
{
    if (!focus) return nullptr;
    for (auto* window : server->windows) {
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
        for (auto* border : window->borders) {
            scene_node_unparent(border);
        }
        wm_window_request_reposition(window, wm_output_get_viewport(output), vec2f32{1, 1});
    } else if (last_output) {
        for (auto* border : window->borders) {
            scene_tree_place_above(window->root_tree.get(), nullptr, border);
        }
        wm_window_request_reposition(window, window->fullscreen.restore, vec2f32{1, 1});
        window->output_constraint = wm_find_output_for(window->client->server, window->fullscreen.restore);
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
    auto* server = window->client->server;

    vec2f32 anchor_pos;

    if (window->parent) {
        // Child, position at center of parent.
        auto parent_bounds = wm_window_get_frame(window->parent);
        anchor_pos = parent_bounds.origin + (parent_bounds.extent) / 2.f;
    } else {
        // Non-child, spawn under mouse
        auto* seat = wm_get_seat(server);
        anchor_pos = seat_pointer_get_position(seat_get_pointer(seat));
    }

    window->anchor = anchor_pos;

    if (auto output = wm_find_output_at(server, anchor_pos).output) {
        window->output_constraint = output;
        window->relative = {0.5f, 0.5f};

        return wm_output_get_workarea(output).extent;
    } else {
        if (!window->parent) window->anchor = {};
        window->relative = {0.f, 0.f};

        return {};
    }
}
