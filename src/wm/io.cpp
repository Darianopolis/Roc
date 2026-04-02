#include "internal.hpp"

#include "io/io.hpp"

static
void reflow_outputs(WindowManager* wm)
{
    f32 x = 0;
    for (auto& output : wm->io.outputs) {
        auto size = output.io->info().size;
        scene_output_set_viewport(output.scene.get(), {{x, 0.f}, size, xywh});
        x += f32(size.x);
    }
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
            scene_push_io_event(wm->scene, event);

        // output
        break;case IoEventType::output_added:
            wm->io.outputs.emplace_back(scene_output_create(wm->io.client.get(), SceneOutputFlag::workspace), event->output.output);
            reflow_outputs(wm);
        break;case IoEventType::output_configure:
            reflow_outputs(wm);
        break;case IoEventType::output_removed:
            std::erase_if(wm->io.outputs, [&](auto& p) { return p.io == event->output.output; });
            reflow_outputs(wm);
        break;case IoEventType::output_frame: {
            auto output = std::ranges::find_if(wm->io.outputs, [&](auto& p) { return p.io == event->output.output; });
            scene_frame(wm->scene, output->scene.get());

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

            scene_render(wm->scene, target.get(), scene_output_get_viewport(output->scene.get()));

            output->io->commit(target.get(), gpu_flush(wm->gpu), IoOutputCommitFlag::vsync);
        }
    }
}

static
void handle_scene_event(WindowManager* wm, SceneEvent* event)
{
    switch (event->type) {
        break;case SceneEventType::output_frame_request: {
            auto output = std::ranges::find_if(wm->io.outputs, [&](auto& p) { return p.scene.get() == event->output; });
            output->io->request_frame();
        }
        break;default:
            ;
    }
}

void wm_init_io(WindowManager* wm)
{
    wm->io.pool = gpu_image_pool_create(wm->gpu);
    wm->io.client = scene_client_create(wm->scene);

    io_set_event_handler(wm->io.context, [wm](IoEvent* event) {
        handle_io_event(wm, event);
    });

    scene_client_set_event_handler(wm->io.client.get(), [wm](SceneEvent* event) {
        handle_scene_event(wm, event);
    });
}
