#include "internal.hpp"

IoOutputBase::~IoOutputBase()
{
    io_output_remove(this);
}

static
void request_frame(IoOutputBase* output)
{
    if (!output->frame_requested) {
        output->frame_requested = true;
        io_output_try_redraw_later(output);
    }
}

void io_output_add(IoOutputBase* output)
{
    debug_assert(!std::ranges::contains(output->io->outputs, output));
    output->io->outputs.emplace_back(output);
    output->output = wm_output_create(output->io->wm, output, WmOutputInterface {
        .request_frame = [](void* data) {
            request_frame(static_cast<IoOutputBase*>(data));
        },
    });
}

void io_output_remove(IoOutputBase* output)
{
    std::erase(output->io->outputs, output);
}

// -----------------------------------------------------------------------------

void io_output_try_redraw(IoOutputBase* output)
{
    if (!output->frame_requested) return;
    if (!output->commit_available) return;
    if (!output->size.x || !output->size.y) return;

    output->frame_requested = false;

    auto needs_redraw = wm_output_frame(output->output.get());

    auto* io = output->io;
    auto* wm = io->wm;

    if (needs_redraw) {
        auto format = gpu_format_from_drm(DRM_FORMAT_ABGR8888);
        auto usage = GpuImageUsage::render;

        auto target = io->image_pool->acquire({
            .extent = output->info().size,
            .format = format,
            .usage = usage,
            .modifiers = ptr_to(gpu_intersect_format_modifiers({{
                &gpu_get_format_properties(io->gpu, format, usage)->mods,
                &output->info().formats->get(format),
            }}))
        });

        scene_render(wm_get_scene_renderer(wm), wm_get_scene(wm), target.get(), wm_output_get_viewport(output->output.get()));

        output->commit(target.get(), gpu_flush(io->gpu), IoOutputCommitFlag::vsync);
    }
}

void io_output_try_redraw_later(IoOutputBase* output)
{
    exec_enqueue(output->io->exec, [output = Weak(output)] {
        if (!output) return;
        io_output_try_redraw(output.get());
    });
}

void io_output_post_configure(IoOutputBase* output)
{
    wm_output_set_pixel_size(output->output.get(), output->info().size);
}
