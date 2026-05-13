#include "shell.hpp"

#include <core/math.hpp>
#include <core/signal.hpp>

#include <wm/wm.hpp>
#include <ui/ui.hpp>

auto main(int argc, char* argv[]) -> int
{
    log_init(PROGRAM_NAME ".log");
    fd_registry_init();
    registry_init();
    defer {
        registry_deinit();
        fd_registry_deinit();
        log_deinit();
    };

    log_info("{} ({:n:})", PROJECT_NAME, std::span<const char* const>(argv, argc));

    Shell shell = {};

    // Config

    shell.app_share = std::filesystem::path(getenv("HOME")) / ".local/share" / PROGRAM_NAME;
    shell.wallpaper = getenv("WALLPAPER") ?: "";
    if (getenv("WAYLAND_DISPLAY")) {
        log_debug("Running nested!");
        shell.main_mod = SeatModifier::alt;
    } else {
        log_debug("Running in direct session");
        shell.main_mod = SeatModifier::super;
    }

    // Systems

    auto exec = exec_create();
    auto gpu = gpu_create(exec.get(), {});
    auto io = io_create(exec.get(), gpu.get());
    auto wm = wm_create({
        .exec = exec.get(),
        .gpu = gpu.get(),
        .main_mod = shell.main_mod,
    });
    auto way = way_create(exec.get(), gpu.get(), wm.get());

    shell.exec = exec.get();
    shell.gpu = gpu.get();
    shell.way = way.get();
    shell.io = io.get();
    shell.wm = wm.get();

    // Applets

    auto _ = shell_init_background(&shell);
    auto _ = shell_init_launcher(&shell);
    auto _ = shell_init_log_viewer(&shell);
    auto _ = shell_init_menu(&shell);

    shell_init_xwayland(&shell, argc, argv);

    // IO

    struct InputDevice
    {
        IoInputDevice* io;
        Ref<WmInputDevice> wm;
    };
    struct Output
    {
        IoOutput* io;
        Ref<WmOutput> wm;
    };
    std::vector<InputDevice> input_devices;
    auto pool = gpu_image_pool_create(gpu.get());
    std::vector<Output> outputs;
    auto find_output = [&](IoOutput* output) -> WmOutput* {
        for (auto& o : outputs) {
            if (o.io == output) return o.wm.get();
        }
        return nullptr;
    };
    auto io_listener = io_get_signals(io.get()).event.listen([&](IoEvent* event) {
        switch (event->type) {
            // shutdown
            break;case IoEventType::shutdown_requested:
                io_stop(io.get());

            // input
            break;case IoEventType::input_added:
                input_devices.emplace_back(event->input.device, wm_input_device_create(wm.get(), event->input.device, WmInputDeviceInterface {
                    .update_leds = [](void* data, Flags<libinput_led> leds) {
                        static_cast<IoInputDevice*>(data)->update_leds(leds);
                    },
                }));
            break;case IoEventType::input_removed:
                std::erase_if(input_devices, [&](const auto& i) { return i.io == event->input.device; });
            break;case IoEventType::input_event: {
                std::vector<WmInputDeviceChannel> events;
                for (auto& c : event->input.channels) {
                    events.emplace_back(WmInputDeviceChannel{.type=c.type, .code=c.code, .value=c.value});
                }
                for (auto& i : input_devices) {
                    if (i.io != event->input.device) continue;
                    wm_input_device_push_events(i.wm.get(), event->input.quiet, events);
                    break;
                }
            }

            // output
            break;case IoEventType::output_added:
                outputs.emplace_back(event->output.output, wm_output_create(wm.get(), event->output.output, WmOutputInterface {
                    .request_frame = [](void* data) {
                        static_cast<IoOutput*>(data)->request_frame();
                    },
                }));
            break;case IoEventType::output_configure:
                wm_output_set_pixel_size(find_output(event->output.output), event->output.output->info().size);
            break;case IoEventType::output_removed:
                std::erase_if(outputs, [&](const auto& o) { return o.io == event->output.output; });
            break;case IoEventType::output_frame: {
                auto io_output = event->output.output;
                auto output = find_output(io_output);

                wm_output_frame(output);

                // TODO: Only redraw with damage

                auto format = gpu_format_from_drm(DRM_FORMAT_ABGR8888);
                auto usage = GpuImageUsage::render;

                auto target = pool->acquire({
                    .extent = io_output->info().size,
                    .format = format,
                    .usage = usage,
                    .modifiers = ptr_to(gpu_intersect_format_modifiers({{
                        &gpu_get_format_properties(gpu.get(), format, usage)->mods,
                        &io_output->info().formats->get(format),
                    }}))
                });

                scene_render(wm_get_scene(wm.get()), target.get(), wm_output_get_viewport(output));

                io_output->commit(target.get(), gpu_flush(gpu.get()), IoOutputCommitFlag::vsync);
            }
        }
    });

    // Run

    io_start(io.get());
    exec_run(exec.get());
}
