#include "wroc.hpp"

#include "core/object.hpp"

#include "gpu/gpu.hpp"

#include "wroc_blit_shader.hpp"
#include "shaders/blit.h"

#include "wroc/event.hpp"

wroc_renderer::~wroc_renderer() = default;

static
void register_format(wroc_renderer* renderer, gpu_format format)
{
    auto gpu = server->gpu;

    if (!format->is_ycbcr) {
        if (gpu_get_format_props(gpu, format, gpu_image_usage::texture | gpu_image_usage::transfer)->opt_props.get()) {
            renderer->shm_formats.add(format, DRM_FORMAT_MOD_LINEAR);
        }
    }

    for (auto& props : gpu_get_format_props(gpu, format, gpu_image_usage::texture | gpu_image_usage::transfer_src)->mod_props) {
        if (props.ext_mem_props.externalMemoryFeatures & VK_EXTERNAL_MEMORY_FEATURE_IMPORTABLE_BIT) {
            renderer->dmabuf_formats.add(format, props.modifier);
        }
    }
}

ref<wroc_renderer> wroc_renderer_create(flags<wroc_render_option> render_options)
{
    auto renderer = core_create<wroc_renderer>();
    renderer->options = render_options;

    for (auto format : gpu_get_formats()) {
        register_format(renderer.get(), format);
    }

    wroc_renderer_init_buffer_feedback(renderer.get());

    auto* gpu = server->gpu;

    renderer->output_format = gpu_format_from_drm(DRM_FORMAT_ABGR8888);
    renderer->output_format_modifiers = server->backend->get_output_format_set().get(renderer->output_format);
    core_assert(!renderer->output_format_modifiers.empty());

    std::filesystem::path path = getenv("WALLPAPER");

    int w, h;
    int num_channels;
    stbi_uc* data = stbi_load(path.c_str(), &w, &h, &num_channels, STBI_rgb_alpha);
    defer { stbi_image_free(data); };

    log_info("Loaded image ({}, width = {}, height = {})", path.c_str(), w, h);

    renderer->background = gpu_image_create(gpu, {
        .extent = {w, h},
        .format = gpu_format_from_drm(DRM_FORMAT_ABGR8888),
        .usage = gpu_image_usage::texture | gpu_image_usage::transfer
    });
    gpu_image_update_immed(renderer->background.get(), data);

    renderer->sampler = gpu_sampler_create(gpu, {
        .mag = VK_FILTER_NEAREST,
        .min = VK_FILTER_LINEAR,
    });

    renderer->vertex = gpu_shader_create(gpu, {
        .stage = VK_SHADER_STAGE_VERTEX_BIT,
        .code = wroc_blit_shader,
        .entry = "vertex"
    });
    renderer->fragment = gpu_shader_create(gpu, {
        .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
        .code = wroc_blit_shader,
        .entry = "fragment"
    });

    return renderer;
}

