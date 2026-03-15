#pragma once

#include "pch.hpp"
#include "types.hpp"

namespace core
{
    enum class LogLevel : u32
    {
        trace,
        debug,
        info,
        warn,
        error,
        fatal,
    };

    auto get_log_level() -> core::LogLevel;
    bool is_log_level_enabled(core::LogLevel);
    void init_log(core::LogLevel, const char* log_file);
    void      log(core::LogLevel, std::string_view message);

    struct Stacktrace;

    struct LogEntry
    {
        core::LogLevel level;
        std::chrono::system_clock::time_point timestamp;
        u32 start;
        u32 len;
        u32 line_start;
        u32 lines;
        const struct core::Stacktrace* stacktrace;

        auto message() const noexcept -> std::string_view;
    };

    struct LogHistory
    {
        std::unique_lock<std::recursive_mutex> mutex;
        std::span<const core::LogEntry> entries;
        u32 lines;
        usz buffer_size;

        auto find(u32 line) const noexcept -> const core::LogEntry*;
    };

    auto log_get_history() -> core::LogHistory;
    void log_set_history_enabled(bool enabled);
    bool log_is_history_enabled();
    void log_clear_history();

    template<typename ...Args>
    void log(core::LogLevel level, std::format_string<Args...> fmt, Args&&... args)
    {
        if (core::get_log_level() > level) return;
        core::log(level, std::vformat(fmt.get(), std::make_format_args(args...)));
    }
}

#define log_trace(fmt, ...) do { if (core::is_log_level_enabled(core::LogLevel::trace)) core::log(core::LogLevel::trace, std::format(fmt __VA_OPT__(,) __VA_ARGS__)); } while (0)
#define log_debug(fmt, ...) do { if (core::is_log_level_enabled(core::LogLevel::debug)) core::log(core::LogLevel::debug, std::format(fmt __VA_OPT__(,) __VA_ARGS__)); } while (0)
#define log_info( fmt, ...) do { if (core::is_log_level_enabled(core::LogLevel::info )) core::log(core::LogLevel::info,  std::format(fmt __VA_OPT__(,) __VA_ARGS__)); } while (0)
#define log_warn( fmt, ...) do { if (core::is_log_level_enabled(core::LogLevel::warn )) core::log(core::LogLevel::warn,  std::format(fmt __VA_OPT__(,) __VA_ARGS__)); } while (0)
#define log_error(fmt, ...) do { if (core::is_log_level_enabled(core::LogLevel::error)) core::log(core::LogLevel::error, std::format(fmt __VA_OPT__(,) __VA_ARGS__)); } while (0)
