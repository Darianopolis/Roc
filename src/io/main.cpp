#include "io.hpp"

#include "core/math.hpp"

static
void render(gpu::Context* gpu, io_output* output, gpu::ImagePool* pool)
{
    auto format = gpu::format::from_drm(DRM_FORMAT_ABGR8888);
    auto usage = gpu::ImageUsage::transfer_dst;
    auto image = pool->acquire({
        .extent = output->info().size,
        .format = format,
        .usage = usage,
        .modifiers = core::ptr_to(gpu::intersect_format_modifiers({
            &gpu::get_format_props(gpu, format, usage)->mods,
            &output->info().formats->get(format),
        })),
    });

    auto queue = gpu::queue::get(gpu, gpu::QueueType::graphics);
    auto commands = gpu::commands::begin(queue);

    gpu->vk.CmdClearColorImage(commands->buffer, image->handle(), VK_IMAGE_LAYOUT_GENERAL,
        core::ptr_to(VkClearColorValue{.float32{1,0,0,1}}),
        1, core::ptr_to(VkImageSubresourceRange{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1}));

    auto done = gpu::submit(commands.get(), {});
    output->commit(image.get(), done, io_output_commit_flag::vsync);
}

static
void handle_event(io_context* io, gpu::Context* gpu, gpu::ImagePool* pool, io_event* event)
{
    auto& input = event->input;

    switch (event->type) {
        break;case io_event_type::shutdown_requested:
            log_error("io::shutdown_requested({})", core::to_string(event->shutdown.reason));
            io_stop(io);
        break;case io_event_type::input_event:
            static constexpr auto channel_to_str = [](auto& e) {
                return std::format("{} = {}", libevdev_event_code_get_name(e.type, e.code), e.value);
            };
            log_info("io::input_event({:s}{})",
                input.channels | std::views::transform(channel_to_str) | std::views::join_with(", "sv),
                input.quiet ? ", QUIET" : "");
        break;case io_event_type::output_configure:
            log_info("io::output_configure{}", core::to_string(event->output.output->info().size));
            event->output.output->request_frame();
        break;case io_event_type::output_frame:
            render(gpu, event->output.output, pool);
        break;case io_event_type::input_added:
              case io_event_type::input_removed:
              case io_event_type::output_added:
              case io_event_type::output_removed:
            log_warn("io::{}", core::to_string(event->type));
    }
}

int main()
{
    auto event_loop = core::event_loop::create();
    auto gpu = gpu::create({}, event_loop.get());
    auto io = io_create(event_loop.get(), gpu.get());
    auto pool = gpu::image_pool::create(gpu.get());
    io_set_event_handler(io.get(), [&](io_event* event) {
        handle_event(io.get(), gpu.get(), pool.get(), event);
    });
    io_run(io.get());
}
