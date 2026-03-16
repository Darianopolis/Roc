#include "event.hpp"
#include "chrono.hpp"
#include "util.hpp"

u64 core_eventfd_read(int fd)
{
    u64 count = 0;
    return (unix_check<read, EAGAIN, EINTR>(fd, &count, sizeof(count)).value == sizeof(count)) ? count : 0;
}

void core_eventfd_signal(int fd, u64 inc)
{
    unix_check<write>(fd, &inc, sizeof(inc));
}

// -----------------------------------------------------------------------------

static
u32 to_epoll_events(flags<core_fd_event_bit> events)
{
    u32 out = 0;
    if (events.contains(core_fd_event_bit::readable))  out |= EPOLLIN;
    if (events.contains(core_fd_event_bit::writable)) out |= EPOLLOUT;
    return out;
}

static
flags<core_fd_event_bit> from_epoll_events(u32 events)
{
    flags<core_fd_event_bit> out = {};
    if (events & EPOLLIN)  out |= core_fd_event_bit::readable;
    if (events & EPOLLOUT) out |= core_fd_event_bit::writable;
    return out;
}

void core_event_loop_timer_expiry_impl(core_event_loop* loop, std::chrono::steady_clock::time_point exp)
{
    if (loop->current_wakeup && exp > *loop->current_wakeup) {
        // log_error("Earlier timer wakeup already set");
        // log_error("  current expiration: {}", core_duration_to_string(*loop->current_wakeup - std::chrono::steady_clock::now()));
        // log_error("  new expiration: {}", core_duration_to_string(exp - std::chrono::steady_clock::now()));
        return;
    }

    loop->current_wakeup = exp;

    // log_trace("Next timeout in {}", core_duration_to_string(exp - std::chrono::steady_clock::now()));

    unix_check<timerfd_settime>(loop->timer_fd.get(), TFD_TIMER_ABSTIME, ptr_to(itimerspec {
        .it_value = core_steady_clock_to_timespec<CLOCK_MONOTONIC>(exp),
    }), nullptr);
}

static
void handle_timer(core_event_loop* loop, int fd)
{
    u64 expirations;
    if (unix_check<read>(fd, &expirations, sizeof(expirations)).value != sizeof(expirations)) return;

    auto now = std::chrono::steady_clock::now();
    loop->current_wakeup = std::nullopt;

    std::optional<std::chrono::steady_clock::time_point> min_exp;

    std::vector<std::move_only_function<void()>> dequeued;
    std::erase_if(loop->timed_events, [&](auto& event) {
        if (now >= event.expiration) {
            dequeued.emplace_back(std::move(event.callback));
            return true;
        } else {
            min_exp = min_exp ? std::min(*min_exp, event.expiration) : event.expiration;
        }
        return false;
    });

    loop->stats.events_handled += dequeued.size();
    for (auto& callback : dequeued) {
        callback();
    }

    if (min_exp) {
        core_event_loop_timer_expiry_impl(loop, *min_exp);
    }
}

ref<core_event_loop> core_event_loop_create()
{
    auto loop = core_create<core_event_loop>();
    loop->main_thread = std::this_thread::get_id();

    loop->epoll_fd = core_fd_adopt(unix_check<epoll_create1>(EPOLL_CLOEXEC).value);

    loop->task_fd = core_fd_adopt(unix_check<eventfd>(0, EFD_CLOEXEC | EFD_NONBLOCK).value);
    core_fd_add_listener(loop->task_fd.get(), loop.get(), core_fd_event_bit::readable, [loop = loop.get()](int fd, flags<core_fd_event_bit> events) {
        loop->tasks_available += core_eventfd_read(fd);

        // Don't double dip task event stats
        loop->stats.events_handled--;
    });

    loop->timer_fd = core_fd_adopt(unix_check<timerfd_create>(CLOCK_MONOTONIC, TFD_NONBLOCK | TFD_CLOEXEC).value);
    core_fd_add_listener(loop->timer_fd.get(), loop.get(), core_fd_event_bit::readable, [loop = loop.get()](int fd, flags<core_fd_event_bit> events) {
        handle_timer(loop, fd);

        // Don't double dip timer event stats
        loop->stats.events_handled--;
    });

    loop->internal_listener_count = loop->listener_count;

    return loop;
}

