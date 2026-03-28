#pragma once

#include "types.hpp"
#include "debug.hpp"
#include "util.hpp"

inline
std::chrono::system_clock::time_point time_current()
{
    return std::chrono::system_clock::now();
}

/*
 * Assume steady_clock is implemented as CLOCK_MONOTONIC.
 */
static constexpr int steady_clock_id = CLOCK_MONOTONIC;

template<int ClockID>
std::chrono::steady_clock::time_point steady_clock_from_timespec(const timespec& ts)
{
    static_assert(steady_clock_id == ClockID);

    auto ns = ts.tv_sec * 1'000'000'000 + ts.tv_nsec;
    auto dur = std::chrono::nanoseconds(ns);
    return std::chrono::steady_clock::time_point(dur);
}

template<int ClockID>
timespec steady_clock_to_timespec(std::chrono::steady_clock::time_point tp)
{
    static_assert(steady_clock_id == ClockID);

    auto ns = tp.time_since_epoch().count();

    timespec ts;
    ts.tv_sec  = ns / 1'000'000'000;
    ts.tv_nsec = ns % 1'000'000'000;
    return ts;
}

enum class TimeFormat : u32
{
    iso8601,
    date_pretty,
    datetime,
    datetime_ms,
    time,
    time_ms,
};

struct FmtTime
{
    std::chrono::system_clock::time_point time_point;
    TimeFormat behavior;
};

template<>
struct std::formatter<FmtTime> {
    constexpr auto parse(auto& ctx) { return ctx.begin(); }
    auto format(FmtTime time, auto& ctx) const
    {
        tm tm = {};
        gmtime_r(ptr_to(std::chrono::system_clock::to_time_t(time.time_point)), &tm);

        auto year  = tm.tm_year + 1900;
        auto month = tm.tm_mon + 1;
        auto msec  = std::chrono::duration_cast<std::chrono::milliseconds>(time.time_point.time_since_epoch()).count() % 1000;

        static constexpr const char* weekdays[] = {
            "Sunday",
            "Monday",
            "Tuesday",
            "Wednesday",
            "Thursday",
            "Friday",
            "Saturday",
        };

        static constexpr const char* months[] = {
            "January",
            "February",
            "March",
            "April",
            "May",
            "June",
            "July",
            "August",
            "September",
            "October",
            "November",
            "December"
        };

        static constexpr auto day_suffix = [&](int i) {
            switch (i) {
                break;case 1: case 21: case 31: return "st";
                break;case 2: case 22:          return "nd";
                break;case 3: case 23:          return "rd";
                break;default:                  return "th";
            }
        };

        switch (time.behavior) {
            break;case TimeFormat::date_pretty:
                return std::format_to(ctx.out(), "{}, {} {}{}", weekdays[tm.tm_wday], months[tm.tm_mon], tm.tm_mday, day_suffix(tm.tm_mday));

            break;case TimeFormat::iso8601:
                return std::format_to(ctx.out(), "{:04}-{:02}-{:02}T{:02}:{:02}:{:02}Z",      year, month, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec);
            break;case TimeFormat::datetime:
                return std::format_to(ctx.out(), "{:04}-{:02}-{:02} {:02}:{:02}:{:02}",       year, month, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec);
            break;case TimeFormat::datetime_ms:
                return std::format_to(ctx.out(), "{:04}-{:02}-{:02} {:02}:{:02}:{:02}.{:03}", year, month, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec, msec);

            break;case TimeFormat::time:
                return std::format_to(ctx.out(), "{:02}:{:02}:{:02}",       tm.tm_hour, tm.tm_min, tm.tm_sec);
            break;case TimeFormat::time_ms: {
                return std::format_to(ctx.out(), "{:02}:{:02}:{:02}.{:03}", tm.tm_hour, tm.tm_min, tm.tm_sec, msec);
            }
        }

        debug_unreachable();
    }
};

// -----------------------------------------------------------------------------

template<typename Rep, typename Period>
struct std::formatter<std::chrono::duration<Rep, Period>> {
    constexpr auto parse(auto& ctx) { return ctx.begin(); }
    constexpr auto format(std::chrono::duration<Rep, Period> v, auto& ctx) const
    {
        f64 nanos = std::chrono::duration<f64, std::nano>(v).count();

        auto with_suffix = [&](std::string_view suffix, f64 amount) {
            u32 decimals = amount < 10 ? 2 : (amount < 100 ? 1 : 0);
            return std::format_to(ctx.out(), "{:.{}f}{}", amount, decimals, suffix);
        };

        if (nanos >= 60 * 1e9) {
            f64 secs = nanos / 1e9;
            if (nanos >= 3600 * 1e9) {
                f64 hours = std::floor(secs / 3600);
                secs -= hours * 3600;
                f64 mins = std::floor(secs / 60);
                secs -= mins * 60;
                return std::format_to(ctx.out(), "{:.0f}h {:.0f}m {:.0f}s", hours, mins, secs);
            } else {
                f64 mins = std::floor(secs / 60);
                secs -= mins * 60;
                return std::format_to(ctx.out(), "{:.0f}m {:.1f}s", mins, secs);
            }
        }
        if (nanos >= 1e9) return with_suffix("s",  nanos / 1e9);
        if (nanos >= 1e6) return with_suffix("ms", nanos / 1e6);
        if (nanos >= 1e3) return with_suffix("us", nanos / 1e3);
        if (nanos >= 1)   return with_suffix("ns", nanos);

        return std::format_to(ctx.out(), "0");
    }
};
