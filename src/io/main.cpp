#include "io.hpp"

static
void render(gpu_context* gpu, io_output* output)
{
    auto image = output->acquire(gpu_image_usage::transfer_dst);

    auto queue = gpu_get_queue(gpu, gpu_queue_type::graphics);
    auto commands = gpu_commands_begin(queue);

    gpu->vk.CmdClearColorImage(commands->buffer, image->image, VK_IMAGE_LAYOUT_GENERAL,
        ptr_to(VkClearColorValue{.float32{1,0,0,1}}),
        1, ptr_to(VkImageSubresourceRange{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1}));

    auto done = gpu_commands_submit(commands.get(), {});
    output->present(image.get(), done);
}

static
void handle_event(io_context* io, gpu_context* gpu, io_event* event)
{
    auto& input = event->input;

    switch (event->type) {
        break;case io_event_type::shutdown_requested:
            log_error("io::shutdown_requested({})", core_enum_to_string(event->shutdown.reason));
            io_stop(io);
        break;case io_event_type::input_event:
            static constexpr auto channel_to_str = [](auto& e) {
                return std::format("{} = {}", libevdev_event_code_get_name(e.type, e.code), e.value);
            };
            log_info("io::input_event({:s}{})",
                input.channels | std::views::transform(channel_to_str) | std::views::join_with(", "sv),
                input.quiet ? ", QUIET" : "");
        break;case io_event_type::output_configure:
            log_info("io::output_configure{}", core_to_string(event->output.output->info().size));
            event->output.output->request_frame();
        break;case io_event_type::output_frame:
            render(gpu, event->output.output);
        break;case io_event_type::input_added:
              case io_event_type::input_removed:
              case io_event_type::output_added:
              case io_event_type::output_removed:
            log_warn("io::{}", core_enum_to_string(event->type));
    }
}

int main()
{
    auto event_loop = core_event_loop_create();
    auto gpu = gpu_create({}, event_loop.get());
    auto io = io_create(event_loop.get(), gpu.get());
    io_set_event_handler(io.get(), [io = io.get(), gpu = gpu.get()](io_event* event) {
        handle_event(io, gpu, event);
    });
    io_run(io.get());
}
