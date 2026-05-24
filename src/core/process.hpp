#pragma once

#include "pch.hpp"

auto process_has_cap( cap_value_t cap) -> bool;
void process_drop_cap(cap_value_t cap);

void spawn_path(std::string_view name, std::span<const std::string_view> args);

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
