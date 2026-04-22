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
