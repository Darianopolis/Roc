#include "internal.hpp"

io_output_base::~io_output_base()
{
    io_output_remove(this);
}

void io_output_add(io_output_base* output)
{
    core_assert(!std::ranges::contains(output->ctx->outputs, output));
    output->ctx->outputs.emplace_back(output);
    io_post_event(output->ctx, core::ptr_to(io_event {
        .type = io_event_type::output_added,
        .output = {
            .output = output
        },
    }));
}

void io_output_remove(io_output_base* output)
{
    if (std::erase(output->ctx->outputs, output)) {
        io_post_event(output->ctx, core::ptr_to(io_event {
            .type = io_event_type::output_removed,
            .output = {
                .output = output
            },
        }));
    }
}

// -----------------------------------------------------------------------------

void io_output_try_redraw(io_output_base* output)
{
    if (!output->frame_requested) return;
    if (!output->commit_available) return;
    if (!output->size.x || !output->size.y) return;

    output->frame_requested = false;

    io_post_event(output->ctx, core::ptr_to(io_event {
        .type = io_event_type::output_frame,
        .output = {
            .output = output,
        }
    }));
}

void io_output_try_redraw_later(io_output_base* output)
{
    core::event_loop::enqueue(output->ctx->event_loop, [output = core::Weak(output)] {
        if (output) {
            io_output_try_redraw(output.get());
        }
    });
}

void io_output_base::request_frame()
{
    frame_requested = true;
    io_output_try_redraw_later(this);
}

void io_output_post_configure(io_output_base* output)
{
    io_post_event(output->ctx, core::ptr_to(io_event {
        .type = io_event_type::output_configure,
        .output = {
            .output = output,
        }
    }));
}
