#pragma once

#include "scene.hpp"

#include <io/io.hpp>

// -----------------------------------------------------------------------------

struct SceneRenderer
{
    Gpu* gpu;

    ankerl::unordered_dense::map<GpuFormat, Ref<GpuPipeline>> pipelines;
    Ref<GpuImage> white;
    Ref<GpuSampler> nearest;
    Ref<GpuBuffer> indices;
};

inline
auto scene_node_has_any_damage_listeners(SceneNode* node) -> bool
{
    if (node->signals.damage.listeners.is_linked()) return true;
    return node->parent && scene_node_has_any_damage_listeners(node->parent);
}

void scene_node_get_damage(SceneInputRegion*, vec2f32 offset, SceneDamage&);
void scene_node_get_damage(SceneTree*,        vec2f32 offset, SceneDamage&);
void scene_node_get_damage(SceneTexture*,     vec2f32 offset, SceneDamage&);

inline
void scene_node_get_damage(SceneNode* node, vec2f32 offset, SceneDamage& damage)
{
    scene_visit(node, [&](auto* node) {
        scene_node_get_damage(node, offset, damage);
    });
}

void scene_node_subtract_cover(SceneInputRegion*, vec2f32 offset, SceneDamage&);
void scene_node_subtract_cover(SceneTree*,        vec2f32 offset, SceneDamage&);
void scene_node_subtract_cover(SceneTexture*,     vec2f32 offset, SceneDamage&);

inline
void scene_node_subtract_cover(SceneNode* node, vec2f32 offset, SceneDamage& damage)
{
    scene_visit(node, [&](auto* node) {
        scene_node_subtract_cover(node, offset, damage);
    });
}

void scene_node_post_damage(SceneNode*, vec2f32 offset, SceneDamage);
