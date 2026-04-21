#pragma once

#include "types.hpp"
#include "debug.hpp"

constexpr
auto color_from_hex(std::string_view str) -> vec4u8
{
    vec4u8 color;
    if (str.starts_with("#")) str.remove_prefix(1);

    static constexpr
    auto hex_to_value = [](char digit) -> u8 {
        switch (digit) {
            break;case 'a' ... 'f': return 10 + digit - 'a';
            break;case 'A' ... 'F': return 10 + digit - 'A';
            break;case '0' ... '9': return digit - '0';
            break;default:
                debug_unreachable();
        }
    };

    auto hex_pair_to_value = [&](u32 i) -> u8 {
        return hex_to_value(str[i]) * 16 + hex_to_value(str[i + 1]);
    };

    debug_assert(str.size() >= 6);

    color.x = hex_pair_to_value(0);
    color.y = hex_pair_to_value(2);
    color.z = hex_pair_to_value(4);
    color.w = str.size() >= 8 ? hex_pair_to_value(6) : 255;

    return color;
}
