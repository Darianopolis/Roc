#pragma once

#include "types.hpp"

namespace core
{
    inline
    std::chrono::system_clock::time_point current_time()
    {
        return std::chrono::system_clock::now();
    }
}

namespace core::steady_clock
{
    // Assume steady_clock is implemented as CLOCK_MONOTONIC.
    static constexpr int id = CLOCK_MONOTONIC;

    template<int ClockID>
    auto from_timespec(const timespec& ts) -> std::chrono::steady_clock::time_point
    {
        static_assert(core::steady_clock::id == ClockID);

        auto ns = ts.tv_sec * 1'000'000'000 + ts.tv_nsec;
        auto dur = std::chrono::nanoseconds(ns);
        return std::chrono::steady_clock::time_point(dur);
    }

    template<int ClockID>
    auto to_timespec(std::chrono::steady_clock::time_point tp) -> timespec
    {
        static_assert(core::steady_clock::id == ClockID);

        auto ns = tp.time_since_epoch().count();

        timespec ts;
        ts.tv_sec  = ns / 1'000'000'000;
        ts.tv_nsec = ns % 1'000'000'000;
        return ts;
    }
}

namespace core
{
    enum class TimeFormat : u32
    {
        iso8601,
        date_pretty,
        datetime,
        datetime_ms,
        time,
        time_ms,
    };

    auto to_string(std::chrono::system_clock::time_point, core::TimeFormat) -> std::string;

    auto to_string(std::chrono::duration<f64, std::nano> dur) -> std::string;
}
