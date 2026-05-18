#include "exec.hpp"

#include "chrono.hpp"
#include "util.hpp"
#include "log.hpp"

// -----------------------------------------------------------------------------

static
auto to_epoll_events(Flags<FdEventBit> events) -> u32
{
    u32 out = 0;
    if (events.contains(FdEventBit::readable)) out |= EPOLLIN;
    if (events.contains(FdEventBit::writable)) out |= EPOLLOUT;
    return out;
}

static
auto from_epoll_events(u32 events) -> Flags<FdEventBit>
{
    Flags<FdEventBit> out = {};
    if (events & EPOLLIN)  out |= FdEventBit::readable;
    if (events & EPOLLOUT) out |= FdEventBit::writable;
    return out;
}

static
void handle_task_eventfd(ExecContext* exec)
{
    eventfd_t tasks = {};
    unix_check<eventfd_read>(exec->task_fd.get(), &tasks);
    exec->tasks_available += tasks;
}

auto exec_create() -> Ref<ExecContext>
{
    auto exec = ref_create<ExecContext>();

    exec->os_thread = std::this_thread::get_id();

    exec->epoll_fd = Fd(unix_check<epoll_create1>(EPOLL_CLOEXEC).value);

    exec->task_fd = Fd(unix_check<eventfd>(0, EFD_CLOEXEC | EFD_NONBLOCK).value);
    fd_listen(exec.get(), exec->task_fd.get(), FdEventBit::readable,
        [exec = exec.get()](fd_t, Flags<FdEventBit>) {
            handle_task_eventfd(exec);
        });

    return exec;
}

thread_local ExecContext* exec_thread_context;

void exec_set_thread_context(ExecContext* exec)
{
    exec_thread_context = exec;
}

auto exec_get_thread_context() -> ExecContext*
{
    return exec_thread_context;
}

static
void check_all_listeners_unregistered(ExecContext* exec)
{
    for (auto[i, listener] : exec->listeners | std::views::enumerate) {
        debug_assert(!listener, "Listener for ({}) still registered", i);
    }
}

ExecContext::~ExecContext()
{
    debug_assert(stopped);
    debug_assert(queue.empty());
    check_all_listeners_unregistered(this);
}

void exec_stop(ExecContext* exec)
{
    exec->stopped = true;

    fd_unlisten(exec, exec->task_fd.get());

    check_all_listeners_unregistered(exec);
}

void exec_run(ExecContext* exec)
{
    debug_assert(!exec_get_thread_context());
    exec_set_thread_context(exec);

    exec->os_thread = std::this_thread::get_id();

    static constexpr usz max_epoll_events = 64;
    std::array<epoll_event, max_epoll_events> events;

    while (!exec->stopped || exec->tasks_available) {

        // Check for new fd events

        i32 timeout = 0;
        if (!exec->tasks_available) {
            timeout = -1;
        }
        auto[count, error] = unix_check<epoll_wait, EAGAIN, EINTR>(exec->epoll_fd.get(), events.data(), events.size(), timeout);
        if (error) {
            if (error == EAGAIN || error == EINTR) {
                if (!exec->tasks_available) continue;
            } else {
                // At this point, we can't assume that we'll receive any future FD events.
                // Since this includes all user input, the only safe thing to do is
                // immediately terminate to avoid locking out the user's system.
                debug_kill();
            }
        }

        // Flush fd events

        if (count > 0) {
            for (i32 i = 0; i < count; ++i) {
                auto fd = events[i].data.fd;
                auto l = exec->listeners[fd];
                if (!l) continue;

                auto event_bits = from_epoll_events(events[i].events);
                if (l->flags.contains(FdListenFlag::oneshot)) {
                    Ref listener = l;
                    fd_unlisten(exec, fd);
                    listener->handle(fd, event_bits);
                } else {
                    l->handle(fd, event_bits);
                }
            }
        }

        // Flush tasks

        if (exec->tasks_available) {
            u64 available = std::exchange(exec->tasks_available, 0);

            for (u64 i = 0; i < available; ++i) {
                ExecTask task;
                {
                    std::scoped_lock _{exec->queue_mutex};
                    task = std::move(exec->queue.front());
                    exec->queue.pop_front();
                }

                task.callback();

                if (task.sync) {
                    task.sync->test_and_set();
                    task.sync->notify_one();
                }
            }
        }

    }
}

// -----------------------------------------------------------------------------

void fd_listen(
    ExecContext* exec,
    fd_t fd,
    FdListener* listener)
{
    auto events = listener->events;

    debug_assert(fd_is_valid(fd));

    debug_assert(events);
    debug_assert(!exec->listeners[fd]);

    exec->listeners[fd] = listener;

    unix_check<epoll_ctl>(exec->epoll_fd.get(), EPOLL_CTL_ADD, fd, ptr_to(epoll_event {
        .events = to_epoll_events(events),
        .data {
            .fd = fd,
        }
    }));
}

void fd_unlisten(ExecContext* exec, fd_t fd)
{
    debug_assert(fd_is_valid(fd));

    if (!exec->listeners[fd]) {
        log_warn("fd does not have registered listener");
    }

    auto res = unix_check<epoll_ctl>(exec->epoll_fd.get(), EPOLL_CTL_DEL, fd, nullptr);
    debug_assert(res.ok());

    exec->listeners[fd] = nullptr;
}
