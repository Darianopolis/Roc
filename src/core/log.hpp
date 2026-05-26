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

struct LogInitInfo
{
    std::optional<std::filesystem::path> log_path;
    std::optional<std::filesystem::path> stdout_redirect;
    std::optional<std::filesystem::path> stderr_redirect;
};

void log_init(const LogInitInfo&);
void log_deinit();

void log_set_file(const std::filesystem::path& log_file);
void log(LogSemantic, std::string_view message);

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