void render(wroc_renderer* renderer, gpu_commands* commands, wroc_renderer_frame_data* frame, gpu_image* current, rect2f64 scene_rect)
{
    auto* gpu = commands->queue->gpu;
    auto cmd = commands->buffer;

    wroc_coord_space space {
        .origin = scene_rect.origin,
        .scale = scene_rect.extent / vec2f64(current->extent()),
    };

    VkExtent2D vk_extent = { current->extent().x, current->extent().y };
    vec2f32 current_extent = current->extent();

    gpu->vk.CmdBeginRendering(cmd, ptr_to(VkRenderingInfo {
        .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
        .renderArea = { {}, vk_extent },
        .layerCount = 1,
        .colorAttachmentCount = 1,
        .pColorAttachments = ptr_to(VkRenderingAttachmentInfo {
            .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
            .imageView = current->view(),
            .imageLayout = VK_IMAGE_LAYOUT_GENERAL,
            .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
            .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
            .clearValue = {.color{.float32{0.f, 0.f, 0.f, 1.f}}},
        }),
    }));

    gpu_cmd_set_viewports(commands, {{{}, current_extent, core_xywh}});
    gpu->vk.CmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, gpu->pipeline_layout, 0, 1, &gpu->set, 0, nullptr);

    auto start_draws = [&] {
        gpu_cmd_set_scissors(commands, {{{}, current->extent(), core_xywh}});
        gpu_cmd_bind_shaders(commands, {renderer->vertex.get(), renderer->fragment.get()});
        gpu_cmd_reset_graphics_state(commands);
        gpu_cmd_set_blend_state(commands, {gpu_blend_mode::premultiplied});
    };

    u32 rect_id_start = 0;
    u32 rect_id = 0;

    renderer->rects_cpu.clear();

    auto draw = [&](gpu_image* image, rect2f64 dest, rect2f64 source, vec4f32 color = {1, 1, 1, 1}) {

        // log_debug("draw(dest = {}, source = {})", core_to_string(dest), core_to_string(source));

        gpu_commands_protect_object(commands, image);

        auto pixel_dst = core_round<i32, f64>(space.from_global(dest));

        rect_id++;
        renderer->rects_cpu.emplace_back(wroc_shader_rect {
            .image = image ? image4f32{image, renderer->sampler.get()} : image4f32 {},
            .image_rect = source,
            .rect = pixel_dst,
            .opacity = 1.f,
            .color = color,
        });
    };

    auto flush_draws = [&] {
        if (rect_id_start == rect_id) return;

        if (frame->rects.count < rect_id) {
            auto new_size = core_compute_geometric_growth(frame->rects.count, rect_id);
            log_debug("Renderer - reallocating rect buffer, size: {}", new_size);
            if (frame->rects.buffer && rect_id_start) {
                // Previous draws using this buffer, keep alive until all draws complete
                log_debug("  previous buffer still used in draws ({}), keeping...", rect_id_start);
                gpu_commands_protect_object(commands, frame->rects.buffer.get());
            }
            frame->rects = {gpu_buffer_create(gpu, new_size * sizeof(wroc_shader_rect), {}), new_size};
        }

        if (!renderer->rects_cpu.empty()) {
            std::memcpy(frame->rects.host() + rect_id_start, renderer->rects_cpu.data() + rect_id_start, (rect_id - rect_id_start) * sizeof(wroc_shader_rect));
        }

        wroc_shader_rect_input si = {};
        si.rects = frame->rects.device();
        si.output_size = current_extent;
        gpu_cmd_push_constants(commands, 0, sizeof(si), &si);
        gpu->vk.CmdDraw(cmd, 6 * (rect_id - rect_id_start), 1, rect_id_start * 6, 0);
        rect_id_start = rect_id;
    };

    start_draws();

    // Background

    for (auto& output : server->output_layout->outputs) {
        auto src = core_rect_fit<f64>(server->renderer->background->extent(), output->layout_rect.extent);
        draw(server->renderer->background.get(), output->layout_rect, src);
    }

    // Surface helper

    auto draw_surface = [&](this auto&& draw_surface, wroc_surface* surface, vec2f64 pos, vec2f64 scale, f64 opacity = 1.0) -> void {
        if (!surface || !surface->mapped) return;

        // log_trace("drawing surface: {} (stack.size = {})", (void*)surface, surface->current.surface_stack.size());

        for (auto&[s, s_pos] : surface->current.surface_stack) {
            if (s.get() == surface) {

                // Draw self
                rect2f64 dst = surface->buffer_dst;
                dst.origin *= scale;
                dst.extent *= scale;
                dst.origin += pos;

                draw(surface->current.buffer->image.get(), dst, surface->buffer_src, vec4f32(opacity, opacity, opacity, opacity));

            } else {

                // Draw subsurface
                draw_surface(s.get(), pos + vec2f64(s_pos) * scale, scale, opacity);
            }
        }
    };

    // Draw xdg surfaces

    auto draw_xdg_surfaces = [&](bool show_cycled) {
        for (auto* surface : server->surfaces) {
            if (!surface->mapped) continue;

            auto* xdg_surface = wroc_surface_get_addon<wroc_xdg_surface>(surface);
            if (!xdg_surface) continue;

            auto[pos, scale] = wroc_surface_get_coord_space(surface);

            f32 opacity = 1.0;
            if (server->interaction_mode == wroc_interaction_mode::focus_cycle) {
                if (surface == server->focus.cycled.get()) {
                    if (!show_cycled) continue;
                } else {
                    if (show_cycled) continue;
                    opacity = 0.2;
                }
            }

            draw_surface(surface, pos, scale, opacity);

            auto* toplevel = wroc_surface_get_addon<wroc_toplevel>(surface);
            if (toplevel && !toplevel->fullscreen.output) {

                // Draw focus border above toplevels
                f64     width = 2.0;
                vec4f32 color = surface == server->seat->keyboard->focused_surface.get()
                    ? vec4f32{0.4, 0.4, 1.0, 1.0}
                    : vec4f32{0.3, 0.3, 0.3, 1.0};
                color *= opacity;

                aabb2f64 r = wroc_toplevel_get_layout_rect(toplevel);
                draw(nullptr, /* left   */ { r.min - width,     {r.min.x, r.max.y + width}, core_minmax}, {}, color);
                draw(nullptr, /* right  */ {{r.max.x, r.min.y - width},  r.max + width,     core_minmax}, {}, color);
                draw(nullptr, /* top    */ {{r.min.x, r.min.y - width}, {r.max.x, r.min.y}, core_minmax}, {}, color);
                draw(nullptr, /* bottom */ {{r.min.x, r.max.y}, {r.max.x, r.max.y + width}, core_minmax}, {}, color);
            }
        }
    };

    draw_xdg_surfaces(false);
    if (server->interaction_mode == wroc_interaction_mode::focus_cycle) {
        draw_xdg_surfaces(true);
    }

    // Draw ImGui

    {
        flush_draws();
        wroc_imgui_render(server->imgui.get(), commands, scene_rect, current->extent());
        start_draws();
    }

    // Draw zone selection

    if (server->interaction_mode == wroc_interaction_mode::zone) {
        auto& c = server->zone.config;
        vec4f32 color = server->zone.selecting ? c.color_selected : c.color_initial;
        color = {vec3f32(color) * color.a, color.a};
        draw(nullptr, server->zone.final_zone, {}, color);
    }

    // Draw drag icon

    if (auto& icon = server->data_manager.drag.icon) {
        draw_surface(icon->surface.get(), server->seat->pointer->position, vec2f64(1.0));
    }

    // Draw cursor

    {
        auto* pointer = server->seat->pointer.get();

        auto* surface = server->imgui->wants_mouse
            ? wroc_cursor_get_shape(server->cursor.get(), server->imgui->cursor_shape)
            : wroc_cursor_get_current(pointer, server->cursor.get());

        if (surface) {
            draw_surface(surface, pointer->position, vec2f64(1.0));
        }
    }

    if (renderer->debug.show_debug_cursor) {

        // Debug cursor crosshair

        auto* pointer = server->seat->pointer.get();
        auto pos = pointer->position;

        vec4f32 color = {0.5, 0, 0, 0.5};
        auto length = 24;
        auto width = 2;

        auto hlength = length / 2;
        auto hwidth = width / 2;

        draw(nullptr, {pos - vec2f64{hwidth, hlength}, vec2f64{width, length}, core_xywh}, {}, color);
        draw(nullptr, {pos - vec2f64{hlength, hwidth}, vec2f64{length, width}, core_xywh}, {}, color);
    }

    // Finish

    flush_draws();

    gpu->vk.CmdEndRendering(cmd);
}

