#include "seat.hpp"

auto seat_input_region_create(SeatFocus* focus) -> Ref<SeatInputRegion>
{
    auto region = ref_create<SeatInputRegion>();
    region->focus = focus;
    return region;
}

SeatInputRegion::~SeatInputRegion()
{
    scene_node_unparent(this);
}

void SeatInputRegion::apply_damage(Scene* scene)
{
    log_warn("input region damaged");
}

void seat_input_region_set_region(SeatInputRegion* input_region, region2f32 region)
{
    if (input_region->region == region) return;

    input_region->region = std::move(region);

    scene_node_damage(input_region);
}
