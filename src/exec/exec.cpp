#include "exec.hpp"

#include "core/chrono.hpp"
#include "core/util.hpp"

// -----------------------------------------------------------------------------

static
u32 to_epoll_events(flags<exec_fd_event_bit> events)
{
    u32 out = 0;
    if (events.contains(exec_fd_event_bit::readable)) out |= EPOLLIN;
    if (events.contains(exec_fd_event_bit::writable)) out |= EPOLLOUT;
    return out;
}

static
flags<exec_fd_event_bit> from_epoll_events(u32 events)
{
    flags<exec_fd_event_bit> out = {};
    if (events & EPOLLIN)  out |= exec_fd_event_bit::readable;
    if (events & EPOLLOUT) out |= exec_fd_event_bit::writable;
    return out;
}

void exec_add_timer_wakeup(exec_context* ctx, std::chrono::steady_clock::time_point exp)
{
    if (ctx->current_wakeup && exp > *ctx->current_wakeup) {
        // log_error("Earlier timer wakeup already set");
        // log_error("  current expiration: {}", core_duration_to_string(*ctx->current_wakeup - std::chrono::steady_clock::now()));
        // log_error("  new expiration: {}", core_duration_to_string(exp - std::chrono::steady_clock::now()));
        return;
    }

    ctx->current_wakeup = exp;

    // log_trace("Next timeout in {}", core_duration_to_string(exp - std::chrono::steady_clock::now()));

    unix_check<timerfd_settime>(ctx->timer_fd.get(), TFD_TIMER_ABSTIME, ptr_to(itimerspec {
        .it_value = core_steady_clock_to_timespec<CLOCK_MONOTONIC>(exp),
    }), nullptr);
}

static
void handle_timer(exec_context* ctx, int fd)
{
    u64 expirations;
    if (unix_check<read>(fd, &expirations, sizeof(expirations)).value != sizeof(expirations)) return;

    auto now = std::chrono::steady_clock::now();
    ctx->current_wakeup = std::nullopt;

    std::optional<std::chrono::steady_clock::time_point> min_exp;

    std::vector<std::move_only_function<void()>> dequeued;
    std::erase_if(ctx->timed_events, [&](auto& event) {
        if (now >= event.expiration) {
            dequeued.emplace_back(std::move(event.callback));
            return true;
        } else {
            min_exp = min_exp ? std::min(*min_exp, event.expiration) : event.expiration;
        }
        return false;
    });

    ctx->stats.events_handled += dequeued.size();
    for (auto& callback : dequeued) {
        callback();
    }

    if (min_exp) {
        exec_add_timer_wakeup(ctx, *min_exp);
    }
}

ref<exec_context> exec_create()
{
    auto ctx = core_create<exec_context>();

    ctx->os_thread = std::this_thread::get_id();

    ctx->epoll_fd = core_fd(unix_check<epoll_create1>(EPOLL_CLOEXEC).value);

    ctx->task_fd = core_fd(unix_check<eventfd>(0, EFD_CLOEXEC | EFD_NONBLOCK).value);
    exec_fd_listen(ctx.get(), ctx->task_fd.get(), exec_fd_event_bit::readable, [ctx = ctx.get()](int fd, flags<exec_fd_event_bit> events) {
        eventfd_t tasks = {};
        unix_check<eventfd_read>(fd, &tasks);
        ctx->tasks_available += tasks;

        // Don't double dip task event stats
        ctx->stats.events_handled--;
    });

    ctx->timer_fd = core_fd(unix_check<timerfd_create>(CLOCK_MONOTONIC, TFD_NONBLOCK | TFD_CLOEXEC).value);
    exec_fd_listen(ctx.get(), ctx->timer_fd.get(), exec_fd_event_bit::readable, [ctx = ctx.get()](int fd, flags<exec_fd_event_bit> events) {
        handle_timer(ctx, fd);

        // Don't double dip timer event stats
        ctx->stats.events_handled--;
    });

    return ctx;
}

thread_local exec_context* exec_thread_context;

void exec_set_thread_context(exec_context* ctx)
{
    exec_thread_context = ctx;
}

