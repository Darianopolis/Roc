#pragma once

#include "debug.hpp"
#include "object.hpp"
#include "enum.hpp"
#include "fd.hpp"
#include "signal.hpp"

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

    std::thread::id os_thread;

    std::mutex queue_mutex;
    std::deque<ExecTask> queue;

    u64 tasks_available;
    Fd task_fd;

    Fd epoll_fd;

    std::array<Ref<FdListener>, fd_limit> listeners = {};

    ~ExecContext();
};

auto exec_create() -> Ref<ExecContext>;

void exec_set_thread_context(ExecContext*);
auto exec_get_thread_context() -> ExecContext*;

void exec_run( ExecContext*);
void exec_stop(ExecContext*);

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

