#pragma once

#include "types.hpp"

namespace core
{
    struct CommandParser
    {
        std::span<const std::string_view> args;
        u32 index;

        operator bool() const { return index < args.size(); }

        bool match(std::string_view arg)
        {
            if (index < args.size() && args[index] == arg) {
                index++;
                return true;
            }
            return false;
        }

        std::span<const std::string_view> peek_rest() { return args.subspan(index); }

        std::string_view peek()       { return index < args.size() ? args[index]   : std::string_view{}; }
        std::string_view get_string() { return index < args.size() ? args[index++] : std::string_view{}; }

        template<typename T>
        std::optional<T> get_from_chars()
        {
            if (index >= args.size()) return std::nullopt;

            T value;
            auto res = std::from_chars(args[index].begin(), args[index].end(), value);
            if (!res) return std::nullopt;

            index++;

            return value;
        }

        std::optional<int> get_int()    { return get_from_chars<int>(); }
        std::optional<int> get_double() { return get_from_chars<f64>(); }
    };
}
