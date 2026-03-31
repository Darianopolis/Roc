#include "wm.hpp"

#include <core/chrono.hpp>

#include "scene/scene.hpp"

#include "io/io.hpp"

#include "ui/ui.hpp"
#include "way/way.hpp"
#include "way/surface/surface.hpp"

struct WmLinearAccel
{
    f32 offset;
    f32 rate;
    f32 multiplier;

    auto operator()(vec2f32 delta) -> vec2f32
    {
        // Apply a linear mouse acceleration curve
        //
        // Offset     - speed before acceleration is applied.
        // Accel      - rate that sensitivity increases with motion.
        // Multiplier - total multplier for sensitivity.
        //
        //      /
        //     / <- Accel
        // ___/
        //  ^-- Offset

        f32 speed = glm::length(delta);
        vec2f32 sens = vec2f32(multiplier * (1 + (std::max(speed, offset) - offset) * rate));

        return delta * sens;
    }
};

int main()
{
    // Directories

    auto home = std::filesystem::path(getenv("HOME"));
    auto app_share = home / ".local/share" / PROGRAM_NAME;

    // Systems

    auto exec  = exec_create();
    auto gpu   = gpu_create(  exec.get(), {});
    auto io    = io_create(   exec.get(), gpu.get());
    auto scene = scene_create(exec.get(), gpu.get());
    auto wm    = wm_create(gpu.get(), scene.get(), app_share);

    // I/O event plumbing

    auto image_pool = gpu_image_pool_create(gpu.get());
    auto output_client = scene_client_create(scene.get());
    struct Output {
        Ref<SceneOutput> scene;
        IoOutput*         io;
    };
    std::vector<Output> outputs;
    auto reflow_outputs = [&] {
        f32 x = 0;
        for (auto& output : outputs) {
            auto size = output.io->info().size;
            scene_output_set_viewport(output.scene.get(), {{x, 0.f}, size, xywh});
            x += f32(size.x);
        }
    };
    io_set_event_handler(io.get(), [&](IoEvent* event) {
        switch (event->type) {
            // shutdown
            break;case IoEventType::shutdown_requested:
                io_stop(io.get());

            // input
            break;case IoEventType::input_added:
                  case IoEventType::input_removed:
                  case IoEventType::input_event:
                scene_push_io_event(scene.get(), event);

            // output
            break;case IoEventType::output_added:
                outputs.emplace_back(scene_output_create(output_client.get(), SceneOutputFlag::workspace), event->output.output);
                reflow_outputs();
            break;case IoEventType::output_configure:
                reflow_outputs();
            break;case IoEventType::output_removed:
                std::erase_if(outputs, [&](auto& p) { return p.io == event->output.output; });
                reflow_outputs();
            break;case IoEventType::output_frame: {
                auto output = std::ranges::find_if(outputs, [&](auto& p) { return p.io == event->output.output; });
                scene_frame(scene.get(), output->scene.get());

                // TODO: Only redraw with damage

                auto format = gpu_format_from_drm(DRM_FORMAT_ABGR8888);
                auto usage = GpuImageUsage::render;

                auto target = image_pool->acquire({
                    .extent = output->io->info().size,
                    .format = format,
                    .usage = usage,
                    .modifiers = ptr_to(gpu_intersect_format_modifiers({
                        &gpu_get_format_properties(gpu.get(), format, usage)->mods,
                        &output->io->info().formats->get(format),
                    }))
                });

                scene_render(scene.get(), target.get(), scene_output_get_viewport(output->scene.get()));

                output->io->commit(target.get(), gpu_flush(gpu.get()), IoOutputCommitFlag::vsync);
            }
        }
    });
    scene_client_set_event_handler(output_client.get(), [&](SceneEvent* event) {
        switch (event->type) {
            break;case SceneEventType::output_frame_request: {
                auto output = std::ranges::find_if(outputs, [&](auto& p) { return p.scene.get() == event->output; });
                output->io->request_frame();
            }
            break;default:
                ;
        }
    });

    // Pointer

    auto seat_listener = scene_client_create(scene.get());
    scene_client_set_event_handler(seat_listener.get(), [&](SceneEvent* event) {
        switch (event->type) {
            break;case SceneEventType::seat_add: {
                auto* pointer = scene_seat_get_pointer(event->seat);
                scene_pointer_set_xcursor(pointer, "default");
                scene_pointer_set_accel(  pointer, WmLinearAccel {
                    .offset     = 2.f,
                    .rate       = 0.05f,
                    .multiplier = 0.3f
                });
            }
            break;default:
                ;
        }
    });

    // Background

    auto sampler = gpu_sampler_create(gpu.get(), {
        .mag = VK_FILTER_NEAREST,
        .min = VK_FILTER_LINEAR,
    });

    auto background_image = [&] {
        std::filesystem::path path = getenv("WALLPAPER");
        int w, h;
        int num_channels;
        stbi_uc* data = stbi_load(path.c_str(), &w, &h, &num_channels, STBI_rgb_alpha);
        defer { stbi_image_free(data); };
        log_info("Loaded background ({}, width = {}, height = {})", path.c_str(), w, h);

        // Create background texture node
        auto image = gpu_image_create(gpu.get(), {
            .extent = {w, h},
            .format = gpu_format_from_drm(DRM_FORMAT_XBGR8888),
            .usage = GpuImageUsage::texture | GpuImageUsage::transfer
        });
        gpu_copy_memory_to_image(image.get(), as_bytes(data, w * h * 4), {{{image->extent()}}});
        return image;
    }();

    auto background_client = scene_client_create(scene.get());

    Ref<SceneTree> background_layer = {};
    auto unparent_background = [&] {
        if (background_layer) scene_node_unparent(background_layer.get());
    };
    defer { unparent_background(); };
    auto update_backgrounds = [&] {
        unparent_background();
        background_layer = scene_tree_create(scene.get());
        scene_tree_place_above(scene_get_layer(scene.get(), SceneLayer::background), nullptr, background_layer.get());

        for (auto* output : scene_list_outputs(scene.get())) {
            vec2f32 image_size = background_image->extent();
            auto viewport = scene_output_get_viewport(output);

            // Create texture node
            auto texture = scene_texture_create(scene.get());
            scene_texture_set_image(texture.get(), background_image.get(), sampler.get(), GpuBlendMode::premultiplied);
            auto src = rect_fit<f32>(image_size, viewport.extent);
            scene_texture_set_src(texture.get(), {src.origin / image_size, src.extent / image_size, xywh});
            scene_texture_set_dst(texture.get(), viewport);
            scene_tree_place_above(background_layer.get(), nullptr, texture.get());
        }
    };

    scene_client_set_event_handler(background_client.get(), [scene = scene.get(), &update_backgrounds](SceneEvent* event) {
        switch (event->type) {
            break;case SceneEventType::output_layout:
                update_backgrounds();
            break;default:
                ;
        }
    });

    // Test client

    auto client = scene_client_create(scene.get());

    auto window = scene_window_create(client.get());
    auto initial_size = vec2f32{256, 256};
    scene_window_set_frame(window.get(), {{64, 64}, initial_size, xywh});

    auto canvas = scene_texture_create(scene.get());
    scene_texture_set_tint(canvas.get(), {255, 0, 255, 255});
    scene_texture_set_dst(canvas.get(), {{}, initial_size, xywh});
    scene_tree_place_below(scene_window_get_tree(window.get()), nullptr, canvas.get());

    auto input = scene_input_region_create(client.get(), window.get());
    scene_input_region_set_region(input.get(), {{{}, initial_size, xywh}});
    scene_tree_place_above(scene_window_get_tree(window.get()), nullptr, input.get());

    auto inner = scene_tree_create(scene.get());
    scene_tree_set_translation(inner.get(), {64, 64});
    scene_tree_place_above(scene_window_get_tree(window.get()), nullptr, inner.get());

    auto square = scene_texture_create(scene.get());
    scene_texture_set_tint(square.get(), {0, 255, 255, 255});
    scene_texture_set_dst(square.get(), {{}, {128, 128}, xywh});
    scene_tree_place_above(inner.get(), nullptr, square.get());

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
                scene_window_raise(window.get());
            break;case SceneEventType::pointer_scroll:
                log_trace("pointer_scroll(delta: {})", event->pointer.scroll.delta);
            break;case SceneEventType::pointer_enter:
                log_trace("pointer_enter({}, region: {})", (void*)event->pointer.pointer, (void*)event->pointer.focus.region);
            break;case SceneEventType::pointer_leave:
                log_trace("pointer_leave({})", (void*)event->pointer.pointer);
            break;case SceneEventType::keyboard_enter:
                log_trace("keyboard_enter({})", (void*)event->keyboard.keyboard);
            break;case SceneEventType::keyboard_leave:
                log_trace("keyboard_leave({})", (void*)event->keyboard.keyboard);
            break;case SceneEventType::window_reposition: {
                auto frame = event->window.reposition.frame;
                scene_texture_set_dst(       canvas.get(), {{}, frame.extent, xywh});
                scene_input_region_set_region(input.get(), {{{}, frame.extent, xywh}});
                scene_window_set_frame(event->window.window, frame);
            }
            break;case SceneEventType::window_close:
                log_warn("window_close({})", (void*)event->window.window);
            break;case SceneEventType::output_frame:
                  case SceneEventType::output_added:
                  case SceneEventType::output_removed:
                  case SceneEventType::output_configured:
                  case SceneEventType::output_frame_request:
                  case SceneEventType::output_layout:
                ;
            break;case SceneEventType::hotkey:
                ;
            break;case SceneEventType::selection:
                log_trace("selection({})", (void*)event->data.source);
        }
    });

    scene_window_map(window.get());

    // Wayland

    auto way = way_create(exec.get(), gpu.get(), scene.get());

    // ImGui

    std::string ui_text_edit = "Hello, world!";
    auto ui = ui_create(gpu.get(), scene.get(), app_share / "ui");
    ui_set_frame_handler(ui.get(), [&] {
        ImGui::ShowDemoWindow();

        defer { ImGui::End(); };
        if (ImGui::Begin("Roc")) {

            if (ImGui::Button("Shutdown")) {
                io_stop(io.get());
            }

            if (ImGui::Button("New Output")) {
                io_add_output(io.get());
            }

            if (ImGui::Button("Reposition")) {
                if (auto* window = ui_get_window(ImGui::GetCurrentWindow())) {
                    scene_window_request_reposition(window, {{}, {512, 512}, xywh}, {});
                }
            }

            {
                defer {  ImGui::EndDisabled(); };
                ImGui::BeginDisabled(!gpu->renderdoc);
                if (ImGui::Button("Capture")) {
                    static u32 capture = 0;
                    gpu->renderdoc->StartFrameCapture(nullptr, nullptr);
                    gpu->renderdoc->SetCaptureTitle(std::format("Roc capture {}", ++capture).c_str());
                    for (auto* output : scene_list_outputs(scene.get())) {
                        auto viewport =  scene_output_get_viewport(output);
                        auto texture = gpu_image_create(gpu.get(), {
                            .extent = viewport.extent,
                            .format = gpu_format_from_drm(DRM_FORMAT_ABGR8888),
                            .usage = GpuImageUsage::render
                        });
                        scene_render(scene.get(), texture.get(), viewport);
                    }
                    gpu->renderdoc->EndFrameCapture(nullptr, nullptr);
                }
            }

            if (ImGui::Button("Print Scene Graph")) {
                u32 depth = 0;
                auto indent = [&] { return std::string(depth, ' '); };
                scene_iterate<SceneIterateDirection::back_to_front>(
                    scene_get_layer(scene.get(), SceneLayer::window)->parent,
                    [&](SceneTree* tree) {
                        WaySurface* surface;
                        if (tree->system == way->scene_system
                                && (surface = way_get_userdata<WaySurface>(tree))) {
                            log_warn("{}tree({}{}) {{", indent(),
                                surface->role,
                                tree->enabled ? "": ", disabled");
                        } else {
                            log_warn("{}tree{} {{", indent(), tree->enabled ? "": "(disabled)");
                        }
                        depth += 2;
                    },
                    [&](SceneNode* node) {
                        log_warn("{}{}", indent(), node->type);
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

    // Hotkey

    auto main_mod = SceneModifier::alt;
    auto hotkey_client = scene_client_create(scene.get());
    ankerl::unordered_dense::map<SceneHotkey, std::move_only_function<void(SceneEvent*)>> hotkeys;

    auto close_hotkey = [](SceneEvent* event) {
        if (!event->hotkey.pressed) return;
        auto[_, input_region] = scene_input_device_get_focus(event->hotkey.input_device);
        if (input_region && input_region->window) {
            scene_window_request_close(input_region->window.get());
        }
    };
    hotkeys[{ main_mod, KEY_Q      }] = close_hotkey;
    hotkeys[{ main_mod, BTN_MIDDLE }] = close_hotkey;

    hotkeys[{ main_mod, KEY_S }] = [](SceneEvent* event) {
        if (!event->hotkey.pressed) return;
        auto keyboard = scene_input_device_get_keyboard(event->hotkey.input_device);
        if (!keyboard) return;
        scene_keyboard_clear_focus(keyboard);
    };

    for (auto&[hotkey, _] : hotkeys) {
        debug_assert(scene_client_hotkey_register(hotkey_client.get(), hotkey));
    }
    scene_client_set_event_handler(hotkey_client.get(), [&](SceneEvent* event) {
        if (event->type == SceneEventType::hotkey) {
            hotkeys.at(event->hotkey.hotkey)(event);
        }
    });

    // Selection

    auto data_client = scene_client_create(scene.get());
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
    for (auto* seat : scene_get_seats(scene.get())) {
        scene_seat_set_selection(seat, data_source.get());
    }

    // Run

    io_run(io.get());
}
