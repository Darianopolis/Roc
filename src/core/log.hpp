#pragma once

#include "pch.hpp"
#include "types.hpp"
#include "signal.hpp"

enum class LogSemantic : u32
{
    trace,
    debug,
    info,
    warn,
    error,
    fatal,
};

void log_init(const char* log_path);
void log_deinit();

void log_set_file(const std::filesystem::path& log_file);
void log(LogSemantic, std::string_view message);

struct LogEntry
{
    LogSemantic semantic;
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

struct LogSignals
{
    Signal<void(LogEntry*)> log_entry;
};

auto log_history_get() -> LogHistory;
void log_history_enable(bool enabled);
auto log_history_get_signals() -> LogSignals&;
auto log_history_is_enabled() -> bool;
void log_history_clear();

template<typename ...Args>
void log(LogSemantic semantic, std::format_string<Args...> fmt, Args&&... args)
{
    log(semantic, std::format(fmt, std::forward<Args>(args)...));
}

#define log_trace(...) log(LogSemantic::trace, __VA_ARGS__)
#define log_debug(...) log(LogSemantic::debug, __VA_ARGS__)
#define log_info( ...) log(LogSemantic::info,  __VA_ARGS__)
#define log_warn( ...) log(LogSemantic::warn,  __VA_ARGS__)
#define log_error(...) log(LogSemantic::error, __VA_ARGS__)