auto exec_get_thread_context() -> exec_context*
{
    return exec_thread_context;
}

#define CORE_EVENT_LOOP_CHECK_LISTENERS 1

exec_context::~exec_context()
{
    core_assert(stopped);

    exec_fd_unlisten(this, task_fd.get());
    exec_fd_unlisten(this, timer_fd.get());

#if CORE_EVENT_LOOP_CHECK_LISTENERS
    for (auto[i, listener] : listeners | std::views::enumerate) {
        core_assert(!listener, "Listener for ({}) still registered", i);
    }
#endif
}

void exec_stop(exec_context* ctx)
{
    ctx->stopped = true;

#if CORE_EVENT_LOOP_CHECK_LISTENERS
    auto user_listeners = ctx->listeners
        | std::views::enumerate
        | std::views::filter([&](auto e) {
            auto[fd, l] = e;
            return l && fd != ctx->timer_fd.get() && fd != ctx->task_fd.get();
        });

    if (u32 listeners = core_count(user_listeners)) {
        // Just log an error for now, in the future we will be more strict and
        // assert if any user registered listeners are still attached at this point.
        log_error("Stopping event loop with {} registered listeners remaining!", listeners);
    }

    for (auto[i, listener] : user_listeners) {
        log_error("  ({})", i);
    }
#endif
}

void exec_run(exec_context* ctx)
{
    core_assert(!exec_get_thread_context());
    exec_set_thread_context(ctx);

    ctx->os_thread = std::this_thread::get_id();

    static constexpr usz max_epoll_events = 64;
    std::array<epoll_event, max_epoll_events> events;

    while (!ctx->stopped) {

        // Check for new fd events

        i32 timeout = 0;
        if (!ctx->tasks_available) {
            ctx->stats.poll_waits++;
            timeout = -1;
        }
        auto[count, error] = unix_check<epoll_wait, EAGAIN, EINTR>(ctx->epoll_fd.get(), events.data(), events.size(), timeout);
        if (error) {
            if (error == EAGAIN || error == EINTR) {
                if (!ctx->tasks_available) continue;
            } else {
                // At this point, we can't assume that we'll receive any future FD events.
                // Since this includes all user input, the only safe thing to do is
                // immediately terminate to avoid locking out the user's system.
                core_debugkill();
            }
        }

        // Flush fd events

        if (count > 0) {
            for (i32 i = 0; i < count; ++i) {
                auto fd = events[i].data.fd;
                auto l = ctx->listeners[fd];
                if (!l) continue;

                ctx->stats.events_handled++;

                auto event_bits = from_epoll_events(events[i].events);
                if (l->flags.contains(exec_fd_listen_flag::oneshot)) {
                    ref listener = l;
                    exec_fd_unlisten(ctx, fd);
                    listener->handle(fd, event_bits);
                } else {
                    l->handle(fd, event_bits);
                }
            }
        }

        // Flush tasks

        if (ctx->tasks_available) {
            u64 available = std::exchange(ctx->tasks_available, 0);

            ctx->stats.events_handled += available;
            for (u64 i = 0; i < available; ++i) {
                exec_task task;
                while (!ctx->queue.try_dequeue(task));

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

void exec_fd_listen(
    exec_context* ctx,
    int fd,
    exec_fd_listener* listener)
{
    auto events = listener->events;

    core_assert(core_fd_is_valid(fd));

    core_assert(events);
    core_assert(!ctx->listeners[fd]);

    ctx->listeners[fd] = listener;

    unix_check<epoll_ctl>(ctx->epoll_fd.get(), EPOLL_CTL_ADD, fd, ptr_to(epoll_event {
        .events = to_epoll_events(events),
        .data {
            .fd = fd,
        }
    }));
}

void exec_fd_unlisten(exec_context* ctx, int fd)
{
    core_assert(core_fd_is_valid(fd));

    if (!ctx->listeners[fd]) {
        log_warn("fd does not have registered listener");
    }

    auto res = unix_check<epoll_ctl>(ctx->epoll_fd.get(), EPOLL_CTL_DEL, fd, nullptr);
    core_assert(res.ok());

    ctx->listeners[fd] = nullptr;
}
