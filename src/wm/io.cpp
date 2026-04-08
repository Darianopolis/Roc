#include "internal.hpp"

#include "io/io.hpp"

static
void reflow_outputs(WindowManager* wm)
{
    f32 x = 0;
    for (auto& output : wm->io.outputs) {
        auto size = output.output->info().size;
        output.viewport = {{x, 0.f}, size, xywh};
        x += f32(size.x);
    }
    for (auto& output : wm->io.outputs) {
        output.output->request_frame();
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
            ;
        break;case IoEventType::input_event:
            wm_seat_handle_io_event(wm, event);

        // output
        break;case IoEventType::output_added:
            wm->io.outputs.emplace_back(event->output.output);
            reflow_outputs(wm);
        break;case IoEventType::output_configure:
            reflow_outputs(wm);
        break;case IoEventType::output_removed:
            std::erase_if(wm->io.outputs, [&](auto& p) { return p.output == event->output.output; });
            reflow_outputs(wm);
        break;case IoEventType::output_frame: {
            auto output = std::ranges::find_if(wm->io.outputs, [&](auto& p) { return p.output == event->output.output; });

            // TODO: Only redraw with damage

            auto format = gpu_format_from_drm(DRM_FORMAT_ABGR8888);
            auto usage = GpuImageUsage::render;

            auto target = wm->io.pool->acquire({
                .extent = output->output->info().size,
                .format = format,
                .usage = usage,
                .modifiers = ptr_to(gpu_intersect_format_modifiers({
                    &gpu_get_format_properties(wm->gpu, format, usage)->mods,
                    &output->output->info().formats->get(format),
                }))
            });

            scene_render(wm->scene, target.get(), output->viewport);

            output->output->commit(target.get(), gpu_flush(wm->gpu), IoOutputCommitFlag::vsync);
        }
    }
}

static
void handle_damage(WindowManager* wm)
{
    for (auto& output : wm->io.outputs) {
        output.output->request_frame();
    }
}

void wm_init_io(WindowManager* wm)
{
    wm->io.pool = gpu_image_pool_create(wm->gpu);

    struct DamageListener : SceneDamageListener
    {
        WindowManager* wm;
        DamageListener(WindowManager* wm): wm(wm) {}
        virtual void damage(Scene*) { handle_damage(wm); }
    };

    scene_add_damage_listener(wm->scene, ref_create<DamageListener>(wm).get());

    io_set_event_handler(wm->io.context, [wm](IoEvent* event) {
        handle_io_event(wm, event);
    });
}
