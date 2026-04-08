#pragma once

#include "ui.hpp"

#include "seat/seat.hpp"

struct UiViewportData {
    Ref<WmToplevel> window;
    RefVector<SceneMesh> meshes;
    Ref<SeatInputRegion> input_region;

    // Pending reposition request. Requests are double-buffered so that
    // resizes requested during ImGui frames are handled correctly.
    std::optional<rect2f32> reposition;
};

struct Ui
{
    Gpu* gpu;
    Scene* scene;

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

    std::move_only_function<UiFrameFn> frame_handler;

    std::flat_set<Seat*> seats;

    SeatKeyboard* keyboard;
    SeatPointer*  pointer;

    struct {
        std::string text;
    } clipboard;

    ~Ui();
};