static
void on_screenshot_ready(std::chrono::steady_clock::time_point start, gpu_image* image, gpu_buffer* buffer)
{
    auto save_start = std::chrono::steady_clock::now();
    log_info("Screenshot captured in {}, saving...", core_duration_to_string(save_start - start));

    auto save_path = "screenshot.png";

    stbi_write_png(save_path, image->extent().x, image->extent().y, STBI_rgb_alpha, buffer->host_address, image->extent().x * 4);
    auto save_end = std::chrono::steady_clock::now();
    log_info("Screenshot saved in {}", core_duration_to_string(save_end - save_start));

    core_event_loop_enqueue(server->event_loop.get(), [image, buffer] {
        log_debug("Deleting screenshot resources");
        server->renderer->screenshot_queued = false;
        core_remove_ref(image);
        core_remove_ref(buffer);
    });
}

void wroc_screenshot(rect2f64 rect)
{
    auto* renderer = server->renderer.get();
    auto* gpu = server->gpu;

    log_info("Taking screenshot of region {}", core_to_string(rect));

    auto start = std::chrono::steady_clock::now();

    vec2u32 extent = rect.extent;
    auto image = gpu_image_create(gpu, {
        .extent = extent,
        .format = gpu_format_from_drm(DRM_FORMAT_ABGR8888),
        .usage = gpu_image_usage::render | gpu_image_usage::transfer
    });

    auto byte_size = usz(4) * extent.x * extent.y;
    auto buffer = gpu_buffer_create(gpu, byte_size, gpu_buffer_flag::host);

    // Completion handler

    struct screenshot_guard : core_object
    {
        wroc_renderer_frame_data frame = {};
        std::chrono::steady_clock::time_point start;
        ref<gpu_image> image;
        ref<gpu_buffer> buffer;

        ~screenshot_guard()
        {
            std::thread{on_screenshot_ready, start, core_add_ref(image.get()), core_add_ref(buffer.get())}.detach();
        }
    };
    auto guard = core_create<screenshot_guard>();
    guard->start = start;
    guard->image = image;
    guard->buffer = buffer;

    // Render

    auto render_queue = gpu_get_queue(gpu, gpu_queue_type::graphics);
    auto render_commands = gpu_commands_begin(render_queue);
    render(renderer, render_commands.get(), &guard->frame, image.get(), rect);
    gpu_commands_protect_object(render_commands.get(), guard.get());
    auto render_done = gpu_commands_submit(render_commands.get(), {});

    // Transfer

    auto transfer_queue = gpu_get_queue(gpu, gpu_queue_type::transfer);
    auto transfer_commands = gpu_commands_begin(transfer_queue);
    gpu_copy_image_to_buffer(transfer_commands.get(), buffer.get(), image.get());
    gpu_commands_protect_object(transfer_commands.get(), guard.get());
    gpu_commands_submit(transfer_commands.get(), {render_done});
}

