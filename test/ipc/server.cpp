#include <ipc/socket.hpp>
#include <ipc/ipc.hpp>

#include <core/util.hpp>
#include <core/log.hpp>
#include <core/enum.hpp>

static
auto process_message(SocketConnection* conn) -> bool
{
    // Check size

    auto size = ipc_peek<u32>(conn);
    if (!size || size > conn->in.data.get_used()) return false;
    ipc_pop<u32>(conn);

    // Read

    auto str = ipc_pop<std::string_view>(conn).value();
    auto fd_count = ipc_pop<u32>(conn).value();

    std::vector<fd_t> fds;
    for (u32 i = 0; i < fd_count; ++i) {
        auto fd = ipc_pop_fd(conn);
        debug_assert(fd);
        fds.emplace_back(fd.get());
    }

    // Respond

    auto response = std::format("Server received message (str: {}, fds: {})",
        str.data() ? std::format("\"{}\"", str) : "nullptr", fds);

    log_debug("{}", response);

    ipc_push<std::string_view>(conn, response);

    // Flush

    socket_queue_flush(conn);

    return true;
}

int main(int argc, char* argv[])
{
    DebugSignalHandlers _;
    Logger _;
    Allocator _;
    FdRegistry _;
    fd_mark_open_as_inherited();

    auto exec = exec_create();
    auto listener = socket_listen(exec.get(), "ipc-test-socket");
    ankerl::unordered_dense::set<Ref<SocketConnection>> connections;
    listener->accept = [&](Fd fd) {
        auto conn = socket_connect(exec.get(), std::move(fd));
        connections.insert(conn);
        conn->readable = [&](SocketConnection* conn) {

            log_trace("--------");

            if (!socket_is_ok(conn)) {
                log_error("connection closed");
                connections.erase(conn);
                return;
            }

            while (process_message(conn))
                ;
        };
    };

    exec_run(exec.get());

    return EXIT_SUCCESS;
}
