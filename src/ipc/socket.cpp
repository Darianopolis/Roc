#include "socket.hpp"

#include <core/util.hpp>
#include <core/log.hpp>

static
auto socket_read(SocketConnection* conn) -> bool;

static
void close_connection(SocketConnection* conn)
{
    if (!conn->fd) return;
    fd_unlisten(conn->exec, conn->fd.get());
    conn->fd.reset();
}

SocketConnection::~SocketConnection()
{
    close_connection(this);
}

auto socket_connect(ExecContext* exec, Fd fd, const SocketConnectionBufferInfo& buffer_info) -> Ref<SocketConnection>
{
    auto conn = ref_create<SocketConnection>();
    conn->exec = exec;
    conn->fd = std::move(fd);
    conn->in.data  = RingBuffer<std::byte>(buffer_info.data_size);
    conn->out.data = RingBuffer<std::byte>(buffer_info.data_size);
    conn->in.fds   = RingBuffer<fd_t>(buffer_info.fd_count);
    conn->out.fds  = RingBuffer<fd_t>(buffer_info.fd_count);

    auto max_fds_per_msg = std::max(buffer_info.max_fds_per_recv, buffer_info.max_fds_per_send);
    debug_assert(buffer_info.fd_count >= max_fds_per_msg);

    conn->max_fds_per_recv = buffer_info.max_fds_per_recv;
    conn->max_fds_per_send = buffer_info.max_fds_per_send;

    conn->cmsg.resize(CMSG_LEN(max_fds_per_msg * sizeof(fd_t)));

    fd_listen(exec, conn->fd.get(), FdEventBit::readable, [conn = conn.get()](fd_t, Flags<FdEventBit>) {
        if (!conn->readable) return;
        if (!socket_read(conn)) {
            close_connection(conn);
        }
        conn->readable(conn);
    });

    return conn;
}

// -----------------------------------------------------------------------------

auto make_abstract_address(sockaddr_un& addr, std::string_view name) -> socklen_t
{
    debug_assert(name.size() < sizeof(sockaddr_un::sun_path));

    std::memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    std::memcpy(addr.sun_path + 1, name.data(), name.size());
    return num_cast<socklen_t>(offsetof(sockaddr_un, sun_path) + 1 + name.size());
}

auto socket_connect(ExecContext* exec, std::string_view name, const SocketConnectionBufferInfo& buffer_info) -> Ref<SocketConnection>
{
    Fd fd = Fd(unix_check<socket>(AF_UNIX, SOCK_STREAM, 0).value);
    if (!fd) return nullptr;

    sockaddr_un addr;
    auto len = make_abstract_address(addr, name);

    if (!unix_check<connect>(fd.get(), reinterpret_cast<sockaddr*>(&addr), len)) return nullptr;

    return socket_connect(exec, std::move(fd), buffer_info);
}

SocketListener::~SocketListener()
{
    fd_unlisten(exec, fd.get());
}

auto socket_listen(ExecContext* exec, std::string_view name, const SocketListenInfo& listen_info) -> Ref<SocketListener>
{
    Fd fd = Fd(unix_check<socket>(AF_UNIX, SOCK_STREAM, 0).value);
    if (!fd) return nullptr;

    sockaddr_un addr;
    auto len = make_abstract_address(addr, name);

    if (!unix_check<bind>(fd.get(), reinterpret_cast<sockaddr*>(&addr), len)) return nullptr;
    if (!unix_check<listen>(fd.get(), listen_info.queue_size)) return nullptr;

    auto listener = ref_create<SocketListener>();
    listener->exec = exec;
    listener->fd = std::move(fd);

    fd_listen(exec, listener->fd.get(), FdEventBit::readable, [listener = listener.get()](fd_t fd, Flags<FdEventBit>) {
        auto res = unix_check<accept>(fd, nullptr, nullptr);
        if (!res) return;
        if (!listener->accept) unix_check<close>(res.value);
        listener->accept(Fd(res.value));
    });

    return listener;
}

// -----------------------------------------------------------------------------

static
void read_cmsg(SocketConnection* conn, msghdr* msg)
{
    for (cmsghdr* cmsg = CMSG_FIRSTHDR(msg); cmsg; cmsg = CMSG_NXTHDR(msg, cmsg)) {
        if (       cmsg->cmsg_level != SOL_SOCKET
                || cmsg->cmsg_type  != SCM_RIGHTS) {
            continue;
        }

        auto size = cmsg->cmsg_len - CMSG_LEN(0);
        auto data = CMSG_DATA(cmsg);

        auto count = size / sizeof(fd_t);

        debug_assert(size % 4 == 0);
        debug_assert(conn->in.fds.get_free() >= count);
        std::memcpy(conn->in.fds.get_head(), data, size);
        conn->in.fds.head += count;
    }
}

