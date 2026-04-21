#pragma once

#include "types.hpp"

struct CommandParser
{
    std::span<const std::string_view> args;
    u32 index;

    operator bool() const { return index < args.size(); }

    auto match(std::string_view arg) -> bool
    {
        if (index < args.size() && args[index] == arg) {
            index++;
            return true;
        }
        return false;
    }

    auto peek_rest() -> std::span<const std::string_view> { return args.subspan(index); }

    auto peek()       -> std::string_view{ return index < args.size() ? args[index]   : std::string_view{}; }
    auto get_string() -> std::string_view { return index < args.size() ? args[index++] : std::string_view{}; }

    template<typename T>
    auto get_from_chars() -> std::optional<T>
    {
        if (index >= args.size()) return std::nullopt;

        T value;
        auto res = std::from_chars(args[index].begin(), args[index].end(), value);
        if (!res) return std::nullopt;

        index++;

        return value;
    }

    auto get_int()    -> std::optional<int> { return get_from_chars<int>(); }
    auto get_double() -> std::optional<int> { return get_from_chars<f64>(); }
};
