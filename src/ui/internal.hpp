#pragma once

#include "ui.hpp"

struct ui_viewport_data {
    ref<scene_window>       window;
    ref<scene_tree>         draws;
    ref<scene_input_region> input_plane;

    // Pending reposition request. Requests are double-buffered so that
    // resizes requested during ImGui frames are handled correctly.
    std::optional<rect2f32> reposition;
};

struct ui_context
{
    gpu_context*   gpu;
    scene_context* scene;

    ref<gpu_sampler>  sampler;
    ref<scene_client> client;
    ImGuiContext*     context;
    u32 frames_requested = 0;

    struct texture {
        ref<gpu_image>   image;
        ref<gpu_sampler> sampler;
        gpu_blend_mode   blend;
    };
    std::vector<texture> textures;
    ref<gpu_image> font_image;

    std::vector<std::move_only_function<ui_frame_fn>> frame_handlers;

    scene_keyboard* keyboard;
    scene_pointer*  pointer;

    struct {
        std::string text;
    } clipboard;

    ~ui_context();
};

void ui_init(ui_context*);
void ui_frame(ui_context*);
void ui_handle_key(ui_context*, scene_scancode, bool pressed);
void ui_handle_mods(ui_context*);
void ui_handle_motion(ui_context*);
void ui_handle_button(ui_context*, scene_scancode, bool pressed);
void ui_handle_wheel(ui_context*, vec2f32 delta);
void ui_handle_keyboard_enter(ui_context*, scene_keyboard*, scene_input_region*);
void ui_handle_keyboard_leave(ui_context*);
void ui_handle_pointer_enter(ui_context*, scene_pointer*, scene_input_region*);
void ui_handle_pointer_leave(ui_context*);
void ui_handle_output_layout(ui_context*);
