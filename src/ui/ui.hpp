#pragma once

#include "scene/scene.hpp"

struct UiContext;

auto ui_create(Gpu*, Scene*, const std::filesystem::path& ini_dir) -> Ref<UiContext>;

// -----------------------------------------------------------------------------

using UiFrameFn = void();

void ui_request_frame(UiContext*);
void ui_set_frame_handler(UiContext*, std::move_only_function<UiFrameFn>&&);
auto ui_get_texture(UiContext*, GpuImage*, GpuSampler*, GpuBlendMode) -> ImTextureID;

auto ui_get_window(ImGuiWindow*) -> SceneWindow*;

// -----------------------------------------------------------------------------

template<typename ...Args>
void ui_text(std::format_string<Args...> fmt, Args&&... args)
{
    ImGui::TextUnformatted(std::vformat(fmt.get(), std::make_format_args(args...)).c_str());
}
