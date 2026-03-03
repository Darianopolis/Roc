#include "wm.hpp"

#include "scene/scene.hpp"

#include "io/io.hpp"

#include "imui/imui.hpp"
#include "way/way.hpp"
#include "way/internal.hpp"

int main()
{
    auto event_loop = core_event_loop_create();
    auto gpu = gpu_create({}, event_loop.get());
    auto io = io_create(event_loop.get(), gpu.get());
    auto scene = scene_create(gpu.get(), io.get());
    auto wm = wm_create(scene.get());

    // I/O event plumbing

    auto output_client = scene_client_create(scene.get());
    struct output {
        ref<scene_output> scene;
        io_output*        io;
    };
    std::vector<output> outputs;
    auto reflow_outputs = [&] {
        f32 x = 0;
        for (auto& output : outputs) {
            auto size = output.io->info().size;
            scene_output_set_viewport(output.scene.get(), {{x, 0.f}, size, core_xywh});
            x += f32(size.x);
        }
    };
    io_set_event_handler(io.get(), [&](io_event* event) {
        switch (event->type) {
            // shutdown
            break;case io_event_type::shutdown_requested:
                io_stop(io.get());

            // input
            break;case io_event_type::input_added:
                  case io_event_type::input_removed:
                  case io_event_type::input_event:
                scene_push_io_event(scene.get(), event);

            // output
            break;case io_event_type::output_added:
                outputs.emplace_back(scene_output_create(output_client.get()), event->output.output);
                reflow_outputs();
            break;case io_event_type::output_configure:
                reflow_outputs();
            break;case io_event_type::output_removed:
                std::erase_if(outputs, [&](auto& p) { return p.io == event->output.output; });
                reflow_outputs();
            break;case io_event_type::output_frame: {
                auto output = std::ranges::find_if(outputs, [&](auto& p) { return p.io == event->output.output; });
                scene_render(scene.get(), output->scene.get(), output->io);
            }
        }
    });
    scene_client_set_event_handler(output_client.get(), [&](scene_event* event) {
        switch (event->type) {
            break;case scene_event_type::output_damaged: {
                auto output = std::ranges::find_if(outputs, [&](auto& p) { return p.scene.get() == event->output; });
                output->io->request_frame();
            }
            break;default:
                ;
        }
    });

    // Pointer driver

    scene_pointer_set_driver(scene.get(), [scene = scene.get()](scene_pointer_driver_in in) -> scene_pointer_driver_out {

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

        static constexpr f32 offset     = 2.f;
        static constexpr f32 rate       = 0.05f;
        static constexpr f32 multiplier = 0.3f;

        f32 speed = glm::length(in.delta);
        vec2f32 sens = vec2f32(multiplier * (1 + (std::max(speed, offset) - offset) * rate));
        vec2f32 delta = in.delta * sens;

        vec2f32 new_pos = in.position + delta;

        return {
            .position = scene_find_output_for_point(scene, new_pos).position,
            .accel    = delta,
            .unaccel  = in.delta
        };
    });

    // Background

    auto sampler = gpu_sampler_create(gpu.get(), VK_FILTER_NEAREST, VK_FILTER_LINEAR);

    auto background_image = [&] {
        std::filesystem::path path = getenv("WALLPAPER");
        int w, h;
        int num_channels;
        stbi_uc* data = stbi_load(path.c_str(), &w, &h, &num_channels, STBI_rgb_alpha);
        defer { stbi_image_free(data); };
        log_info("Loaded background ({}, width = {}, height = {})", path.c_str(), w, h);

        // Create background texture node
        auto image = gpu_image_create(gpu.get(), {w, h}, gpu_format_from_drm(DRM_FORMAT_XBGR8888),
            gpu_image_usage::texture | gpu_image_usage::transfer);
        gpu_image_update_immed(image.get(), data);
        return image;
    }();

    auto background_client = scene_client_create(scene.get());

    ref<scene_tree> background_layer = {};
    auto update_backgrounds = [&] {
        auto root = scene_get_root_transform(scene.get());

        if (background_layer) scene_node_unparent(background_layer.get());
        background_layer = scene_tree_create(scene.get());
        scene_tree_place_above(scene_get_layer(scene.get(), scene_layer::background), nullptr, background_layer.get());

        for (auto* output : scene_list_outputs(scene.get())) {
            vec2f32 image_size = background_image->extent;
            auto viewport = scene_output_get_viewport(output);

            // Create input sink
            auto input = scene_input_region_create(background_client.get());
            scene_input_region_set_region(input.get(), {viewport});
            scene_node_set_transform(input.get(), scene_get_root_transform(scene.get()));
            scene_tree_place_above(background_layer.get(), nullptr, input.get());

            // Create texture node
            auto texture = scene_texture_create(scene.get());
            scene_texture_set_image(texture.get(), background_image.get(), sampler.get(), gpu_blend_mode::premultiplied);
            auto src = core_rect_fit<f32>(image_size, viewport.extent);
            scene_texture_set_src(texture.get(), {src.origin / image_size, src.extent / image_size, core_xywh});
            scene_texture_set_dst(texture.get(), viewport);
            scene_node_set_transform(texture.get(), root);
            scene_tree_place_above(background_layer.get(), nullptr, texture.get());
        }
    };

    scene_client_set_event_handler(background_client.get(), [scene = scene.get(), &update_backgrounds](scene_event* event) {
        switch (event->type) {
            break;case scene_event_type::pointer_button:
                if (event->key.pressed) {
                    log_warn("Background clicked, dropping keyboard grabs");
                    scene_keyboard_clear_focus(scene);
                }
            break;case scene_event_type::output_layout:
                update_backgrounds();
            break;default:
                ;
        }
    });

    // Test client

    auto client = scene_client_create(scene.get());

    auto window = scene_window_create(client.get());
    auto initial_size = vec2f32{256, 256};
    scene_window_set_frame(window.get(), {{64, 64}, initial_size, core_xywh});

    auto canvas = scene_texture_create(scene.get());
    scene_texture_set_tint(canvas.get(), {255, 0, 255, 255});
    scene_texture_set_dst(canvas.get(), {{}, initial_size, core_xywh});
    scene_node_set_transform(canvas.get(), scene_window_get_transform(window.get()));
    scene_tree_place_below(scene_window_get_tree(window.get()), nullptr, canvas.get());

    auto input = scene_input_region_create(client.get());
    scene_input_region_set_region(input.get(), {{{}, initial_size, core_xywh}});
    scene_node_set_transform(input.get(), scene_window_get_transform(window.get()));
    scene_tree_place_above(scene_window_get_tree(window.get()), nullptr, input.get());

    auto transform = scene_transform_create(scene.get());
    scene_node_set_transform(transform.get(), scene_window_get_transform(window.get()));
    scene_transform_update(transform.get(), {64, 64}, 1);

    auto square = scene_texture_create(scene.get());
    scene_texture_set_tint(square.get(), {0, 255, 255, 255});
    scene_texture_set_dst(square.get(), {{}, {128, 128}, core_xywh});
    scene_node_set_transform(square.get(), transform.get());
    scene_tree_place_above(scene_window_get_tree(window.get()), nullptr, square.get());

    scene_client_set_event_handler(client.get(), [&](scene_event* event) {
        switch (event->type) {
            break;case scene_event_type::keyboard_key:
                log_trace("keyboard_key({}, {})",
                    libevdev_event_code_get_name(EV_KEY, event->key.code),
                    event->key.pressed ? "pressed" : "released");
            break;case scene_event_type::keyboard_modifier:
                log_trace("keyboard_modifier({})", core_to_string(scene_keyboard_get_modifiers(scene.get())));
            break;case scene_event_type::pointer_motion:
                log_trace("pointer_motion(accel: {}, unaccel: {}, pos: {})",
                    core_to_string(event->pointer.motion.rel_accel),
                    core_to_string(event->pointer.motion.rel_unaccel),
                    core_to_string(scene_pointer_get_position(scene.get())));
            break;case scene_event_type::pointer_button:
                log_trace("pointer_button({}, {})",
                    libevdev_event_code_get_name(EV_KEY, event->key.code),
                    event->key.pressed ? "pressed" : "released");
                scene_keyboard_grab(client.get());
                scene_window_raise(window.get());
            break;case scene_event_type::pointer_scroll:
                log_trace("pointer_scroll(delta: {})", core_to_string(event->pointer.scroll.delta));
            break;case scene_event_type::focus_pointer:
                log_trace("focus_pointer({} -> {})", (void*)event->focus.lost.client, (void*)event->focus.gained.client);
            break;case scene_event_type::focus_keyboard:
                log_trace("focus_keyboard({} -> {})", (void*)event->focus.lost.client, (void*)event->focus.gained.client);
            break;case scene_event_type::window_reposition: {
                auto frame = event->window.reposition.frame;
                scene_texture_set_dst(       canvas.get(), {{}, frame.extent, core_xywh});
                scene_input_region_set_region(input.get(), {{{}, frame.extent, core_xywh}});
                scene_window_set_frame(event->window.window, frame);
            }
            break;case scene_event_type::output_frame:
                  case scene_event_type::output_added:
                  case scene_event_type::output_removed:
                  case scene_event_type::output_configured:
                  case scene_event_type::output_damaged:
                  case scene_event_type::output_layout:
                  case scene_event_type::hotkey:
                ;
        }
    });

    scene_window_map(window.get());

    // Wayland

    auto way = way_create(event_loop.get(), gpu.get(), scene.get());

    // ImGui

    auto imui = imui_create(gpu.get(), scene.get());
    imui_add_frame_handler(imui.get(), [&] {
        ImGui::ShowDemoWindow();

        defer { ImGui::End(); };
        if (ImGui::Begin("Roc")) {
            if (ImGui::Button("New Output")) {
                io_add_output(io.get());
            }

            if (ImGui::Button("Reposition")) {
                if (auto* window = imui_get_window(ImGui::GetCurrentWindow())) {
                    scene_window_request_reposition(window, {{}, {512, 512}, core_xywh}, {});
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
                        auto texture = gpu_image_create(gpu.get(), viewport.extent, gpu_format_from_drm(DRM_FORMAT_ABGR8888), gpu_image_usage::render);
                        scene_render(scene.get(), texture.get(), viewport);
                    }
                    gpu->renderdoc->EndFrameCapture(nullptr, nullptr);
                }
            }

            if (ImGui::Button("Print Scene Graph")) {
                u32 depth = 0;
                auto indent = [&] { return std::string(depth, ' '); };
                scene_iterate(scene_get_layer(scene.get(), scene_layer::window)->parent,
                    scene_iterate_direction::back_to_front,
                    [&](scene_tree* tree) {
                        if (auto* surface = dynamic_cast<way_surface*>(tree->userdata)) {
                            log_warn("{}tree({}{}) {{", indent(),
                                core_enum_to_string(surface->role),
                                tree->enabled ? "": ", disabled");
                        } else {
                            log_warn("{}tree{} {{", indent(), tree->enabled ? "": "(disabled)");
                        }
                        depth += 2;
                        return scene_iterate_action::next;
                    },
                    [&](scene_node* node) {
                        log_warn("{}{}", indent(), core_enum_to_string(node->type));
                        return scene_iterate_action::next;
                    },
                    [&](scene_tree* tree) {
                        depth -= 2;
                        log_warn("{}}}", indent());
                        return scene_iterate_action::next;
                    });
            }
        }
    });
    imui_request_frame(imui.get());

    // Hotkey

    auto main_mod = scene_modifier::alt;
    auto hotkey_client = scene_client_create(scene.get());
    ankerl::unordered_dense::map<scene_hotkey, std::string> hotkeys;
    hotkeys[{ main_mod,                         KEY_SPACE  }] = "launcher";
    hotkeys[{ main_mod,                         KEY_Q      }] = "close-focused";
    hotkeys[{ main_mod,                         KEY_S      }] = "clear-focus";
    hotkeys[{ main_mod,                         BTN_MIDDLE }] = "close-under-cursor";
    for (auto[hotkey, _] : hotkeys) {
        core_assert(scene_client_hotkey_register(hotkey_client.get(), hotkey));
    }
    scene_client_set_event_handler(hotkey_client.get(), [&](scene_event* event) {
        if (event->type == scene_event_type::hotkey) {
            log_debug("hotkey({} - {})", hotkeys.at(event->hotkey.hotkey), event->hotkey.pressed ? "pressed" : "released");
        }
    });

    // Run

    io_run(io.get());
}
