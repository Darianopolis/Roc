#include "session.hpp"

static constexpr libseat_seat_listener io_seat_listener
{
    .enable_seat = [](libseat*, void*) {
        log_warn("SEAT ENABLE");
    },
    .disable_seat = [](libseat*, void*) {
        log_warn("SEAT DISABLE");
    },
};

void io_session_init(IoContext* io)
{
    io->session = ref_create<IoSession>();
    io->session->seat = libseat_open_seat(&io_seat_listener, nullptr);
    if (!io->session->seat) {
        log_warn("Failed to open seat, falling back to nested mode");
        io->session.destroy();
        return;
    }

    log_info("Opened seat: {}", libseat_seat_name(io->session->seat));

    auto fd = libseat_get_fd(io->session->seat);
    exec_fd_listen(io->exec, fd, FdEventBit::readable, [io](int fd, Flags<FdEventBit>) {
        unix_check<libseat_dispatch>(io->session->seat, 0);
    });
}

void io_session_deinit(IoContext* io)
{
    if (io->session) {
        exec_fd_unlisten(io->exec, libseat_get_fd(io->session->seat));
        libseat_close_seat(io->session->seat);
    }

    io->session.destroy();
}

auto io_session_get_seat_name(IoSession* session) -> const char*
{
    return libseat_seat_name(session->seat);
}

auto io_session_open_device(IoSession* session, const char* path) -> int
{
    int fd = -1;
    auto devid = libseat_open_device(session->seat, path, &fd);
    session->devices.emplace_back(IoSeatDevice {
        .id = devid,
        .fd = fd,
    });
    return fd;
}

void io_session_close_device(IoSession* session, int fd)
{
    std::erase_if(session->devices, [&](auto& device) {
        if (device.fd == fd) {
            libseat_close_device(session->seat, device.id);
            return true;
        }
        return false;
    });
}
