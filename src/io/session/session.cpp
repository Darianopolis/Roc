#include "session.hpp"

#include <core/log.hpp>

static
void set_enabled(IoContext* io, bool state)
{
    io->session->enabled = state;
    io->session->signals.state(state);
}

static constexpr libseat_seat_listener io_seat_listener
{
    .enable_seat = [](libseat* seat, void* data) {
        log_warn("Seat enabled");

        auto* io = static_cast<IoContext*>(data);

        set_enabled(io, true);
    },
    .disable_seat = [](libseat* seat, void* data) {
        log_warn("Seat disabled");

        auto* io = static_cast<IoContext*>(data);

        io->session->timeout_arm.reset();

        set_enabled(io, false);
        libseat_disable_seat(seat);
    },
};

void io_session_init(IoContext* io)
{
    io->session = ref_create<IoSession>();
    io->session->seat = libseat_open_seat(&io_seat_listener, io);
    if (!io->session->seat) {
        log_warn("Failed to open seat, falling back to nested mode");
        io->session.destroy();
        return;
    }
    io->session->enabled = true;

    log_info("Opened seat: {}", libseat_seat_name(io->session->seat));

    auto fd = libseat_get_fd(io->session->seat);
    fd_listen(io->exec, fd, FdEventBit::readable, [io](fd_t fd, Flags<FdEventBit>) {
        unix_check<libseat_dispatch>(io->session->seat, 0);
    });
}

void io_session_deinit(IoContext* io)
{
    if (!io->session) return;

    fd_unlisten(io->exec, libseat_get_fd(io->session->seat));
    libseat_close_seat(io->session->seat);

    io->session.destroy();
}

auto io_session_get_seat_name(IoSession* session) -> const char*
{
    return libseat_seat_name(session->seat);
}

auto io_session_open_device(IoSession* session, const char* path) -> fd_t
{
    fd_t fd = -1;
    auto devid = libseat_open_device(session->seat, path, &fd);
    session->devices.emplace_back(IoSeatDevice {
        .id = devid,
        .fd = fd,
    });
    return fd;
}

void io_session_close_device(IoSession* session, fd_t fd)
{
    std::erase_if(session->devices, [&](auto& device) {
        if (device.fd == fd) {
            libseat_close_device(session->seat, device.id);
            close(fd);
            return true;
        }
        return false;
    });
}

void io_switch_session(IoContext* io, i32 session)
{
    if (!io->session) {
        log_warn("Cannot switch session, libseat not initialized");
        return;
    }

    if (auto res = unix_check<libseat_switch_session>(io->session->seat, session); res.err()) {
        log_warn("Failed to switch session: {}", strerror(res.error));
        return;
    };

    // NOTE: There is an inherent race condition here:
    //
    //       If the user switches session, and then switches back to this session before a `disable_seat` event is received
    //       then we will not regain libinput device control, and the system will be unresponsive.
    //
    //       To avoid this, we set up a timeout - if we don't see a `disable_seat` event within one second
    //       then we assume that the session was switched back too quickly and we forcibly cycle session state

    io->session->timeout_arm = ref_create<int>();
    timer_enqueue(io->timer.get(), std::chrono::steady_clock::now() + 1s, [io, armed = Weak(io->session->timeout_arm)] {
        if (!armed || !io->session->enabled) return;

        log_error("Session switch timed out - Cycling session state");

        set_enabled(io, false);
        set_enabled(io, true);
    });
}
