#pragma once

#include "types.hpp"
#include "debug.hpp"

namespace core
{
    constexpr
    u8 hex_to_value(char digit)
    {
        switch (digit) {
            break;case 'a' ... 'f': return 10 + digit - 'a';
            break;case 'A' ... 'F': return 10 + digit - 'A';
            break;case '0' ... '9': return digit - '0';
            break;default:
                core::unreachable();
        }
    }

    constexpr
    vec4u8 color_from_hex(std::string_view str)
    {
        vec4u8 color;
        if (str.starts_with("#")) str.remove_prefix(1);

        auto hex_to_value = [&](u32 i) -> u8 {
            return core::hex_to_value(str[i]) * 16 + core::hex_to_value(str[i + 1]);
        };

        core_assert(str.size() >= 6);

        color.x = hex_to_value(0);
        color.y = hex_to_value(2);
        color.z = hex_to_value(4);
        color.w = str.size() >= 8 ? hex_to_value(6) : 255;

        return color;
    }
}
