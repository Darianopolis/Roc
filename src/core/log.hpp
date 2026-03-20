#pragma once

#include "pch.hpp"
#include "types.hpp"

enum class core_log_level : u32
{
    trace,
    debug,
    info,
    warn,
    error,
    fatal,
};

auto core_log_get_level() -> core_log_level;
bool core_log_is_enabled(core_log_level);
void core_log_init(      core_log_level, const char* log_file);
void core_log(           core_log_level, std::string_view message);

struct core_log_entry
{
    core_log_level level;
    std::chrono::system_clock::time_point timestamp;
    u32 start;
    u32 len;
    u32 line_start;
    u32 lines;
    const struct core_stacktrace* stacktrace;

    std::string_view message() const noexcept;
};
struct core_log_history
{
    std::unique_lock<std::recursive_mutex> mutex;
    std::span<const core_log_entry> entries;
    u32 lines;
    usz buffer_size;

    const core_log_entry* find(u32 line) const noexcept;
};
auto core_log_get_history() -> core_log_history;
void core_log_set_history_enabled(bool enabled);
bool core_log_is_history_enabled();
void core_log_clear_history();

template<typename ...Args>
void core_log(core_log_level level, std::format_string<Args...> fmt, Args&&... args)
{
    if (core_log_get_level() > level) return;
    core_log(level, std::vformat(fmt.get(), std::make_format_args(args...)));
}

#define log_trace(fmt, ...) do { if (core_log_is_enabled(core_log_level::trace)) core_log(core_log_level::trace, std::format(fmt __VA_OPT__(,) __VA_ARGS__)); } while (0)
#define log_debug(fmt, ...) do { if (core_log_is_enabled(core_log_level::debug)) core_log(core_log_level::debug, std::format(fmt __VA_OPT__(,) __VA_ARGS__)); } while (0)
#define log_info( fmt, ...) do { if (core_log_is_enabled(core_log_level::info )) core_log(core_log_level::info,  std::format(fmt __VA_OPT__(,) __VA_ARGS__)); } while (0)
#define log_warn( fmt, ...) do { if (core_log_is_enabled(core_log_level::warn )) core_log(core_log_level::warn,  std::format(fmt __VA_OPT__(,) __VA_ARGS__)); } while (0)
#define log_error(fmt, ...) do { if (core_log_is_enabled(core_log_level::error)) core_log(core_log_level::error, std::format(fmt __VA_OPT__(,) __VA_ARGS__)); } while (0)
