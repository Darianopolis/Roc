#pragma once

#include "scene/scene.hpp"

struct ui_context;

auto ui_create(gpu_context*, scene_context*) -> ref<ui_context>;

// -----------------------------------------------------------------------------

using ui_frame_fn = void();

void ui_request_frame(ui_context*);
void ui_add_frame_handler(ui_context*, std::move_only_function<ui_frame_fn>&&);
auto ui_get_texture(ui_context*, gpu_image*, gpu_sampler*, gpu_blend_mode) -> ImTextureID;

auto ui_get_window(ImGuiWindow*) -> scene_window*;

// -----------------------------------------------------------------------------

template<typename ...Args>
void ui_text(std::format_string<Args...> fmt, Args&&... args)
{
    ImGui::TextUnformatted(std::vformat(fmt.get(), std::make_format_args(args...)).c_str());
}
