#include "chrono.hpp"

#include "debug.hpp"
#include "string.hpp"
#include "util.hpp"

std::string core::to_string(std::chrono::system_clock::time_point time, core::TimeFormat format)
{
    tm tm = {};
    gmtime_r(core::ptr_to(std::chrono::system_clock::to_time_t(time)), &tm);

    auto year  = tm.tm_year + 1900;
    auto month = tm.tm_mon + 1;
    auto msec  = std::chrono::duration_cast<std::chrono::milliseconds>(time.time_since_epoch()).count() % 1000;

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

    switch (format) {
        break;case core::TimeFormat::date_pretty:
            return std::format("{}, {} {}{}", weekdays[tm.tm_wday], months[tm.tm_mon], tm.tm_mday, day_suffix(tm.tm_mday));

        break;case core::TimeFormat::iso8601:
            return std::format("{:04}-{:02}-{:02}T{:02}:{:02}:{:02}Z",      year, month, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec);
        break;case core::TimeFormat::datetime:
            return std::format("{:04}-{:02}-{:02} {:02}:{:02}:{:02}",       year, month, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec);
        break;case core::TimeFormat::datetime_ms:
            return std::format("{:04}-{:02}-{:02} {:02}:{:02}:{:02}.{:03}", year, month, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec, msec);

        break;case core::TimeFormat::time:
            return std::format("{:02}:{:02}:{:02}",       tm.tm_hour, tm.tm_min, tm.tm_sec);
        break;case core::TimeFormat::time_ms: {
            return std::format("{:02}:{:02}:{:02}.{:03}", tm.tm_hour, tm.tm_min, tm.tm_sec, msec);
        }
    }

    core::unreachable();
}

std::string core::to_string(std::chrono::duration<f64, std::nano> dur)
{
    f64 nanos = dur.count();

    if (nanos >= 60 * 1e9) {
        if (nanos >= 3600 * 1e9) {
            f64 secs = nanos / 1e9;
            f64 hours = std::floor(secs / 3600);
            secs -= hours * 3600;
            f64 mins = std::floor(secs / 60);
            secs -= mins * 60;
            return std::format("{:.0f}h {:.0f}m {:.0f}s", hours, mins, secs);
        }
        f64 secs = nanos / 1e9;
        f64 mins = std::floor(secs / 60);
        secs -= mins * 60;
        return std::format("{:.0f}m {:.1f}s", mins, secs);
    }

#define CORE_DURATION_FORMAT_CASE(Size, Suffix) \
    if (nanos >= (Size)) { \
        f64 in_size = nanos / (Size); \
        return core::format_with_suffix(Suffix, in_size, core::decimals_for_3sf(in_size)); \
    }

    CORE_DURATION_FORMAT_CASE(1e9, "s")
    CORE_DURATION_FORMAT_CASE(1e6, "ms")
    CORE_DURATION_FORMAT_CASE(1e3, "us")
    CORE_DURATION_FORMAT_CASE(1,   "ns")

    return "0";
}
