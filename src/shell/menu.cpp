#include "shell.hpp"

#include <ui/ui.hpp>

#include <way/surface/surface.hpp>

struct ShellMenu
{
    Shell* shell;

    Listener<void()> frame;
    bool show_demo_window = false;
};

static
void frame(ShellMenu* menu)
{
    auto* shell = menu->shell;
    auto* io = shell->io.get();
    auto* gpu = shell->gpu.get();
    auto* wm = shell->wm.get();
    auto* way = shell->way.get();
    auto* scene = wm_get_scene(wm);

    if (menu->show_demo_window) {
        ImGui::ShowDemoWindow(&menu->show_demo_window);
    }

    defer { ImGui::End(); };
    if (ImGui::Begin("Shell")) {

        if (ImGui::Button("Destroy Clients")) {
            way_clear(way);
        }

        if (ImGui::Button("Shutdown")) {
            io_stop(io);
        }

        if (ImGui::Button("New Output")) {
            io_output_create(io);
        }

        ImGui::Checkbox("Show Demo Window", &menu->show_demo_window);

        {
            defer {  ImGui::EndDisabled(); };
            ImGui::BeginDisabled(!gpu->renderdoc);
            if (ImGui::Button("Capture")) {
                static u32 capture = 0;
                gpu->renderdoc->StartFrameCapture(nullptr, nullptr);
                gpu->renderdoc->SetCaptureTitle(std::format("Shell capture {}", ++capture).c_str());
                for (auto* output : wm_list_outputs(wm)) {
                    auto viewport = wm_output_get_viewport(output);
                    auto texture = gpu_image_create(gpu, {
                        .extent = vec_cast<u32>(viewport.extent),
                        .format = gpu_format_from_drm(DRM_FORMAT_ABGR8888),
                        .usage = GpuImageUsage::render
                    });
                    scene_render(scene, texture.get(), viewport);
                    gpu_wait(gpu_flush(gpu));
                }
                gpu->renderdoc->EndFrameCapture(nullptr, nullptr);
            }
        }

        if (ImGui::Button("Print Scene Graph")) {
            u32 depth = 0;
            auto indent = [&] { return std::string(depth, ' '); };
            scene_iterate<SceneIterateDirection::back_to_front>(
                wm_get_layer(wm, WmLayer::window)->parent,
                [&](SceneTree* tree) {
                    WaySurface* surface;
                    if (tree->userdata.id == way->userdata_id
                            && (surface = way_get_userdata<WaySurface>(way, tree->userdata.data))) {
                        log_warn("{}tree({}{}) {{", indent(),
                            surface->role,
                            tree->enabled ? "": ", disabled");
                    } else {
                        log_warn("{}tree{} {{", indent(), tree->enabled ? "": "(disabled)");
                    }
                    depth += 2;
                },
                [&](SceneNode* node) {
                    log_warn("{}{}", indent(), typeid(*node).name());
                },
                [&](SceneTree* tree) {
                    depth -= 2;
                    log_warn("{}}}", indent());
                });
        }
    }
}

void shell_init_menu(Shell* shell)
{
    auto menu = ref_create<ShellMenu>();
    menu->shell = shell;

    menu->frame = ui_get_signals(shell->ui.get()).frame.listen([menu = menu.get()] {
        frame(menu);
    });
    ui_request_frame(shell->ui.get());

    shell->apps.emplace_back(menu);
}
