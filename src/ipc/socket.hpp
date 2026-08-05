#pragma once

#include <core/ring-buffer.hpp>
#include <core/object.hpp>
#include <core/exec.hpp>

// -----------------------------------------------------------------------------

/*
 * Max number of file descriptors that can be sent via `SCM_RIGHTS` in a `sendmsg` call
 *
 * Stable since Linux 2.6.38
 */
static constexpr usz ipc_scm_max_fd = 253;

struct SocketConnectionBufferInfo
{
    usz data_size = 65'536;
    usz fd_count  = 1024;
    usz max_fds_per_recv = ipc_scm_max_fd; // Defaulted to 253 to match absolute kernel limits.
    usz max_fds_per_send = 28;             // Defaulted to  28 to match libwayland's implementation limits.
};

struct SocketConnectionBuffers
{
    RingBuffer<std::byte> data;
    RingBuffer<fd_t>      fds;
};

struct SocketConnectionBarrier
{
    usz data_offset;
    usz fd_offset;
};

struct SocketConnection
{
    ExecContext* exec;

    Fd fd;

    SocketConnectionBuffers in;
    SocketConnectionBuffers out;

    std::deque<SocketConnectionBarrier> barriers;

    usz max_fds_per_recv;
    usz max_fds_per_send;

    std::vector<std::byte> cmsg;

    bool flush_queued;

    std::function<void(SocketConnection*)> readable;

    ~SocketConnection();
};

auto socket_connect(ExecContext*,               Fd fd,   const SocketConnectionBufferInfo& = {}) -> Ref<SocketConnection>;
auto socket_connect(ExecContext*, std::string_view name, const SocketConnectionBufferInfo& = {}) -> Ref<SocketConnection>;

auto socket_is_ok(      SocketConnection*) -> bool;
void socket_queue_flush(SocketConnection*);
auto socket_flush(      SocketConnection*) -> bool;

/*
 * Inserts a barrier to guarantee that all currently enqueued file descriptors
 * will be dequeued with or before the last byte enqueued before this point.
 */
void socket_insert_barrier(SocketConnection*);

// -----------------------------------------------------------------------------

struct SocketListenInfo
{
    int queue_size = 128;
};

struct SocketListener
{
    ExecContext* exec;

    Fd fd;

    std::function<void(Fd)> accept;

    ~SocketListener();
};

auto socket_listen(ExecContext*, std::string_view name, const SocketListenInfo& = {}) -> Ref<SocketListener>;
