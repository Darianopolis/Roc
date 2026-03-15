#include "protocol.hpp"

u32 wroc_get_elapsed_milliseconds()
{
    // TODO: This will elapse after 46 days of runtime, should we base it on surface epoch?

    auto now = std::chrono::steady_clock::now();
    auto elapsed = now - server->epoch;
    return std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count();
}

wl_global* wroc_global(const wl_interface* interface, i32 version, wl_global_bind_func_t bind, void* data)
{
    core_assert(version <= interface->version);
    return wl_global_create(server->display, interface, version, data, bind);
}

void wroc_queue_client_flush()
{
    if (server->client_flushes_pending) return;
    server->client_flushes_pending++;
    core::event_loop::enqueue(server->event_loop.get(), [] {
        if (server->client_flushes_pending) {
            wl_display_flush_clients(server->display);
            server->client_flushes_pending = 0;
        }
    });
}

static
void signal_handler(int sig)
{
    if (sig == SIGINT) {
        // Immediately unregister SIGINT in case of unresponsive event loop
        std::signal(sig, SIG_DFL);
    }

    // TODO: Dedicated stop eventfd for signal safe handling
    core::event_loop::enqueue(server->event_loop.get(), [sig] {
        const char* name = "Unknown";
        switch (sig) {
            break;case SIGTERM: name = "Terminate";
            break;case SIGINT:  name = "Interrupt";
        }

        log_error("{} ({}) signal received", name, sig);

        if (sig == SIGTERM || sig == SIGINT) {
            wroc_terminate();
        }
    });
}

void wroc_terminate()
{
    // TODO: Proper termination will require further event handling (e.g. waiting for GPU jobs to complete)
    //       Send event to start termination, then close event loop only after all subsystems have closed.
    core::event_loop::stop(server->event_loop.get());
}

wroc_server* server;

