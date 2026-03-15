#pragma once

#include "imui.hpp"

struct imui_viewport_data {
    core::Ref<scene_window>       window;
    core::Ref<scene_tree>         draws;
    core::Ref<scene_input_region> input_plane;

    // Pending reposition request. Requests are double-buffered so that
    // resizes requested during ImGui frames are handled correctly.
    std::optional<rect2f32> reposition;
};

struct imui_context
{
    gpu_context*   gpu;
    scene_context* scene;

    core::Ref<gpu_sampler>  sampler;
    core::Ref<scene_client> client;
    ImGuiContext*     context;
    u32 frames_requested = 0;

    struct texture {
        core::Ref<gpu_image>   image;
        core::Ref<gpu_sampler> sampler;
        gpu_blend_mode   blend;
    };
    std::vector<texture> textures;
    core::Ref<gpu_image> font_image;

    std::vector<std::move_only_function<imui_frame_fn>> frame_handlers;

    scene_keyboard* keyboard;
    scene_pointer*  pointer;

    struct {
        std::string text;
    } clipboard;

    ~imui_context();
};

void imui_init(imui_context*);
void imui_frame(imui_context*);
void imui_handle_key(imui_context*, scene_scancode, bool pressed);
void imui_handle_mods(imui_context*);
void imui_handle_motion(imui_context*);
void imui_handle_button(imui_context*, scene_scancode, bool pressed);
void imui_handle_wheel(imui_context*, vec2f32 delta);
void imui_handle_keyboard_enter(imui_context*, scene_keyboard*, scene_input_region*);
void imui_handle_keyboard_leave(imui_context*);
void imui_handle_pointer_enter(imui_context*, scene_pointer*, scene_input_region*);
void imui_handle_pointer_leave(imui_context*);
void imui_handle_output_layout(imui_context*);
