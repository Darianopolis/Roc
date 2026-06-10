#pragma once

#include "scene.hpp"

#include <io/io.hpp>

// -----------------------------------------------------------------------------

struct SceneRenderer
{
    Gpu* gpu;

    Ref<GpuShader>  vertex;
    Ref<GpuShader>  fragment;
    Ref<GpuImage>   white;
    Ref<GpuSampler> nearest;
    Ref<GpuBuffer>  indices;

    ~SceneRenderer();
};

auto scene_node_get_damage(SceneInputRegion*) -> SceneDamage;
auto scene_node_get_damage(SceneTree*       ) -> SceneDamage;
auto scene_node_get_damage(SceneTexture*    ) -> SceneDamage;

inline
auto scene_node_get_damage(SceneNode* node) -> SceneDamage
{
    return scene_visit(node, [](auto* node) {
        return scene_node_get_damage(node);
    });
}

void scene_node_post_damage(SceneNode*, SceneDamage);