void wroc_run(int argc, char* argv[])
{
    core::log_set_history_enabled(true);

    core::Flags<gpu::Feature> gpu_features = {};

    core::Flags<wroc_render_option> render_options = {};
    wroc_backend_type backend_type = getenv("WAYLAND_DISPLAY")
        ? wroc_backend_type::layered
        : wroc_backend_type::direct;

    bool show_imgui_on_startup = false;
    bool show_csd = false;

    std::optional<std::string> x11_socket;

    std::optional<std::string> log_file;

    for (int i = 1; i < argc; ++i) {
        auto arg = std::string_view(argv[i]);
        if (arg == "--imgui") {
            show_imgui_on_startup = true;
        } else if (arg == "--xwayland") {
            if (i + 1 >= argc || argv[i + 1][0] != ':') {
                log_error("Expected x11 socket path after --xwayland");
                return;
            }
            x11_socket = argv[++i];
        } else if (arg == "--log-file") {
            if (i + 1 >= argc || argv[i + 1][0] == '\0') {
                log_error("Expected valid path after --log-file");
                return;
            }
            log_file = argv[++i];
        } else if (arg == "--csd") {
            show_csd = true;
        } else if (arg == "--validation") {
            gpu_features |= gpu::Feature::validation;
        } else {
            log_error("Unrecognized flag: {}", arg);
            return;
        }
    }

    core::init_log(core::LogLevel::trace, log_file ? log_file->c_str() : nullptr);

    auto server_ref = core::create<wroc_server>();
    server = server_ref.get();
    auto event_loop = core::event_loop::create();
    server->event_loop = event_loop;
    log_info("Server = {}", (void*)server);

    if (backend_type == wroc_backend_type::direct) {
        server->main_mod = wroc_modifier::super;
        server->main_mod_evdev = KEY_LEFTMETA;
    } else {
        server->main_mod = wroc_modifier::alt;
        server->main_mod_evdev = KEY_LEFTALT;
    }

    // Seat

    wroc_seat_init();

    server->epoch = std::chrono::steady_clock::now();

    // Init libwayland

    if ((getenv("WROC_WAYLAND_DEBUG_SERVER")?:"")[0]!=0) {
        wroc_setenv("WAYLAND_DEBUG", "1");
    } else {
        wroc_setenv("WAYLAND_DEBUG", nullptr);
    }
    server->display = wl_display_create();
    wroc_setenv("WAYLAND_DEBUG", nullptr);

    wl_display_set_default_max_buffer_size(server->display, 4096);

    server->socket = wl_display_add_socket_auto(server->display);

    auto wl_event_loop = wl_display_get_event_loop(server->display);
    auto wl_event_loop_fd = core::fd::reference(wl_event_loop_get_fd(wl_event_loop));
    core::fd::add_listener(wl_event_loop_fd.get(), event_loop.get(), core::FdEventBit::readable,
        [&](int fd, core::Flags<core::FdEventBit> events) {
            server->client_flushes_pending++;
            core::check<wl_event_loop_dispatch>(wl_event_loop, 0);
            wl_display_flush_clients(server->display);
            server->client_flushes_pending--;
        });

    // Output layout

    wroc_output_layout_init();

    // GPU

    log_info("Initializing gpu");
    auto gpu = gpu::create(gpu_features, event_loop.get());
    server->gpu = gpu.get();

    // Backend

    log_info("Initializing backend");
    server->backend = wroc_backend_create(backend_type);
    server->backend->init();
    log_info("Backend initialized");

    // Renderer

    log_info("Initializing renderer");
    server->renderer = wroc_renderer_create(render_options);

    // Cursor

    wroc_cursor_create();

    // ImGui

    log_info("Initializing imgui");
    wroc_imgui_init();
    wroc_debug_gui_init(show_imgui_on_startup);

    // Backend start

    server->backend->start();

    // Register globals

    WROC_GLOBAL(wl_shm);

    WROC_GLOBAL(zwp_linux_dmabuf_v1);
    WROC_GLOBAL(wp_linux_drm_syncobj_manager_v1);

    WROC_GLOBAL(wl_compositor);
    WROC_GLOBAL(wl_subcompositor);
    WROC_GLOBAL(wl_data_device_manager);
    WROC_GLOBAL(xdg_wm_base);
    WROC_GLOBAL(wl_seat, server->seat.get());

    WROC_GLOBAL(zwp_pointer_gestures_v1);

    WROC_GLOBAL(wp_viewporter);

    WROC_GLOBAL(zwp_relative_pointer_manager_v1);
    WROC_GLOBAL(zwp_pointer_constraints_v1);

    if (!show_csd) {
        WROC_GLOBAL(zxdg_decoration_manager_v1);
        WROC_GLOBAL(org_kde_kwin_server_decoration_manager);
    }

    WROC_GLOBAL(wp_cursor_shape_manager_v1);

    // TODO: Hardcoded extras - these should be enabled via a configuration or plugin system

    wroc_spawn("playerctld", {"playerctld"}, {});

    // Run

    log_info("WAYLAND_DISPLAY={}", server->socket);
    wroc_setenv("WAYLAND_DISPLAY", server->socket.c_str(), wroc_setenv_option::system_wide);
    if (backend_type == wroc_backend_type::direct) {
        wroc_setenv("XDG_CURRENT_DESKTOP", PROGRAM_NAME, wroc_setenv_option::system_wide);
    }
    if (x11_socket) {
        wroc_spawn("xwayland-satellite", {"xwayland-satellite", x11_socket->c_str()}, {});
        server->x11_socket = *x11_socket;
    }

    log_info("Running compositor on: {}", server->socket);

    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    core::event_loop::run(event_loop.get());

    signal(SIGINT, SIG_DFL);
    signal(SIGTERM, SIG_IGN);

    // Shutdown

    log_info("Compositor shutting down");

    log_info("Flushing gpu submissions");
    gpu::wait_idle(gpu.get());

    log_info("Destroying: backend");
    server->backend = nullptr;

    log_info("Destroying: clients");
    wl_display_destroy_clients(server->display);

    log_info("Destroying: renderer");
    server->renderer = nullptr;

    log_info("Destroying: wl_display");
    wl_event_loop_fd = nullptr;
    wl_display_destroy(server->display);

    log_info("Destroying: server");
    server_ref = nullptr;

    log_info("Destroying: gpu");
    gpu = nullptr;

    log_info("Destroying: event loop");
    event_loop = nullptr;

    log_info("Shutdown complete");
}
