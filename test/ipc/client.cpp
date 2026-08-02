#include <ipc/socket.hpp>
#include <ipc/ipc.hpp>

#include <core/log.hpp>
#include <core/enum.hpp>
#include <core/chrono.hpp>

int main()
{
    auto exec = exec_create();
    auto conn = socket_connect(exec.get(), "ipc-test-socket");

    usz messages = 0;

    auto push_message = [&](SocketConnection* conn, const char* str, u32 fd_count) {
        messages++;
        auto size_offset = ipc_reserve(conn, sizeof(u32));
        ipc_push(conn, str);
        ipc_push(conn, fd_count);
        u32 size = num_cast<u32>(conn->out.data.head - size_offset);
        std::memcpy(conn->out.data.data + size_offset, &size, sizeof(size));
        if (fd_count) {
            for (u32 i = 0; i <fd_count; ++i) {
                ipc_push_fd(conn, Fd(fd_dup_unsafe(STDERR_FILENO)));
            }
            socket_insert_barrier(conn);
        }
    };

    auto start = std::chrono::steady_clock::now();

    for (u32 i = 0; i < 16; ++i) {
        push_message(conn.get(), std::array{char('a' + i), '\0'}.data(), 3);
    }

    push_message(conn.get(), "Hello, Server!", 4);
    push_message(conn.get(), "This is a second message", 2);
    push_message(conn.get(), "This has no file descriptors", 0);
    push_message(conn.get(), nullptr, 3);
    push_message(conn.get(), "Goodbye", 1);

    socket_queue_flush(conn.get());

    auto destroy_and_stop = [&] {
        exec_enqueue(exec.get(), [&] {
            conn.destroy();
            exec_stop(exec.get());
        });
    };

    conn->readable = [&](SocketConnection*) {

        log_trace("--------");

        if (!socket_is_ok(conn.get())) {
            log_error("connection closed");
            destroy_and_stop();
            return;
        }

        while (auto str = ipc_pop<std::string_view>(conn.get())) {
            log_debug("Received: {}", *str);
            messages--;
        }

        if (messages == 0) {
            auto end = std::chrono::steady_clock::now();
            log_info("All message confirmations received in {}", FmtDuration{end - start});
            destroy_and_stop();
        }
    };

    exec_run(exec.get());
}
