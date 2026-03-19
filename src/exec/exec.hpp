#pragma once

#include "core/debug.hpp"
#include "core/object.hpp"
#include "core/enum.hpp"
#include "core/fd.hpp"
#include "core/eventfd.hpp"

// -----------------------------------------------------------------------------

struct exec_task
{
    std::move_only_function<void()> callback;
    std::atomic_flag* sync;
};

struct exec_fd_listener;

struct exec_context
{
    bool stopped = false;

    std::array<ref<exec_fd_listener>, core_fd_limit> listeners  = {};

    std::thread::id os_thread;

    moodycamel::ConcurrentQueue<exec_task> queue;

    u64 tasks_available;
    core_fd task_fd;

    core_fd timer_fd;
    struct timed_event
    {
        std::chrono::steady_clock::time_point expiration;
        std::move_only_function<void()> callback;
    };
    std::deque<timed_event> timed_events;
    std::optional<std::chrono::steady_clock::time_point> current_wakeup;

    core_fd epoll_fd;

    struct {
        u64 events_handled;
        u64 poll_waits;
    } stats;

    ~exec_context();
};

auto exec_create() -> ref<exec_context>;

void exec_set_thread_context(exec_context*);
auto exec_get_thread_context() -> exec_context*;

void exec_run( exec_context*);
void exec_stop(exec_context*);

void exec_add_timer_wakeup(exec_context*, std::chrono::steady_clock::time_point exp);

template<typename Lambda>
void exec_enqueue_timed(exec_context* exec, std::chrono::steady_clock::time_point exp, Lambda&& task)
{
    core_assert(std::this_thread::get_id() == exec->os_thread);

    exec->timed_events.emplace_back(exp, std::move(task));

    exec_add_timer_wakeup(exec, exp);
}

template<typename Lambda>
void exec_enqueue(exec_context* exec, Lambda&& task)
{
    exec->queue.enqueue({ .callback = std::move(task) });
    if (std::this_thread::get_id() == exec->os_thread) {
        exec->tasks_available++;
    } else {
        core_eventfd_signal(exec->task_fd.get(), 1);
    }
}

template<typename Lambda>
void exec_enqueue_and_wait(exec_context* exec, Lambda&& task)
{
    core_assert(std::this_thread::get_id() != exec->os_thread);

    std::atomic_flag done = false;
    // We can avoid moving `task` entirely since its lifetime is guaranteed
    exec->queue.enqueue({ .callback = [&task] { task(); }, .sync = &done });
    core_eventfd_signal(exec->task_fd.get(), 1);
    done.wait(false);
}

// -----------------------------------------------------------------------------

enum class exec_fd_event_bit : u32
{
    readable = 1 << 0,
    writable = 1 << 1,
};

enum class exec_fd_listen_flag : u32
{
    oneshot = 1 << 0,
};

struct exec_fd_listener
{
    flags<exec_fd_event_bit> events;
    flags<exec_fd_listen_flag> flags;

    virtual void handle(int fd, ::flags<exec_fd_event_bit> events) = 0;
};

// -----------------------------------------------------------------------------

void exec_fd_listen(  exec_context*, int fd, exec_fd_listener*);
void exec_fd_unlisten(exec_context*, int fd);

template<typename Fn>
void exec_fd_listen(
    exec_context* exec,
    int fd,
    flags<exec_fd_event_bit> events,
    Fn&& callback,
    flags<exec_fd_listen_flag> flags = {})
{
    struct lambda_listener : exec_fd_listener
    {
        Fn lambda;
        lambda_listener(Fn&& lambda): lambda(std::move(lambda)) {}
        virtual void handle(int fd, ::flags<exec_fd_event_bit> events) { lambda(fd, events); }
    };

    auto listener = core_create<lambda_listener>(std::move(callback));
    listener->events = events;
    listener->flags = flags;
    exec_fd_listen(exec, fd, listener.get());
}

