#pragma once

#include "ui.hpp"

struct UiViewportData {
    Ref<WmWindow> window;
    RefVector<SceneMesh> meshes;
    Ref<SceneInputRegion> input_region;
    Ref<SeatFocus> focus;

    rect2f32 frame;
};

struct UiDataSource;

struct UiClient
{
    Gpu* gpu;

    WmServer* wm;
    Ref<WmClient> client;

    std::chrono::steady_clock::time_point last_frame = {};

    std::string ini_path;

    Ref<GpuSampler> sampler;
    ImGuiContext* context;
    u32 frames_requested = 0;

    struct Texture {
        Ref<GpuImage>   image;
        Ref<GpuSampler> sampler;
        GpuBlendMode    blend;
    };
    std::vector<Texture> textures;
    Ref<GpuImage> font_image;

    UiSignals signals;

    std::flat_set<Seat*> seats;

    Ref<UiDataSource> data_source;

    SeatKeyboard* keyboard;
    SeatPointer*  pointer;

    struct {
        std::string text;
    } clipboard;

    ~UiClient();
};

struct UiContextGuard
{
    ImGuiContext* old;

    UiContextGuard(ImGuiContext* imgui)
        : old(ImGui::GetCurrentContext())
    {
        ImGui::SetCurrentContext(imgui);
    }

    ~UiContextGuard()
    {
        ImGui::SetCurrentContext(old);
    }
};
