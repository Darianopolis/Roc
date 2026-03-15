#pragma once

#include "debug.hpp"
#include "object.hpp"
#include "fd.hpp"
#include "enum.hpp"

namespace core
{
    u64  eventfd_read( int fd);
    void eventfd_signal(int fd, u64 inc);
}

// -----------------------------------------------------------------------------

namespace core
{
    struct Task
    {
        std::move_only_function<void()> callback;
        std::atomic_flag* sync;
    };

    struct EventLoop
    {
        bool stopped = false;

        std::thread::id main_thread;

        moodycamel::ConcurrentQueue<core::Task> queue;

        u64 tasks_available;
        core::Fd task_fd;

        core::Fd timer_fd;
        struct timed_event
        {
            std::chrono::steady_clock::time_point expiration;
            std::move_only_function<void()> callback;
        };
        std::deque<timed_event> timed_events;
        std::optional<std::chrono::steady_clock::time_point> current_wakeup;

        u32 internal_listener_count;
        u32 listener_count = 0;

        core::Fd epoll_fd;

        struct {
            u64 events_handled;
            u64 poll_waits;
        } stats;

        ~EventLoop();
    };
}

namespace core::event_loop
{
    auto create() -> core::Ref<core::EventLoop>;
    void run( core::EventLoop*);
    void stop(core::EventLoop*);

    void timer_expiry_impl(core::EventLoop*, std::chrono::steady_clock::time_point exp);

    template<typename Lambda>
    void enqueue_timed(core::EventLoop* loop, std::chrono::steady_clock::time_point exp, Lambda&& task)
    {
        core_assert(std::this_thread::get_id() == loop->main_thread);

        loop->timed_events.emplace_back(exp, std::move(task));

        core::event_loop::timer_expiry_impl(loop, exp);
    }

    template<typename Lambda>
    void enqueue(core::EventLoop* loop, Lambda&& task)
    {
        loop->queue.enqueue({ .callback = std::move(task) });
        if (std::this_thread::get_id() == loop->main_thread) {
            loop->tasks_available++;
        } else {
            core::eventfd_signal(loop->task_fd.get(), 1);
        }
    }

    template<typename Lambda>
    void enqueue_and_wait(core::EventLoop* loop, Lambda&& task)
    {
        core_assert(std::this_thread::get_id() != loop->main_thread);

        std::atomic_flag done = false;
        // We can avoid moving `task` entirely since its lifetime is guaranteed
        loop->queue.enqueue({ .callback = [&task] { task(); }, .sync = &done });
        core::eventfd_signal(loop->task_fd.get(), 1);
        done.wait(false);
    }
}

// -----------------------------------------------------------------------------

namespace core
{
    enum class FdEventBit : u32
    {
        readable = 1 << 0,
        writable = 1 << 1,
    };

    enum class FdListenFlag : u32
    {
        oneshot = 1 << 0,
    };

    using FdListenerFn = void(int, core::Flags<core::FdEventBit> events);

    struct FdListener
    {
        core::Weak<core::EventLoop> loop = nullptr;
        core::Flags<core::FdEventBit> events;
        core::Flags<core::FdListenFlag> flags;

        virtual void handle(int fd, core::Flags<core::FdEventBit> events) = 0;
    };
}

// -----------------------------------------------------------------------------

namespace core::fd
{
    void add_listener(int, core::EventLoop*, core::FdListener*);

    template<typename Fn>
    void add_listener(
        int fd,
        core::EventLoop* loop,
        core::Flags<core::FdEventBit> events,
        Fn&& callback,
        core::Flags<core::FdListenFlag> flags = {})
    {
        struct core_fd_listener_lambda : core::FdListener
        {
            Fn lambda;
            core_fd_listener_lambda(Fn&& lambda): lambda(std::move(lambda)) {}
            virtual void handle(int fd, core::Flags<core::FdEventBit> events) { lambda(fd, events); }
        };

        auto listener = core::create<core_fd_listener_lambda>(std::move(callback));
        listener->events = events;
        listener->flags = flags;
        core::fd::add_listener(fd, loop, listener.get());
    }
}
