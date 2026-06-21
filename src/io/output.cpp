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
        .commit = [](void* data, const WmOutputCommitInfo& info) -> bool {
            return static_cast<IoOutputBase*>(data)->commit(info);
        }
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

    wm_output_frame(output->output.get(), output->info().formats);
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
