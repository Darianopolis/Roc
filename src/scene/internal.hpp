#pragma once

#include "scene.hpp"

#include "io/io.hpp"

// -----------------------------------------------------------------------------

struct Scene
{
    ExecContext* exec;
    Gpu* gpu;

    struct {
        Ref<GpuShader>  vertex;
        Ref<GpuShader>  fragment;
        Ref<GpuImage>   white;
        Ref<GpuSampler> sampler;
    } render;

    Ref<SceneTree> root;

    RefVector<SceneDamageListener> damage_listeners;

    ~Scene();
};

void scene_render_init(Scene*);

void scene_post_damage(Scene*);
