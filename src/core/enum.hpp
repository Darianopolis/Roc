#pragma once

#include "types.hpp"

template<typename E>
    requires std::is_enum_v<E>
struct std::formatter<E> {
    constexpr auto parse(auto& ctx) { return ctx.begin(); }
    constexpr auto format(E v, auto& ctx) const
    {
        return std::format_to(ctx.out(), "{}", magic_enum::enum_name(v));
    }
};

// -----------------------------------------------------------------------------

template<typename E>
    requires std::is_enum_v<E>
struct Flags
{
    using underlying_type = std::underlying_type_t<E>;

    underlying_type value;

    constexpr Flags() = default;
    constexpr Flags(E e): value(std::to_underlying(e)) {}
    explicit constexpr Flags(E e, auto... rest) : value((e || ... || std::to_underlying(rest))) {}

#define BINARY_OP(Name, Op, ...) \
    friend constexpr Flags operator Name(Flags a, Flags b) { return {E(a.value Op __VA_ARGS__ b.value)}; } \
    constexpr auto operator Name##=(Flags other) -> Flags& { value Op##= __VA_ARGS__ other.value; return *this; }

    BINARY_OP(|, |)
    BINARY_OP(&, &)
    BINARY_OP(-, &, ~)

#undef BINARY_OP

    constexpr auto      get()          const noexcept -> E    { return E(value); }
    constexpr auto contains(Flags set) const noexcept -> bool { return (value & set.value) == set.value; }
    constexpr auto    empty()          const noexcept -> bool { return !value;   }
    explicit operator bool()           const noexcept         { return !empty(); }

    friend constexpr auto operator==(Flags a, Flags b) -> bool = default;
};

template<typename E>
struct std::hash<Flags<E>>
{
    auto operator()(const Flags<E>& v) -> usz
    {
        return std::hash<typename Flags<E>::underlying_type>{}(v.value);
    }
};

template<typename E>
    requires std::is_scoped_enum_v<E>
constexpr Flags<E> operator|(E a, E b)
{
    return Flags(a) | b;
}

template<typename E>
    requires std::is_enum_v<E>
struct std::formatter<Flags<E>> {
    constexpr auto parse(auto& ctx) { return ctx.begin(); }
    constexpr auto format(Flags<E> bitfield, auto& ctx) const
    {
        using Type = Flags<E>::underlying_type;
        // TODO: This breaks with enum flags with higher bits using magic_enum, switch to reflection
        Type v = bitfield.value;
        bool first = true;
        auto out = ctx.out();
        while (v) {
            Type lsb = Type(1) << std::countr_zero(v);
            if (!std::exchange(first, false)) out = std::format_to(out, "|");
            first = false;
            out = std::format_to(out, "{}", magic_enum::enum_name(E(lsb)));
            v &= ~lsb;
        }
        return out;
    }
};