bool wroc_output_try_prepare_acquire(wroc_output* output)
{
    auto& swapchain = output->swapchain;

    // Clear out any incorrectly sized images
    std::erase_if(swapchain.free_images, [&](auto& i) { return i->extent() != output->size; });

    // Clear out any other free images if we have too many in flight
    auto total_images = swapchain.free_images.size() + swapchain.images_in_flight;
    if (total_images > server->renderer->max_swapchain_images && !swapchain.free_images.empty()) {
        swapchain.free_images.erase(swapchain.free_images.end()
            - std::min<usz>(swapchain.free_images.size(), total_images - server->renderer->max_swapchain_images));
    }

    // Return true if we have a free image, or a free slot to allocate a new one
    return !swapchain.free_images.empty() ||swapchain.images_in_flight < server->renderer->max_swapchain_images;
}

static
ref<gpu_image> acquire(wroc_renderer* renderer, wroc_output* output)
{
    auto& swapchain = output->swapchain;

    swapchain.images_in_flight++;

    if (!swapchain.free_images.empty()) {
        auto image = std::move(swapchain.free_images.back());
        swapchain.free_images.pop_back();
        return image;
    }

    log_warn("Creating new swapchain image {}", core_to_string(output->size));
    core_assert(swapchain.images_in_flight <= renderer->max_swapchain_images);

    auto* gpu = server->gpu;

    auto image = gpu_image_create(gpu, {
        .extent = output->size,
        .format = renderer->output_format,
        .usage = gpu_image_usage::render,
        .modifiers = &renderer->output_format_modifiers
    });

    return image;
}

