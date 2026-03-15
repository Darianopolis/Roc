#include "wm.hpp"

#include "scene/scene.hpp"

#include "io/io.hpp"

#include "imui/imui.hpp"
#include "way/way.hpp"
#include "way/internal.hpp"

int main()
{
    // Systems

    auto event_loop = core::event_loop::create();
    auto gpu = gpu::create({}, event_loop.get());
    auto io = io_create(event_loop.get(), gpu.get());
    auto scene = scene_create(gpu.get(), io.get());
    auto wm = wm_create(scene.get());

    // I/O event plumbing

    auto image_pool = gpu::image_pool::create(gpu.get());
    auto output_client = scene_client_create(scene.get());
    struct output {
        core::Ref<scene_output> scene;
        io_output*        io;
    };
    std::vector<output> outputs;
    auto reflow_outputs = [&] {
        f32 x = 0;
        for (auto& output : outputs) {
            auto size = output.io->info().size;
            scene_output_set_viewport(output.scene.get(), {{x, 0.f}, size, core::xywh});
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
                scene_frame(scene.get(), output->scene.get(), output->io, image_pool.get());
            }
        }
    });
    scene_client_set_event_handler(output_client.get(), [&](scene_event* event) {
        switch (event->type) {
            break;case scene_event_type::output_frame_request: {
                auto output = std::ranges::find_if(outputs, [&](auto& p) { return p.scene.get() == event->output; });
                output->io->request_frame();
            }
            break;default:
                ;
        }
    });

    // Pointer

    scene_pointer_set_xcursor(scene_get_pointer(scene.get()), "default");

    scene_pointer_set_driver(scene_get_pointer(scene.get()), [scene = scene.get()](scene_pointer_driver_in in) -> scene_pointer_driver_out {

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

    auto sampler = gpu::sampler::create(gpu.get(), {
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
        auto image = gpu::image::create(gpu.get(), {
            .extent = {w, h},
            .format = gpu::format::from_drm(DRM_FORMAT_XBGR8888),
            .usage = gpu::ImageUsage::texture | gpu::ImageUsage::transfer
        });
        gpu::copy_memory_to_image(image.get(), {data, usz(w * h * 4)}, {{{image->extent()}}});
        return image;
    }();

    auto background_client = scene_client_create(scene.get());

    core::Ref<scene_tree> background_layer = {};
    auto unparent_background = [&] {
        if (background_layer) scene_node_unparent(background_layer.get());
    };
    defer { unparent_background(); };
    auto update_backgrounds = [&] {
        unparent_background();
        background_layer = scene_tree_create(scene.get());
        scene_tree_place_above(scene_get_layer(scene.get(), scene_layer::background), nullptr, background_layer.get());

        for (auto* output : scene_list_outputs(scene.get())) {
            vec2f32 image_size = background_image->extent();
            auto viewport = scene_output_get_viewport(output);

            // Create texture node
            auto texture = scene_texture_create(scene.get());
            scene_texture_set_image(texture.get(), background_image.get(), sampler.get(), gpu::BlendMode::premultiplied);
            auto src = core::rect::fit<f32>(image_size, viewport.extent);
            scene_texture_set_src(texture.get(), {src.origin / image_size, src.extent / image_size, core::xywh});
            scene_texture_set_dst(texture.get(), viewport);
            scene_tree_place_above(background_layer.get(), nullptr, texture.get());
        }
    };

    scene_client_set_event_handler(background_client.get(), [scene = scene.get(), &update_backgrounds](scene_event* event) {
        switch (event->type) {
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
    scene_window_set_frame(window.get(), {{64, 64}, initial_size, core::xywh});

    auto canvas = scene_texture_create(scene.get());
    scene_texture_set_tint(canvas.get(), {255, 0, 255, 255});
    scene_texture_set_dst(canvas.get(), {{}, initial_size, core::xywh});
    scene_tree_place_below(scene_window_get_tree(window.get()), nullptr, canvas.get());

    auto input = scene_input_region_create(client.get());
    scene_input_region_set_region(input.get(), {{{}, initial_size, core::xywh}});
    scene_tree_place_above(scene_window_get_tree(window.get()), nullptr, input.get());

    auto inner = scene_tree_create(scene.get());
    scene_tree_set_translation(inner.get(), {64, 64});
    scene_tree_place_above(scene_window_get_tree(window.get()), nullptr, inner.get());

    auto square = scene_texture_create(scene.get());
    scene_texture_set_tint(square.get(), {0, 255, 255, 255});
    scene_texture_set_dst(square.get(), {{}, {128, 128}, core::xywh});
    scene_tree_place_above(inner.get(), nullptr, square.get());

    scene_client_set_event_handler(client.get(), [&](scene_event* event) {
        switch (event->type) {
            break;case scene_event_type::keyboard_key:
                log_trace("keyboard_key({}, {})",
                    libevdev_event_code_get_name(EV_KEY, event->keyboard.key.code),
                    event->keyboard.key.pressed ? "pressed" : "released");
            break;case scene_event_type::keyboard_modifier:
                log_trace("keyboard_modifier({})", core::to_string(scene_keyboard_get_modifiers(event->keyboard.keyboard)));
            break;case scene_event_type::pointer_motion:
                log_trace("pointer_motion(accel: {}, unaccel: {}, pos: {})",
                    core::to_string(event->pointer.motion.rel_accel),
                    core::to_string(event->pointer.motion.rel_unaccel),
                    core::to_string(scene_pointer_get_position(event->pointer.pointer)));
            break;case scene_event_type::pointer_button:
                log_trace("pointer_button({}, {})",
                    libevdev_event_code_get_name(EV_KEY, event->pointer.button.code),
                    event->pointer.button.pressed ? "pressed" : "released");
                scene_window_raise(window.get());
            break;case scene_event_type::pointer_scroll:
                log_trace("pointer_scroll(delta: {})", core::to_string(event->pointer.scroll.delta));
            break;case scene_event_type::pointer_enter:
                log_trace("pointer_enter({}, region: {})", (void*)event->pointer.pointer, (void*)event->pointer.focus.region);
            break;case scene_event_type::pointer_leave:
                log_trace("pointer_leave({})", (void*)event->pointer.pointer);
            break;case scene_event_type::keyboard_enter:
                log_trace("keyboard_enter({})", (void*)event->keyboard.keyboard);
            break;case scene_event_type::keyboard_leave:
                log_trace("keyboard_leave({})", (void*)event->keyboard.keyboard);
            break;case scene_event_type::window_reposition: {
                auto frame = event->window.reposition.frame;
                scene_texture_set_dst(       canvas.get(), {{}, frame.extent, core::xywh});
                scene_input_region_set_region(input.get(), {{{}, frame.extent, core::xywh}});
                scene_window_set_frame(event->window.window, frame);
            }
            break;case scene_event_type::output_frame:
                  case scene_event_type::output_added:
                  case scene_event_type::output_removed:
                  case scene_event_type::output_configured:
                  case scene_event_type::output_frame_request:
                  case scene_event_type::output_layout:
                ;
            break;case scene_event_type::hotkey:
                ;
            break;case scene_event_type::selection:
                log_trace("selection({})", (void*)event->data.source);
        }
    });

    scene_window_map(window.get());

    // Wayland

    auto way = way_create(event_loop.get(), gpu.get(), scene.get());

    // ImGui

    std::string imui_text_edit = "Hello, world!";
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
                    scene_window_request_reposition(window, {{}, {512, 512}, core::xywh}, {});
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
                        auto texture = gpu::image::create(gpu.get(), {
                            .extent = viewport.extent,
                            .format = gpu::format::from_drm(DRM_FORMAT_ABGR8888),
                            .usage = gpu::ImageUsage::render
                        });
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
                        way_surface* surface;
                        if (tree->system == way->scene_system
                                && (surface = way_get_userdata<way_surface>(tree))) {
                            log_warn("{}tree({}{}) {{", indent(),
                                core::to_string(surface->role),
                                tree->enabled ? "": ", disabled");
                        } else {
                            log_warn("{}tree{} {{", indent(), tree->enabled ? "": "(disabled)");
                        }
                        depth += 2;
                        return scene_iterate_action::next;
                    },
                    [&](scene_node* node) {
                        log_warn("{}{}", indent(), core::to_string(node->type));
                        return scene_iterate_action::next;
                    },
                    [&](scene_tree* tree) {
                        depth -= 2;
                        log_warn("{}}}", indent());
                        return scene_iterate_action::next;
                    });
            }

            ImGui::InputText("Text", &imui_text_edit);
        }
    });
    imui_request_frame(imui.get());

    // Hotkey

    auto main_mod = scene_modifier::alt;
    auto hotkey_client = scene_client_create(scene.get());
    core::Map<scene_hotkey, std::string> hotkeys;
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

    // Selection

    auto data_client = scene_client_create(scene.get());
    scene_client_set_event_handler(data_client.get(), [](scene_event*) {});
    auto data_source = scene_data_source_create(data_client.get(), {
        .send = [&](const char* mime, int fd) {
            std::string message = "This is a test clipboard message.";
            write(fd, message.data(), message.size());
        }
    });
    scene_data_source_offer(data_source.get(), "text/plain;charset=utf-8");
    scene_data_source_offer(data_source.get(), "text/plain");
    scene_data_source_offer(data_source.get(), "text/html");
    scene_set_selection(scene.get(), data_source.get());

    // Run

    io_run(io.get());
}
