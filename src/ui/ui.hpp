#pragma once

#include "scene/scene.hpp"

struct Ui;

auto ui_create(Gpu*, Scene*, const std::filesystem::path& ini_dir) -> Ref<Ui>;

// -----------------------------------------------------------------------------

using UiFrameFn = void();

void ui_request_frame(Ui*);
void ui_set_frame_handler(Ui*, std::move_only_function<UiFrameFn>&&);
auto ui_get_texture(Ui*, GpuImage*, GpuSampler*, GpuBlendMode) -> ImTextureID;

auto ui_get_window(ImGuiWindow*) -> SceneWindow*;

// -----------------------------------------------------------------------------

template<typename ...Args>
void ui_text(std::format_string<Args...> fmt, Args&&... args)
{
    ImGui::TextUnformatted(std::vformat(fmt.get(), std::make_format_args(args...)).c_str());
}
