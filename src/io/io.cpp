#include "internal.hpp"

#include <core/log.hpp>

auto io_create(WmServer* wm, ExecContext* exec, Gpu* gpu) -> Ref<IoContext>
{
    auto io = ref_create<IoContext>();

    io->wm = wm;
    io->exec = exec;
    io->gpu = gpu;

    io->image_pool = gpu_image_pool_create(gpu);

    io_udev_init(    io.get());
    io_session_init( io.get());
    io_libinput_init(io.get());
    io_evdev_init(   io.get());
    io_drm_init(     io.get());
    io_wayland_init( io.get());

    return io;
}

static
void shutdown(IoContext* io)
{
    io_wayland_deinit(io);
    io_drm_deinit(io);
    io_evdev_deinit(io);
    io_libinput_deinit(io);
    io_session_deinit(io);
    io_udev_deinit(io);

    io->signals.shutdown();
}

IoContext::~IoContext()
{
    fd_unlisten(exec, signal_fd.get());

    debug_assert(!wayland);
    debug_assert(!drm);
    debug_assert(!evdev);
    debug_assert(!libinput);
    debug_assert(!session);
}

auto io_get_signals(IoContext* io) -> IoSignals&
{
    return io->signals;
}

static
void reap_child_processes(IoContext* io)
{
    pid_t pid;
    int status;
    while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
        if (WIFEXITED(status)) {
            log_debug("Child {} exited with {}", pid, WEXITSTATUS(status));
        } else if (WIFSIGNALED(status)) {
            log_debug("Child {} killed by signal {}", pid, WTERMSIG(status));
        }
    }
}

static
void handle_signal(IoContext* io)
{
    signalfd_siginfo info = {};
    unix_check<read>(io->signal_fd.get(), &info, sizeof(info));

    switch (info.ssi_signo) {
        break;case SIGINT:
              case SIGTERM:
            io_stop(io);
        break;case SIGCHLD:
            reap_child_processes(io);
        break;default:
            debug_unreachable();
    }
}

void io_start(IoContext* io)
{
    if (io->wayland) {
        io_wayland_start(io);
    }

    if (io->drm) {
        io_drm_start(io);
    }

    sigset_t mask;
    sigemptyset(&mask);
    sigaddset(&mask, SIGINT);
    sigaddset(&mask, SIGTERM);
    sigaddset(&mask, SIGCHLD);
    sigprocmask(SIG_BLOCK, &mask, nullptr);
    io->signal_fd = Fd(unix_check<signalfd>(-1, &mask, SFD_NONBLOCK | SFD_CLOEXEC).value);
    fd_listen(io->exec, io->signal_fd.get(), FdEventBit::readable, [io](fd_t, Flags<FdEventBit>){
        handle_signal(io);
    });
}

void io_stop(IoContext* io)
{
    if (io->stop_requested) return;
    io->stop_requested = true;

    exec_enqueue(io->exec, [io = Weak(io)] {
        if (!io) return;
        shutdown(io.get());
    });
}
