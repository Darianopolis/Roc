#include "internal.hpp"

#include "core/math.hpp"

WmWindow::~WmWindow()
{
    wm_window_post_event(ptr_to(WmWindowEvent {
        .type = WmEventType::window_destroyed,
        .window = this,
    }));

    tree->userdata = {};
    wm_window_unmap(this);
    std::erase(wm->windows, this);
}

auto wm_window_create(WindowManager* wm) -> Ref<WmWindow>
{
    auto window = ref_create<WmWindow>();
    window->wm = wm;

    wm->windows.emplace_back(window.get());

    window->tree = scene_tree_create();

    window->tree->userdata = {wm->window_system_id, window.get()};

    wm_window_post_event(ptr_to(WmWindowEvent {
        .type = WmEventType::window_created,
        .window = window.get(),
    }));

    return window;
}

void wm_window_set_event_listener(WmWindow* window, WmWindowListener listener)
{
    window->listener = std::move(listener);
}

auto wm_window_get_tree(WmWindow* window) -> SceneTree*
{
    return window->tree.get();
}

void wm_window_set_title(WmWindow* window, std::string_view title)
{
    window->title = title;
}

void wm_window_post_event(WmWindowEvent* event)
{
    if (event->window->listener) {
        event->window->listener(event);
    }
}

void wm_window_request_reposition(WmWindow* window, rect2f32 frame, vec2f32 gravity)
{
    wm_window_post_event(ptr_to(WmWindowEvent {
        .type = WmEventType::window_reposition_requested,
        .window = window,
        .reposition = {
            .frame = frame,
            .gravity = gravity,
        },
    }));
}

void wm_window_request_close(WmWindow* window)
{
    wm_window_post_event(ptr_to(WmWindowEvent {
        .type = WmEventType::window_close_requested,
        .window = window,
    }));
}

void wm_window_set_frame(WmWindow* window, rect2f32 frame)
{
    window->extent = frame.extent;
    scene_tree_set_translation(window->tree.get(), frame.origin);

    wm_window_post_event(ptr_to(WmWindowEvent {
        .type = WmEventType::window_repositioned,
        .window = window,
    }));
}

auto wm_window_get_frame(WmWindow* window) -> rect2f32
{
    return {
        scene_tree_get_position(window->tree.get()),
        window->extent,
        xywh
    };
}

void wm_window_map(WmWindow* window)
{
    if (window->mapped) return;

    scene_tree_place_above(wm_get_layer(window->wm, WmLayer::window), nullptr, window->tree.get());

    window->mapped = true;

    wm_window_post_event(ptr_to(WmWindowEvent {
        .type = WmEventType::window_mapped,
        .window = window,
    }));
}

void wm_window_raise(WmWindow* window)
{
    if (!window->mapped) return;

    scene_tree_place_above(wm_get_layer(window->wm, WmLayer::window), nullptr, window->tree.get());
}

void wm_window_unmap(WmWindow* window)
{
    if (!window->mapped) return;

    scene_node_unparent(window->tree.get());

    window->mapped = false;

    wm_window_post_event(ptr_to(WmWindowEvent {
        .type = WmEventType::window_unmapped,
        .window = window,
    }));
}

// -----------------------------------------------------------------------------

auto wm_find_window_at(WindowManager* wm, vec2f32 point) -> WmWindow*
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

void wm_window_add_input_region(WmWindow* window, SeatInputRegion* region)
{
    window->input_regions.emplace_back(region);
}
