#pragma once

#include "scene/scene.hpp"

struct imui_context;

auto imui_create(gpu::Context*, scene_context*) -> core::Ref<imui_context>;

// -----------------------------------------------------------------------------

using imui_frame_fn = void();

void imui_request_frame(imui_context*);
void imui_add_frame_handler(imui_context*, std::move_only_function<imui_frame_fn>&&);
auto imui_get_texture(imui_context*, gpu::Image*, gpu::Sampler*, gpu::BlendMode) -> ImTextureID;

auto imui_get_window(ImGuiWindow*) -> scene_window*;

// -----------------------------------------------------------------------------

template<typename ...Args>
void imui_text(std::format_string<Args...> fmt, Args&&... args)
{
    ImGui::TextUnformatted(std::vformat(fmt.get(), std::make_format_args(args...)).c_str());
}
