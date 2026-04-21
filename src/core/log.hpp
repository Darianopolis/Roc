#pragma once

#include "pch.hpp"
#include "types.hpp"

enum class LogLevel : u32
{
    trace,
    debug,
    info,
    warn,
    error,
    fatal,
};

auto log_get_level() -> LogLevel;
auto log_is_enabled(LogLevel) -> bool;
void log_init(      LogLevel, const char* log_file);
void log(           LogLevel, std::string_view message);

struct LogEntry
{
    LogLevel level;
    std::chrono::system_clock::time_point timestamp;
    u32 start;
    u32 len;
    u32 line_start;
    u32 lines;
    const struct Stacktrace* stacktrace;

    auto message() const noexcept -> std::string_view;
};

struct LogHistory
{
    std::unique_lock<std::recursive_mutex> mutex;
    std::span<const LogEntry> entries;
    u32 lines;
    usz buffer_size;

    auto find(u32 line) const noexcept -> const LogEntry*;
};

auto log_history_get() -> LogHistory;
void log_history_enable(bool enabled);
void log_history_add_listener(std::move_only_function<void(LogEntry*)>);
auto log_history_is_enabled() -> bool;
void log_history_clear();

template<typename ...Args>
void log(LogLevel level, std::format_string<Args...> fmt, Args&&... args)
{
    if (log_get_level() > level) return;
    log(level, std::format(fmt, std::forward<Args>(args)...));
}

#define log_trace(fmt, ...) do { if (log_is_enabled(LogLevel::trace)) log(LogLevel::trace, std::format(fmt __VA_OPT__(,) __VA_ARGS__)); } while (0)
#define log_debug(fmt, ...) do { if (log_is_enabled(LogLevel::debug)) log(LogLevel::debug, std::format(fmt __VA_OPT__(,) __VA_ARGS__)); } while (0)
#define log_info( fmt, ...) do { if (log_is_enabled(LogLevel::info )) log(LogLevel::info,  std::format(fmt __VA_OPT__(,) __VA_ARGS__)); } while (0)
#define log_warn( fmt, ...) do { if (log_is_enabled(LogLevel::warn )) log(LogLevel::warn,  std::format(fmt __VA_OPT__(,) __VA_ARGS__)); } while (0)
#define log_error(fmt, ...) do { if (log_is_enabled(LogLevel::error)) log(LogLevel::error, std::format(fmt __VA_OPT__(,) __VA_ARGS__)); } while (0)
