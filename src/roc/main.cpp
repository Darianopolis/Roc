#include "roc.hpp"

#include "wm/wm.hpp"

#include "way/way.hpp"
#include "way/surface/surface.hpp"

int main()
{
    Roc roc = {};

    // Config

    roc.app_share = std::filesystem::path(getenv("HOME")) / ".local/share" / PROGRAM_NAME;
    roc.main_mod = SceneModifier::alt;
    roc.wallpaper = getenv("WALLPAPER");

    // Systems

    auto exec  = exec_create();
    auto gpu   = gpu_create(  exec.get(), {});
    auto io    = io_create(   exec.get(), gpu.get());
    auto wm    = wm_create({
        .exec = exec.get(),
        .gpu = gpu.get(),
        .io = io.get(),
        .main_mod = roc.main_mod,
    });
    auto scene = wm_get_scene(wm.get());
    auto way = way_create(exec.get(), gpu.get(), wm.get());

    roc.exec = exec.get();
    roc.gpu = gpu.get();
    roc.way = way.get();
    roc.io = io.get();
    roc.wm = wm.get();

    // Applets

    auto _ = roc_init_background(&roc);
    auto _ = roc_init_launcher(&roc);
    auto _ = roc_init_log_viewer(&roc);

    // Test client

    auto client = scene_client_create(scene);

    auto window = wm_window_create(wm.get());
    auto initial_size = vec2f32{256, 256};
    wm_window_set_frame(window.get(), {{64, 64}, initial_size, xywh});

    auto canvas = scene_texture_create();
    scene_texture_set_tint(canvas.get(), {255, 0, 255, 255});
    scene_texture_set_dst(canvas.get(), {{}, initial_size, xywh});
    scene_tree_place_below(wm_window_get_tree(window.get()), nullptr, canvas.get());

    auto input = scene_input_region_create(client.get());
    wm_window_add_input_region(window.get(), input.get());
    scene_input_region_set_region(input.get(), {{{}, initial_size, xywh}});
    scene_tree_place_above(wm_window_get_tree(window.get()), nullptr, input.get());

    auto inner = scene_tree_create();
    scene_tree_set_translation(inner.get(), {64, 64});
    scene_tree_place_above(wm_window_get_tree(window.get()), nullptr, inner.get());

    auto square = scene_texture_create();
    scene_texture_set_tint(square.get(), {0, 255, 255, 255});
    scene_texture_set_dst(square.get(), {{}, {128, 128}, xywh});
    scene_tree_place_above(inner.get(), nullptr, square.get());

    wm_window_set_event_listener(window.get(), [&](WmWindowEvent* event) {
        switch (event->type) {
            break;case WmEventType::window_reposition_requested: {
                auto frame = event->reposition.frame;
                scene_texture_set_dst(       canvas.get(), {{}, frame.extent, xywh});
                scene_input_region_set_region(input.get(), {{{}, frame.extent, xywh}});
                wm_window_set_frame(event->window, frame);
            }
            break;case WmEventType::window_close_requested:
                log_warn("toplevel_close({})", (void*)event->window);

            break;default:
                ;
        }
    });

    scene_client_set_event_handler(client.get(), [&](SceneEvent* event) {
        switch (event->type) {
            break;case SceneEventType::seat_add:
                log_trace("seat_add({})", (void*)event->seat);
            break;case SceneEventType::seat_configure:
                log_trace("seat_configure({})", (void*)event->seat);
            break;case SceneEventType::seat_remove:
                log_trace("seat_remove({})", (void*)event->seat);

            break;case SceneEventType::keyboard_key:
                log_trace("keyboard_key({}, {})",
                    libevdev_event_code_get_name(EV_KEY, event->keyboard.key.code),
                    event->keyboard.key.pressed ? "pressed" : "released");
            break;case SceneEventType::keyboard_modifier:
                log_trace("keyboard_modifier({})", scene_keyboard_get_modifiers(event->keyboard.keyboard));
            break;case SceneEventType::pointer_motion:
                log_trace("pointer_motion(accel: {}, unaccel: {}, pos: {})",
                    event->pointer.motion.rel_accel,
                    event->pointer.motion.rel_unaccel,
                    scene_pointer_get_position(event->pointer.pointer));
            break;case SceneEventType::pointer_button:
                log_trace("pointer_button({}, {})",
                    libevdev_event_code_get_name(EV_KEY, event->pointer.button.code),
                    event->pointer.button.pressed ? "pressed" : "released");
                wm_window_raise(window.get());
            break;case SceneEventType::pointer_scroll:
                log_trace("pointer_scroll(delta: {})", event->pointer.scroll.delta);
            break;case SceneEventType::pointer_enter:
                log_trace("pointer_enter({}, focus: {})", (void*)event->pointer.pointer, (void*)event->pointer.focus);
            break;case SceneEventType::pointer_leave:
                log_trace("pointer_leave({})", (void*)event->pointer.pointer);
            break;case SceneEventType::keyboard_enter:
                log_trace("keyboard_enter({})", (void*)event->keyboard.keyboard);
            break;case SceneEventType::keyboard_leave:
                log_trace("keyboard_leave({})", (void*)event->keyboard.keyboard);
            break;case SceneEventType::selection:
                log_trace("selection({})", (void*)event->data.source);
        }
    });

    wm_window_map(window.get());

    // ImGui

    std::string ui_text_edit = "Hello, world!";
    auto ui = ui_create(gpu.get(), wm.get(), roc.app_share / "ui");
    ui_set_frame_handler(ui.get(), [&] {
        ImGui::ShowDemoWindow();

        defer { ImGui::End(); };
        if (ImGui::Begin("Roc")) {

            if (ImGui::Button("Shutdown")) {
                io_stop(io.get());
            }

            if (ImGui::Button("New Output")) {
                io_output_create(io.get());
            }

            if (ImGui::Button("Reposition")) {
                if (auto* toplevel = ui_get_toplevel(ImGui::GetCurrentWindow())) {
                    wm_window_request_reposition(toplevel, {{}, {512, 512}, xywh}, {});
                }
            }

            {
                defer {  ImGui::EndDisabled(); };
                ImGui::BeginDisabled(!gpu->renderdoc);
                if (ImGui::Button("Capture")) {
                    static u32 capture = 0;
                    gpu->renderdoc->StartFrameCapture(nullptr, nullptr);
                    gpu->renderdoc->SetCaptureTitle(std::format("Roc capture {}", ++capture).c_str());
                    for (auto* output : wm_list_outputs(wm.get())) {
                        auto viewport =  wm_output_get_viewport(output);
                        auto texture = gpu_image_create(gpu.get(), {
                            .extent = viewport.extent,
                            .format = gpu_format_from_drm(DRM_FORMAT_ABGR8888),
                            .usage = GpuImageUsage::render
                        });
                        scene_render(scene, texture.get(), viewport);
                    }
                    gpu->renderdoc->EndFrameCapture(nullptr, nullptr);
                }
            }

            if (ImGui::Button("Print Scene Graph")) {
                u32 depth = 0;
                auto indent = [&] { return std::string(depth, ' '); };
                scene_iterate<SceneIterateDirection::back_to_front>(
                    scene_get_layer(scene, SceneLayer::window)->parent,
                    [&](SceneTree* tree) {
                        WaySurface* surface;
                        if (tree->userdata.id == way->userdata_id
                                && (surface = way_get_userdata<WaySurface>(tree->userdata.data))) {
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

            ImGui::InputText("Text", &ui_text_edit);
        }
    });
    ui_request_frame(ui.get());

    // Selection

    auto data_client = scene_client_create(scene);
    scene_client_set_event_handler(data_client.get(), [](SceneEvent*) {});
    auto data_source = scene_data_source_create(data_client.get(), {
        .send = [&](const char* mime, int fd) {
            std::string message = "This is a test clipboard message.";
            write(fd, message.data(), message.size());
        }
    });
    scene_data_source_offer(data_source.get(), "text/plain;charset=utf-8");
    scene_data_source_offer(data_source.get(), "text/plain");
    scene_data_source_offer(data_source.get(), "text/html");
    for (auto* seat : scene_get_seats(scene)) {
        scene_seat_set_selection(seat, data_source.get());
    }

    // Run

    io_run(io.get());
}
