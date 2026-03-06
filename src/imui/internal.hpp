#pragma once

#include "imui.hpp"

struct imui_viewport_data {
    ref<scene_window>       window;
    ref<scene_tree>         draws;
    ref<scene_input_region> input_plane;

    // Pending reposition request. Requests are double-buffered so that
    // resizes requested during ImGui frames are handled correctly.
    std::optional<rect2f32> reposition;
};

struct imui_context : core_object
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

    std::vector<std::move_only_function<imui_frame_fn>> frame_handlers;

    struct {
        std::string text;
    } clipboard;

    ~imui_context();
};

void imui_init(imui_context*);
void imui_frame(imui_context*);
void imui_handle_key(imui_context*, scene_scancode, bool pressed);
void imui_handle_mods(imui_context*, flags<scene_modifier>);
void imui_handle_motion(imui_context*);
void imui_handle_button(imui_context*, scene_scancode, bool pressed);
void imui_handle_wheel(imui_context*, vec2f32 delta);
void imui_handle_focus_pointer(imui_context*, scene_focus gained);
void imui_handle_output_layout(imui_context*);
