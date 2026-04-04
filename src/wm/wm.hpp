#pragma once

#include "core/object.hpp"
#include "scene/scene.hpp"

struct ExecContext;
struct Gpu;
struct WindowManager;
struct IoContext;

struct WindowManagerCreateInfo
{
    ExecContext* exec;
    Gpu*         gpu;
    IoContext*   io;
    Scene*       scene;

    SceneModifier main_mod;
};

auto wm_create(const WindowManagerCreateInfo&) -> Ref<WindowManager>;
