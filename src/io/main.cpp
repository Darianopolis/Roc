#include "io.hpp"

#include <core/math.hpp>
#include <gpu/internal.hpp>

static
void render(Gpu* gpu, IoOutput* output, GpuImagePool* pool)
{
    auto format = gpu_format_from_drm(DRM_FORMAT_ABGR8888);
    auto usage = GpuImageUsage::transfer_dst;
    auto image = pool->acquire({
        .extent = output->info().size,
        .format = format,
        .usage = usage,
        .modifiers = ptr_to(gpu_intersect_format_modifiers({{
            &gpu_get_format_properties(gpu, format, usage)->mods,
            &output->info().formats->get(format),
        }})),
    });

    auto* cmd = gpu_get_commands(gpu);
    gpu->vk.CmdClearColorImage(cmd->buffer, image->handle(),
        VK_IMAGE_LAYOUT_GENERAL,
        ptr_to(VkClearColorValue{.float32={1, 1, 1, 1}}),
        1, ptr_to(VkImageSubresourceRange{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1}));

    output->commit(image.get(), gpu_flush(gpu), IoOutputCommitFlag::vsync);
}

static
void handle_event(IoContext* io, Gpu* gpu, GpuImagePool* pool, IoEvent* event)
{
    auto& input = event->input;

    switch (event->type) {
        break;case IoEventType::shutdown_requested:
            log_error("io::shutdown_requested({})", event->shutdown.reason);
            io_stop(io);
        break;case IoEventType::input_event:
            static constexpr auto channel_to_str = [](auto& e) {
                return std::format("{} = {}", libevdev_event_code_get_name(e.type, e.code), e.value);
            };
            log_info("io::input_event({:s}{})",
                input.channels | std::views::transform(channel_to_str) | std::views::join_with(", "sv),
                input.quiet ? ", QUIET" : "");
        break;case IoEventType::output_configure:
            log_info("io::output_configure{}", event->output.output->info().size);
            event->output.output->request_frame();
        break;case IoEventType::output_frame:
            render(gpu, event->output.output, pool);
        break;case IoEventType::input_added:
              case IoEventType::input_removed:
              case IoEventType::output_added:
              case IoEventType::output_removed:
            log_warn("io::{}", event->type);
    }
}

auto main() -> int
{
    auto exec = exec_create();
    auto gpu  = gpu_create( exec.get(), {});
    auto io   = io_create(  exec.get(), gpu.get());
    auto pool = gpu_image_pool_create(gpu.get());
    io_set_event_handler(io.get(), [&](IoEvent* event) {
        handle_event(io.get(), gpu.get(), pool.get(), event);
    });
    io_run(io.get());
}
