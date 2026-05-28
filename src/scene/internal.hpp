#pragma once

#include "scene.hpp"

#include <io/io.hpp>

// -----------------------------------------------------------------------------

struct Scene
{
    Gpu* gpu;

    struct {
        Ref<GpuShader> vertex;
        Ref<GpuShader> fragment;
        Ref<GpuImage> white;
        Ref<GpuSampler> nearest;
    } render;

    Ref<SceneTree> root;

    std::vector<SceneDamageListener> damage_listeners;

    ~Scene();
};

void scene_render_init(Scene*);

void scene_node_damage(SceneTexture*, Scene*);
void scene_node_damage(SceneInputRegion*, Scene*);
void scene_node_damage(SceneTree*, Scene*);

inline
void scene_node_damage(SceneNode* node, Scene* scene)
{
    scene_visit(node, [&](auto* node) {
        scene_node_damage(node, scene);
    });
}
