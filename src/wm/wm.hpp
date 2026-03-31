#pragma once

#include "scene/scene.hpp"

#include "ui/ui.hpp"

enum class WmInteractionMode
{
    none,
    move,
    size,
    zone,
};

struct WindowManager
{
    Gpu* gpu;
    Scene* scene;
    Ref<UiContext> ui;

    SceneModifier main_mod;

    WmInteractionMode mode;
    Ref<SceneClient> client;

    struct {
        ScenePointer* pointer;

        Weak<SceneWindow> window;

        vec2f32  grab;
        rect2f32 frame;
        vec2f32  relative;

    } movesize;

    struct {
        ScenePointer* pointer;

        Weak<SceneWindow> window;

        Ref<SceneTexture> texture;

        aabb2f64 initial_zone;
        aabb2f64 final_zone;
        bool     selecting = false;
    } zone;

    struct {
        bool show_details;
        i64 selected = -1;
    } log;
};

auto wm_create(Gpu*, Scene*, std::filesystem::path app_share) -> Ref<WindowManager>;

void wm_interaction_init(WindowManager*);
void wm_zone_init(       WindowManager*);

void wm_movesize_handle_event(WindowManager*, SceneEvent*);
void wm_zone_handle_event(    WindowManager*, SceneEvent*);

void wm_log_frame(WindowManager*);
void wm_log_init( WindowManager*);