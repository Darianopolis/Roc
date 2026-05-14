#pragma once

#include <core/debug.hpp>
#include <core/object.hpp>
#include <core/enum.hpp>
#include <core/fd.hpp>

// -----------------------------------------------------------------------------

struct ExecTask
{
    std::move_only_function<void()> callback;
    std::atomic_flag* sync;
};

struct FdListener;

struct ExecContext
{
    bool stopped = false;

    std::array<Ref<FdListener>, fd_limit> listeners  = {};

    std::thread::id os_thread;

    std::mutex queue_mutex;
    std::deque<ExecTask> queue;

    u64 tasks_available;
    Fd task_fd;

    Fd timer_fd;
    struct timed_event
    {
        std::chrono::steady_clock::time_point expiration;
        std::move_only_function<void()> callback;
    };
    std::deque<timed_event> timed_events;
    std::optional<std::chrono::steady_clock::time_point> current_wakeup;

    Fd epoll_fd;

    struct {
        u64 events_handled;
        u64 poll_waits;
    } stats;

    ~ExecContext();
};

auto exec_create() -> Ref<ExecContext>;

void exec_set_thread_context(ExecContext*);
auto exec_get_thread_context() -> ExecContext*;

void exec_run( ExecContext*);
void exec_stop(ExecContext*);

void exec_add_timer_wakeup(ExecContext*, std::chrono::steady_clock::time_point exp);

template<typename Lambda>
void exec_enqueue_timed(ExecContext* exec, std::chrono::steady_clock::time_point exp, Lambda&& task)
{
    debug_assert(std::this_thread::get_id() == exec->os_thread);

    exec->timed_events.emplace_back(exp, std::move(task));

    exec_add_timer_wakeup(exec, exp);
}

template<typename Lambda>
void exec_enqueue(ExecContext* exec, Lambda&& task)
{
    std::scoped_lock _{exec->queue_mutex};
    exec->queue.emplace_back(std::move(task));
    if (std::this_thread::get_id() == exec->os_thread) {
        exec->tasks_available++;
    } else {
        unix_check<eventfd_write>(exec->task_fd.get(), 1);
    }
}

template<typename Lambda>
void exec_enqueue_and_wait(ExecContext* exec, Lambda&& task)
{
    debug_assert(std::this_thread::get_id() != exec->os_thread);

    std::atomic_flag done = false;
    {
        std::scoped_lock _{exec->queue_mutex};
        // We can avoid moving `task` entirely since its lifetime is guaranteed
        exec->queue.emplace_back([&task] { task(); }, &done);
        unix_check<eventfd_write>(exec->task_fd.get(), 1);
    }
    done.wait(false);
}

// -----------------------------------------------------------------------------

enum class FdEventBit : u32
{
    readable = 1 << 0,
    writable = 1 << 1,
};

enum class FdListenFlag : u32
{
    oneshot = 1 << 0,
};

struct FdListener
{
    Flags<FdEventBit> events;
    Flags<FdListenFlag> flags;

    virtual void handle(fd_t fd, Flags<FdEventBit> events) = 0;
};

// -----------------------------------------------------------------------------

void fd_listen(  ExecContext*, fd_t fd, FdListener*);
void fd_unlisten(ExecContext*, fd_t fd);

template<typename Fn>
void fd_listen(
    ExecContext* exec,
    fd_t fd,
    Flags<FdEventBit> events,
    Fn&& callback,
    Flags<FdListenFlag> flags = {})
{
    struct Listener : FdListener
    {
        Fn lambda;
        Listener(Fn&& lambda): lambda(std::move(lambda)) {}
        virtual void handle(fd_t fd, Flags<FdEventBit> events) { lambda(fd, events); }
    };

    auto listener = ref_create<Listener>(std::move(callback));
    listener->events = events;
    listener->flags = flags;
    fd_listen(exec, fd, listener.get());
}

