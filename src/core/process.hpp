#pragma once

#include "object.hpp"
#include "fd.hpp"

// -----------------------------------------------------------------------------

auto process_has_cap( cap_value_t cap) -> bool;
void process_drop_cap(cap_value_t cap);

// -----------------------------------------------------------------------------

auto env_get(const char* name) -> std::optional<std::string>;
void env_set(const char* name, std::optional<std::string> value);

template<typename T>
auto env_get(const char* name) -> std::optional<T>
{
    return env_get(name).and_then([](const std::string& str) -> std::optional<T> {
        T value;
        auto res = std::from_chars(str.data(), str.data() + str.size(), value);
        if (res.ec == std::errc()) return value;
        return std::nullopt;
    });
}

// -----------------------------------------------------------------------------

struct Environment
{
    ankerl::unordered_dense::map<std::string, std::string> entries;
    Fd dir;

    void load(char** env)
    {
        for (auto e = env; *e; ++e) {
            std::string_view entry{ *e };
            auto sep = entry.find_first_of('=');
            entries.insert_or_assign(std::string(entry.substr(0, sep)), std::string(entry.substr(sep + 1)));
        }
    }

    void set(const std::string& key, std::optional<std::string> value)
    {
        if (value) {
            entries[key] = *value;
        } else {
            entries.erase(key);
        }
    }
};

// -----------------------------------------------------------------------------

inline
auto path_open(const std::filesystem::path& path, int oflags = 0, auto... args) -> Fd
{
    return Fd(unix_check<open>(path.c_str(), oflags | O_CLOEXEC, args...).value);
}

// -----------------------------------------------------------------------------

struct SpawnFdInherit
{
    fd_t parent;
    fd_t child;

    SpawnFdInherit(fd_t fd                ): parent(fd    ), child(fd   ) {}
    SpawnFdInherit(fd_t parent, fd_t child): parent(parent), child(child) {}
};

auto spawn(
    fd_t exe,
    std::span<const std::string_view> args,
    const Environment* env = nullptr,
    std::span<const SpawnFdInherit> fds = {{STDIN_FILENO, STDOUT_FILENO, STDERR_FILENO}}) -> Fd;