core_event_loop::~core_event_loop()
{
    core_assert(stopped);

    task_fd = nullptr;
    timer_fd = nullptr;

    core_assert(listener_count == 0);
}

void core_event_loop_stop(core_event_loop* loop)
{
    loop->stopped = true;

    if (loop->listener_count > loop->internal_listener_count) {
        // Just log an error for now, in the future we will be more strict and
        // assert if any user registered listeners are still attached at this point.
        log_error("Stopping event loop with {} registered listeners remaining!",
            loop->listener_count - loop->internal_listener_count);
    }
}

void core_event_loop_run(core_event_loop* loop)
{
    core_assert(std::this_thread::get_id() == loop->main_thread);

    static constexpr usz max_epoll_events = 64;
    std::array<epoll_event, max_epoll_events> events;

    while (!loop->stopped) {

        // Check for new fd events

        i32 timeout = 0;
        if (!loop->tasks_available) {
            loop->stats.poll_waits++;
            timeout = -1;
        }
        auto[count, error] = unix_check<epoll_wait, EAGAIN, EINTR>(loop->epoll_fd.get(), events.data(), events.size(), timeout);
        if (error) {
            if (error == EAGAIN || error == EINTR) {
                if (!loop->tasks_available) continue;
            } else {
                // At this point, we can't assume that we'll receive any future FD events.
                // Since this includes all user input, the only safe thing to do is
                // immediately terminate to avoid locking out the user's system.
                core_debugkill();
            }
        }

        // Flush fd events

        if (count > 0) {
            std::array<core_fd, max_epoll_events> sources;
            for (i32 i = 0; i < count; ++i) {
                sources[i] = core_fd(events[i].data.fd);
            }

            for (i32 i = 0; i < count; ++i) {
                if (!sources[i]) continue;

                loop->stats.events_handled++;

                auto l = core_fd_get_listener(sources[i].get());
                auto event_bits = from_epoll_events(events[i].events);
                if (l->flags.contains(core_fd_listen_flag::oneshot)) {
                    ref listener = l;
                    core_fd_remove_listener(sources[i].get());
                    listener->handle(sources[i].get(), event_bits);
                } else {
                    l->handle(sources[i].get(), event_bits);
                }
            }
        }

        // Flush tasks

        if (loop->tasks_available) {
            u64 available = std::exchange(loop->tasks_available, 0);

            loop->stats.events_handled += available;
            for (u64 i = 0; i < available; ++i) {
                core_task task;
                while (!loop->queue.try_dequeue(task));

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

void core_fd_add_listener(
    int fd,
    core_event_loop* loop,
    core_fd_listener* listener)
{
    auto events = listener->events;

    core_assert(events);
    core_assert(!listener->loop);
    core_assert(!core_fd_get_listener(fd));

    loop->listener_count++;

    listener->loop = loop;
    core_fd_set_listener(fd, listener);

    unix_check<epoll_ctl>(loop->epoll_fd.get(), EPOLL_CTL_ADD, fd, ptr_to(epoll_event {
        .events = to_epoll_events(events),
        .data {
            .fd = fd,
        }
    }));
}

void core_fd_remove_listener(int fd)
{
    auto* listener = core_fd_get_listener(fd);
    core_fd_get_listener(fd)->loop->listener_count--;

    unix_check<epoll_ctl>(listener->loop->epoll_fd.get(), EPOLL_CTL_DEL, fd, nullptr);
    listener->loop = nullptr;
    core_fd_set_listener(fd, nullptr);
}