static
auto socket_read(SocketConnection* conn) -> bool
{
    if (!conn->in.data.get_free()) return true;

    iovec iov = {
        .iov_base = conn->in.data.get_head(),
        .iov_len  = conn->in.data.get_free_bytes(),
    };
    msghdr msg = {
        .msg_iov = &iov,
        .msg_iovlen = 1,
        .msg_control = conn->cmsg.data(),
        .msg_controllen = CMSG_LEN(std::min(conn->max_fds_per_recv, conn->in.fds.get_free()) * sizeof(fd_t)),
    };

    isz len;
    do {
        len = unix_check<recvmsg, EINTR>(conn->fd.get(), &msg, MSG_DONTWAIT | MSG_CMSG_CLOEXEC).value;
    } while (len == -1 && errno == EINTR);

    if (msg.msg_flags & MSG_CTRUNC) {
        log_error("Ancillary data discarded!");
        return false;
    }

    if (len <  0) return errno == EAGAIN;
    if (len == 0) return false;

    conn->in.data.head += num_cast<usz>(len);

    read_cmsg(conn, &msg);

    return true;
}

static
auto build_cmsg(SocketConnection* conn, usz count) -> usz
{
    auto& buf = conn->out.fds;

    if (count > 0) {
        auto cmsg = reinterpret_cast<cmsghdr*>(conn->cmsg.data());
        cmsg->cmsg_level = SOL_SOCKET;
        cmsg->cmsg_type = SCM_RIGHTS;
        cmsg->cmsg_len = CMSG_LEN(count * sizeof(fd_t));
        std::memcpy(CMSG_DATA(cmsg), buf.get_tail(), count * sizeof(fd_t));
        return cmsg->cmsg_len;
    }

    return 0;
}

static
void close_fds(RingBuffer<fd_t>& buf, usz count)
{
    for (usz i = 0; i < count; ++i) {
        unix_check<close>(buf.get_tail()[i]);
    }
    buf.tail += count;
}

auto socket_flush(SocketConnection* conn) -> bool
{
    while (conn->out.data.get_used()) {

        usz data_end = conn->out.data.tail;
        usz   fd_end = conn->out.fds.tail;

        // Walk through pending file descriptor barriers

        auto push_barrier = [&](SocketConnectionBarrier barrier) -> bool {
            fd_end = std::min(barrier.fd_offset, conn->out.fds.tail + conn->max_fds_per_send);
            data_end = barrier.data_offset;
            if (barrier.fd_offset > fd_end) {
                usz remaining_fds = barrier.fd_offset - fd_end;
                usz minimum_extra_sends = (remaining_fds + conn->max_fds_per_send - 1) / conn->max_fds_per_send;
                debug_assert(data_end > conn->out.data.tail + minimum_extra_sends,
                    "Not enough data bytes before barrier to satisfy dependency");
                data_end -= minimum_extra_sends;
                return false;
            }
            return true;
        };

        for (auto barrier = conn->barriers.begin(); barrier != conn->barriers.end();) {
            if (!push_barrier(*barrier)) break;
            barrier = conn->barriers.erase(barrier);
        }

        if (conn->barriers.empty()) {
            push_barrier({conn->out.data.head, conn->out.fds.head});
        }

        // Send

        iovec iov = {
            .iov_base = conn->out.data.get_tail(),
            .iov_len  = data_end - conn->out.data.tail,
        };
        msghdr msg = {
            .msg_iov = &iov,
            .msg_iovlen = 1,
            .msg_control = conn->cmsg.data(),
            .msg_controllen = build_cmsg(conn, fd_end - conn->out.fds.tail),
        };

        isz len;
        do {
            len = unix_check<sendmsg, EINTR>(conn->fd.get(), &msg, MSG_NOSIGNAL | MSG_DONTWAIT).value;
        } while (len == -1 && errno == EINTR);

        if (len == -1) return false;

        // Advance

        close_fds(conn->out.fds, fd_end - conn->out.fds.tail);
        conn->out.data.tail += num_cast<usz>(len);
    }

    conn->barriers.clear();

    return true;
}

// -----------------------------------------------------------------------------

auto socket_is_ok(SocketConnection* conn) -> bool
{
    return bool(conn->fd);
}

void socket_insert_barrier(SocketConnection* conn)
{
    conn->barriers.emplace_back(SocketConnectionBarrier {
        .data_offset = conn->out.data.head,
        .fd_offset   = conn->out.fds.head,
    });
}

void socket_queue_flush(SocketConnection* conn)
{
    if (conn->flush_queued) return;

    conn->flush_queued = true;
    exec_enqueue(conn->exec, [conn = Weak(conn)] {
        if (!conn) return;
        conn->flush_queued = false;
        if (!socket_flush(conn.get())) {
            close_connection(conn.get());
        }
    });
}
