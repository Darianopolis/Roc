#include "timer.hpp"

#include "chrono.hpp"

void timer_add_wakeup(Timer* timer, std::chrono::steady_clock::time_point exp)
{
    if (timer->current_wakeup && exp > *timer->current_wakeup) {
        return;
    }

    timer->current_wakeup = exp;

    unix_check<timerfd_settime>(timer->fd.get(), TFD_TIMER_ABSTIME, ptr_to(itimerspec {
        .it_value = steady_clock_to_timespec<CLOCK_MONOTONIC>(exp),
    }), nullptr);
}

static
void handle_task_eventfd(Timer* timer, fd_t fd)
{
    u64 expirations;
    if (unix_check<read>(fd, &expirations, sizeof(expirations)).value != sizeof(expirations)) return;

    auto now = std::chrono::steady_clock::now();
    timer->current_wakeup = std::nullopt;

    std::optional<std::chrono::steady_clock::time_point> min_exp;

    std::vector<std::move_only_function<void()>> dequeued;
    std::erase_if(timer->timed_events, [&](auto& event) {
        if (now >= event.expiration) {
            dequeued.emplace_back(std::move(event.callback));
            return true;
        } else {
            min_exp = min_exp ? std::min(*min_exp, event.expiration) : event.expiration;
        }
        return false;
    });

    for (auto& callback : dequeued) {
        callback();
    }

    if (min_exp) {
        timer_add_wakeup(timer, *min_exp);
    }
}

auto timer_create(ExecContext* exec) -> Ref<Timer>
{
    auto timer = ref_create<Timer>();
    timer->exec = exec;

    timer->fd = Fd(unix_check<timerfd_create>(CLOCK_MONOTONIC, TFD_NONBLOCK | TFD_CLOEXEC).value);
    fd_listen(exec, timer->fd.get(), FdEventBit::readable, [timer = timer.get()](fd_t fd, Flags<FdEventBit>) {
        handle_task_eventfd(timer, fd);
    });

    return timer;
}

Timer::~Timer()
{
    fd_unlisten(exec, fd.get());
}
