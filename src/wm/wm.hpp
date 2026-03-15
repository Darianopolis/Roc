#pragma once

#include "scene/scene.hpp"

enum class wm_movesize_mode
{
    none,
    move,
    size,
};

struct wm_context
{
    scene_context* scene;

    scene_modifier main_mod;

    struct {
        scene_pointer* pointer;

        core::Ref<scene_client> client;
        core::Weak<scene_window> window;

        vec2f32  grab;
        rect2f32 frame;
        vec2f32  relative;

        wm_movesize_mode mode;
    } movesize;
};

auto wm_create(scene_context*) -> core::Ref<wm_context>;

void wm_init_movesize(wm_context*);
