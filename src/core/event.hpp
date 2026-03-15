#pragma once

#include "debug.hpp"
#include "object.hpp"
#include "fd.hpp"

u64  core_eventfd_read( int fd);
void core_eventfd_signal(int fd, u64 inc);

// -----------------------------------------------------------------------------


struct core_event_source;
struct core_fd;

struct core_task
{
    std::move_only_function<void()> callback;
    std::atomic_flag* sync;
};

struct core_event_loop
{
    bool stopped = false;

    std::thread::id main_thread;

    moodycamel::ConcurrentQueue<core_task> queue;

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

    u32 internal_listener_count;
    u32 listener_count = 0;

    core_fd epoll_fd;

    struct {
        u64 events_handled;
        u64 poll_waits;
    } stats;

    ~core_event_loop();
};

ref<core_event_loop> core_event_loop_create();
void core_event_loop_run( core_event_loop*);
void core_event_loop_stop(core_event_loop*);

void core_event_loop_timer_expiry_impl(core_event_loop*, std::chrono::steady_clock::time_point exp);

template<typename Lambda>
void core_event_loop_enqueue_timed(core_event_loop* loop, std::chrono::steady_clock::time_point exp, Lambda&& task)
{
    core_assert(std::this_thread::get_id() == loop->main_thread);

    loop->timed_events.emplace_back(exp, std::move(task));

    core_event_loop_timer_expiry_impl(loop, exp);
}

template<typename Lambda>
void core_event_loop_enqueue(core_event_loop* loop, Lambda&& task)
{
    loop->queue.enqueue({ .callback = std::move(task) });
    if (std::this_thread::get_id() == loop->main_thread) {
        loop->tasks_available++;
    } else {
        core_eventfd_signal(loop->task_fd.get(), 1);
    }
}

template<typename Lambda>
void core_event_loop_enqueue_and_wait(core_event_loop* loop, Lambda&& task)
{
    core_assert(std::this_thread::get_id() != loop->main_thread);

    std::atomic_flag done = false;
    // We can avoid moving `task` entirely since its lifetime is guaranteed
    loop->queue.enqueue({ .callback = [&task] { task(); }, .sync = &done });
    core_eventfd_signal(loop->task_fd.get(), 1);
    done.wait(false);
}

// -----------------------------------------------------------------------------

enum class core_fd_event_bit : u32
{
    readable = 1 << 0,
    writable = 1 << 1,
};
using core_fd_event_bits = flags<core_fd_event_bit>;

enum class core_fd_listen_flag : u32
{
    oneshot = 1 << 0,
};
using core_fd_listen_flags = flags<core_fd_listen_flag>;

using core_fd_listener_fn = void(int, core_fd_event_bits events);

struct core_fd_listener
{
    weak<core_event_loop> loop = nullptr;
    core_fd_event_bits events;
    core_fd_listen_flags flags;

    virtual void handle(int fd, core_fd_event_bits events) = 0;
};

// -----------------------------------------------------------------------------

void core_fd_add_listener(int, core_event_loop*, core_fd_listener*);

template<typename Fn>
void core_fd_add_listener(
    int fd,
    core_event_loop* loop,
    core_fd_event_bits events,
    Fn&& callback,
    core_fd_listen_flags flags = {})
{
    struct core_fd_listener_lambda : core_fd_listener
    {
        Fn lambda;
        core_fd_listener_lambda(Fn&& lambda): lambda(std::move(lambda)) {}
        virtual void handle(int fd, core_fd_event_bits events) { lambda(fd, events); }
    };

    auto listener = core_create<core_fd_listener_lambda>(std::move(callback));
    listener->events = events;
    listener->flags = flags;
    core_fd_add_listener(fd, loop, listener.get());
}
