#pragma once

#include "exec.hpp"

struct TimerTask
{
    std::chrono::steady_clock::time_point expiration;
    std::move_only_function<void()> callback;
};

struct Timer
{
    ExecContext* exec;

    Fd fd;

    std::deque<TimerTask> timed_events;
    std::optional<std::chrono::steady_clock::time_point> current_wakeup;

    ~Timer();
};

auto timer_create(ExecContext*) -> Ref<Timer>;

void timer_add_wakeup(Timer*, std::chrono::steady_clock::time_point exp);

template<typename Lambda>
void timer_enqueue(Timer* timer, std::chrono::steady_clock::time_point exp, Lambda&& task)
{
    debug_assert(std::this_thread::get_id() == timer->exec->os_thread);

    timer->timed_events.emplace_back(exp, std::move(task));

    timer_add_wakeup(timer, exp);
}