static
void release(wroc_output* output, u32 slot_idx, u64 signalled)
{
    auto& slot = output->swapchain.release_slots[slot_idx];

    core_assert(signalled == slot.release_point);

    output->swapchain.free_images.emplace_back(std::move(slot.image));
    output->swapchain.images_in_flight--;

    wroc_output_try_dispatch_frame(output);
}

static
void present(wroc_output* output, gpu_image* image, gpu_syncpoint acquire)
{
    auto& swapchain = output->swapchain;

    auto slot = std::ranges::find_if(swapchain.release_slots, [](auto& s) { return !s.image; });
    if (slot == swapchain.release_slots.end()) {
        slot = swapchain.release_slots.insert(swapchain.release_slots.end(), wroc_output::release_slot {
            .semaphore = gpu_semaphore_create(server->gpu),
        });
    }

    slot->image = image;
    slot->release_point++;

    flags<wroc_output_commit_flag> flags = {};
    if (server->renderer->vsync) flags |= wroc_output_commit_flag::vsync;
    output->commit(image, acquire, {slot->semaphore.get(), slot->release_point}, flags);

    auto slot_idx = std::distance(swapchain.release_slots.begin(), slot);
    gpu_semaphore_wait_value(slot->semaphore.get(), slot->release_point,
        [output = weak(output), slot_idx](u64 signalled) {
            if (output) release(output.get(), slot_idx,signalled);
        });
}

void wroc_render_frame(wroc_output* output)
{
    core_assert(output->frame_available);
    core_assert(output->frames_in_flight < server->renderer->max_frames_in_flight);
    core_assert(output->size.x && output->size.y);

    auto* renderer = server->renderer.get();
    auto* gpu = server->gpu;

    auto current = acquire(renderer, output);

    output->frames_in_flight++;

    auto queue = gpu_get_queue(gpu, gpu_queue_type::graphics);
    auto commands = gpu_commands_begin(queue);

    struct frame_guard : core_object
    {
        wroc_renderer_frame_data frame_data;
        weak<wroc_output> output;

        ~frame_guard()
        {
            server->renderer->available_frames.emplace_back(std::move(frame_data));

            if (output) {
                output->frames_in_flight--;
                wroc_output_try_dispatch_frame_later(output.get());
            }
        }
    };
    auto guard = core_create<frame_guard>();
    guard->output = output;
    gpu_commands_protect_object(commands.get(), guard.get());

    wroc_renderer_frame_data* frame = &guard->frame_data;
    if (!renderer->available_frames.empty()) {
        *frame = std::move(renderer->available_frames.back());
        renderer->available_frames.pop_back();
    } else {
        log_debug("Allocating new renderer frame data");
    }

    // TODO: We should update imgui frames based on events, instead of this hack
    if (output == server->output_layout->outputs[0].get()) {
        wroc_imgui_frame(server->imgui.get(), output->layout_rect);
    }

    // Prepare

    // TODO: QFOTs for DMABUF swapchains

    render(renderer, commands.get(), frame, current.get(), output->layout_rect);

    // Submit

    auto render_done = gpu_commands_submit(commands.get(), {});

    // Present

    present(output, current.get(), render_done);

    // Send frame callbacks

    auto elapsed = wroc_get_elapsed_milliseconds();

    for (wroc_surface* surface : server->surfaces) {
        if (surface->role == wroc_surface_role::none || !surface->role_addon) continue;

        auto surface_output = wroc_output_layout_output_for_surface(server->output_layout.get(), surface);

        // Only dispatch frame callbacks for the surface's primary output
        if (surface_output != output) continue;

        auto dispatch = [&](wroc_resource_list& callbacks) {
            while (auto* callback = callbacks.front()) {
                wroc_send(wl_callback_send_done, callback, elapsed);
                wl_resource_destroy(callback);
            }
        };

        // Frame callbacks are a special case of surface state.
        // We want to dispatch *all* committed frame callbacks,
        // even if the frame hasn't been rendered yet, to ensure
        // smooth frame pacing even if a buffer is delayed.
        dispatch(surface->current.frame_callbacks);
        for (auto& packet : surface->cached) {
            if (packet.id) {
                dispatch(packet.state.frame_callbacks);
            }
        }
    }
}
