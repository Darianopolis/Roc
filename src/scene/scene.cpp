#include "internal.hpp"

Scene::~Scene()
{
}

auto scene_create(Gpu* gpu) -> Ref<Scene>
{
    auto scene = ref_create<Scene>();

    scene->gpu = gpu;

    scene->root = scene_tree_create();
    scene->root->scene = scene.get();

    scene_render_init(scene.get());

    return scene;
}

auto scene_get_root(Scene* scene) -> SceneTree*
{
    return scene->root.get();
}

// -----------------------------------------------------------------------------

auto seat_add_input_event_filter(Seat* seat, std::move_only_function<SeatEventFilterResult(SeatEvent*)> fn) -> Ref<SeatEventFilter>
{
    auto filter = ref_create<SeatEventFilter>();
    filter->seat = seat;
    filter->filter = std::move(fn);
    seat->input_event_filters.emplace_back(filter.get());
    return filter;
}

SeatEventFilter::~SeatEventFilter()
{
    if (seat) {
        std::erase(seat->input_event_filters, this);
    }
}
