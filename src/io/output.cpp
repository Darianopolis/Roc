#include "internal.hpp"

IoOutputBase::~IoOutputBase()
{
    io_output_remove(this);
}

void io_output_add(IoOutputBase* output)
{
    debug_assert(!std::ranges::contains(output->io->outputs, output));
    output->io->outputs.emplace_back(output);
    io_post_event(output->io, ptr_to(IoEvent {
        .output = {
            .type = IoEventType::output_added,
            .output = output
        },
    }));
}

void io_output_remove(IoOutputBase* output)
{
    if (std::erase(output->io->outputs, output)) {
        io_post_event(output->io, ptr_to(IoEvent {
            .output = {
                .type = IoEventType::output_removed,
                .output = output
            },
        }));
    }
}

// -----------------------------------------------------------------------------

void io_output_try_redraw(IoOutputBase* output)
{
    if (!output->frame_requested) return;
    if (!output->commit_available) return;
    if (!output->size.x || !output->size.y) return;

    output->frame_requested = false;

    io_post_event(output->io, ptr_to(IoEvent {
        .output = {
            .type = IoEventType::output_frame,
            .output = output,
        }
    }));
}

void io_output_try_redraw_later(IoOutputBase* output)
{
    exec_enqueue(output->io->exec, [output = Weak(output)] {
        if (output) {
            io_output_try_redraw(output.get());
        }
    });
}

void IoOutputBase::request_frame()
{
    if (!frame_requested) {
        frame_requested = true;
        io_output_try_redraw_later(this);
    }
}

void io_output_post_configure(IoOutputBase* output)
{
    io_post_event(output->io, ptr_to(IoEvent {
        .output = {
            .type = IoEventType::output_configure,
            .output = output,
        }
    }));
}
