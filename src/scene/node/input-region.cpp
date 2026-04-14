#include "base.hpp"

#include "core/math.hpp"

SeatInputRegion::~SeatInputRegion()
{
    client->input_regions--;

    if (parent) {
        scene_node_unparent(this);
    }

    // TODO: Clear focus
}

void SeatInputRegion::damage(Scene* scene)
{
    scene_post_damage(scene, this);
}

auto scene_input_region_create(SeatClient* client) -> Ref<SeatInputRegion>
{
    auto region = ref_create<SeatInputRegion>();
    region->client = client;

    client->input_regions++;

    return region;
}

void scene_input_region_set_region(SeatInputRegion* input_region, region2f32 region)
{
    if (input_region->region == region) return;

#if SCENE_NOISY_NODES
    NODE_LOG("scene.input_region{{{}}}.set_region([{:s}])", (void*)input_region,
        region.aabbs
            | std::views::transform([&](auto& aabb) { return std::format("{}", aabb); })
            | std::views::join_with(", "sv));
#endif

    input_region->region = std::move(region);

    scene_node_damage(input_region);
}
