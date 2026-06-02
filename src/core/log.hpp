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

void log_set_structured_log(const std::filesystem::path&);
void log_redirect_stdout(   const std::filesystem::path&);
void log_redirect_stderr(   const std::filesystem::path&);

void log(LogSemantic, std::string_view message);

template<typename ...Args>
void log_trace(std::format_string<Args...> fmt, Args&&... args)
{
    log(LogSemantic::trace, std::format(fmt, std::forward<Args>(args)...));
}

template<typename ...Args>
void log_debug(std::format_string<Args...> fmt, Args&&... args)
{
    log(LogSemantic::debug, std::format(fmt, std::forward<Args>(args)...));
}

template<typename ...Args>
void log_info(std::format_string<Args...> fmt, Args&&... args)
{
    log(LogSemantic::info, std::format(fmt, std::forward<Args>(args)...));
}

template<typename ...Args>
void log_warn(std::format_string<Args...> fmt, Args&&... args)
{
    log(LogSemantic::warn, std::format(fmt, std::forward<Args>(args)...));
}

template<typename ...Args>
void log_error(std::format_string<Args...> fmt, Args&&... args)
{
    log(LogSemantic::error, std::format(fmt, std::forward<Args>(args)...));
}
