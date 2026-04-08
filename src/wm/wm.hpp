#pragma once

#include "core/object.hpp"
#include "scene/scene.hpp"
#include "seat/seat.hpp"

struct ExecContext;
struct Gpu;
struct WindowManager;
struct IoContext;

struct WmCreateInfo
{
    ExecContext* exec;
    Gpu*         gpu;
    IoContext*   io;
    Scene*       scene;

    SeatModifier modifier;
};

auto wm_create(const WmCreateInfo&) -> Ref<WindowManager>;

enum class WmLayer
{
    background,
    foreground,
    cursor,
};

auto wm_get_layer(WindowManager*, WmLayer) -> SceneTree*;

struct WmToplevel;

struct WmToplevelInterface
{
    virtual void request_reposition(vec4f32 rect, vec2f32 gravity) = 0;
};

auto wm_toplevel_allocate(WindowManager*) -> Ref<WmToplevel>;
void wm_toplevel_init(WmToplevel*, WmToplevelInterface*);
auto wm_toplevel_get_tree(WmToplevel*) -> SceneTree*;
