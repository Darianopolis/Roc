#include "internal.hpp"

#include "io/io.hpp"

#include "core/math.hpp"

static
void reflow_outputs(WindowManager* wm, bool any_changed = false)
{
    f32 x = 0;
    for (auto* output : wm->io.outputs) {
        auto size = output->io->info().size;
        auto last = output->viewport;
        output->viewport = {{x, 0.f}, size, xywh};
        x += f32(size.x);

        if (last != output->viewport) {
            any_changed = true;
            wm_post_output_event(wm, ptr_to(WmOutputEvent {
                .type = WmEventType::output_configured,
                .output = output,
            }));
        }
    }

    if (any_changed) {
        wm_post_output_event(wm, ptr_to(WmOutputEvent {
            .type = WmEventType::output_layout,
        }));
    }
}

static
WmOutput* find_output_for_io(WindowManager* wm, IoOutput* io_output)
{
    for (auto* output : wm->io.outputs) {
        if (output->io == io_output) {
            return output;
        }
    }
    return nullptr;
}

static
void handle_io_event(WindowManager* wm, IoEvent* event)
{
    switch (event->type) {
        // shutdown
        break;case IoEventType::shutdown_requested:
            io_stop(wm->io.context);

        // input
        break;case IoEventType::input_added:
                case IoEventType::input_removed:
                case IoEventType::input_event:
            scene_push_io_event(wm->scene.get(), event);

        // output
        break;case IoEventType::output_added: {
            auto output = ref_create<WmOutput>(event->output.output);
            wm->io.outputs.emplace_back(output.get());
            wm_post_output_event(wm, ptr_to(WmOutputEvent {
                .type = WmEventType::output_added,
                .output = output.get(),
            }));
            reflow_outputs(wm, true);
        }
        break;case IoEventType::output_configure:
            reflow_outputs(wm);
        break;case IoEventType::output_removed: {
            Ref output = find_output_for_io(wm, event->output.output);
            wm->io.outputs.erase_if([&](auto* p) { return p->io == event->output.output; });
            wm_post_output_event(wm, ptr_to(WmOutputEvent {
                .type = WmEventType::output_removed,
                .output = output.get(),
            }));
            reflow_outputs(wm, true);
        }

        break;case IoEventType::output_frame: {
            auto output = find_output_for_io(wm, event->output.output);

            wm_post_output_event(wm, ptr_to(WmOutputEvent {
                .type = WmEventType::output_frame,
                .output = output,
            }));

            // TODO: Only redraw with damage

            auto format = gpu_format_from_drm(DRM_FORMAT_ABGR8888);
            auto usage = GpuImageUsage::render;

            auto target = wm->io.pool->acquire({
                .extent = output->io->info().size,
                .format = format,
                .usage = usage,
                .modifiers = ptr_to(gpu_intersect_format_modifiers({
                    &gpu_get_format_properties(wm->gpu, format, usage)->mods,
                    &output->io->info().formats->get(format),
                }))
            });

            scene_render(wm->scene.get(), target.get(), output->viewport);

            output->io->commit(target.get(), gpu_flush(wm->gpu), IoOutputCommitFlag::vsync);
        }
    }
}

void wm_init_io(WindowManager* wm)
{
    wm->io.pool = gpu_image_pool_create(wm->gpu);

    io_set_event_handler(wm->io.context, [wm](IoEvent* event) {
        handle_io_event(wm, event);
    });

    scene_add_damage_listener(wm->scene.get(), [wm] {
        for (auto* output : wm->io.outputs) {
            output->io->request_frame();
        }
    });
}

// -----------------------------------------------------------------------------

void wm_add_output_listener(WindowManager* wm, WmOutputListener listener)
{
    wm->io.output_listeners.emplace_back(std::move(listener));
}

void wm_post_output_event(WindowManager* wm, WmOutputEvent* event)
{
    for (auto& listener : wm->io.output_listeners) {
        listener(event);
    }
}

void wm_request_frame(WindowManager* wm)
{
    for (auto* output : wm->io.outputs) {
        output->io->request_frame();
    }
}

auto wm_output_get_viewport(WmOutput* output) -> rect2f32
{
    return output->viewport;
}

auto wm_list_outputs(WindowManager* wm) -> std::span<WmOutput* const>
{
    return wm->io.outputs;
}

auto wm_find_output_at(WindowManager* wm, vec2f32 point) -> WmFindOutputResult
{
    vec2f32   best_position = point;
    f32       best_distance = INFINITY;
    WmOutput* best_output   = nullptr;
    for (auto* output : wm->io.outputs) {
        auto clamped = rect_clamp_point(output->viewport, point);
        if (point == clamped) {
            best_position = point;
            best_output = output;
            break;
        } else if (f32 dist = glm::distance(clamped, point); dist < best_distance) {
            best_position = clamped;
            best_distance = dist;
            best_output = output;
        }
    }
    return { best_output, best_position };
}
