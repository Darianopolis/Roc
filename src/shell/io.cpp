#include "shell.hpp"

struct ShellInputDevice
{
    IoInputDevice* io;
    Ref<WmInputDevice> wm;
};

struct ShellOutput
{
    IoOutput* io;
    Ref<WmOutput> wm;
};

struct ShellIo
{
    WmServer* wm;
    Gpu* gpu;
    IoContext* io;

    Ref<GpuImagePool> pool;

    std::vector<ShellInputDevice> input_devices;
    std::vector<ShellOutput> outputs;

    Listener<void(IoEvent*)> listener;
};

static
auto find_output(ShellIo* shell_io, IoOutput* io_output) -> WmOutput*
{
    for (auto& o : shell_io->outputs) {
        if (o.io == io_output) return o.wm.get();
    }
    return nullptr;
};

static
void handle_event(ShellIo* shell_io, IoEvent* event)
{
    switch (event->type) {
        // shutdown
        break;case IoEventType::shutdown_requested:
            io_stop(shell_io->io);

        // input
        break;case IoEventType::input_added:
            shell_io->input_devices.emplace_back(event->input.device, wm_input_device_create(shell_io->wm, event->input.device, WmInputDeviceInterface {
                .update_leds = [](void* data, Flags<libinput_led> leds) {
                    static_cast<IoInputDevice*>(data)->update_leds(leds);
                },
            }));
        break;case IoEventType::input_removed:
            std::erase_if(shell_io->input_devices, [&](const auto& i) { return i.io == event->input.device; });
        break;case IoEventType::input_event: {
            std::vector<WmInputDeviceChannel> events;
            for (auto& c : event->input.channels) {
                events.emplace_back(WmInputDeviceChannel{.type=c.type, .code=c.code, .value=c.value});
            }
            for (auto& i : shell_io->input_devices) {
                if (i.io != event->input.device) continue;
                wm_input_device_push_events(i.wm.get(), event->input.quiet, events);
                break;
            }
        }

        // output
        break;case IoEventType::output_added:
            shell_io->outputs.emplace_back(event->output.output, wm_output_create(shell_io->wm, event->output.output, WmOutputInterface {
                .request_frame = [](void* data) {
                    static_cast<IoOutput*>(data)->request_frame();
                },
            }));
        break;case IoEventType::output_configure:
            wm_output_set_pixel_size(find_output(shell_io, event->output.output), event->output.output->info().size);
        break;case IoEventType::output_removed:
            std::erase_if(shell_io->outputs, [&](const auto& o) { return o.io == event->output.output; });
        break;case IoEventType::output_frame: {
            auto io_output = event->output.output;
            auto output = find_output(shell_io, io_output);

            wm_output_frame(output);

            // TODO: Only redraw with damage

            auto format = gpu_format_from_drm(DRM_FORMAT_ABGR8888);
            auto usage = GpuImageUsage::render;

            auto target = shell_io->pool->acquire({
                .extent = io_output->info().size,
                .format = format,
                .usage = usage,
                .modifiers = ptr_to(gpu_intersect_format_modifiers({{
                    &gpu_get_format_properties(shell_io->gpu, format, usage)->mods,
                    &io_output->info().formats->get(format),
                }}))
            });

            scene_render(wm_get_scene(shell_io->wm), target.get(), wm_output_get_viewport(output));

            io_output->commit(target.get(), gpu_flush(shell_io->gpu), IoOutputCommitFlag::vsync);
        }
    }
}

void shell_init_io_bridge(Shell* shell)
{
    auto shell_io = ref_create<ShellIo>();
    shell_io->wm = shell->wm.get();
    shell_io->gpu = shell->gpu.get();
    shell_io->io = shell->io.get();
    shell_io->pool = gpu_image_pool_create(shell_io->gpu);
    shell_io->listener = io_get_signals(shell->io.get()).event
        .listen([shell_io = shell_io.get()](IoEvent* event) {
            handle_event(shell_io, event);
        });

    shell->apps.emplace_back(shell_io);
}
