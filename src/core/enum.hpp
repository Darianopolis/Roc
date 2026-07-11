#pragma once

#include "types.hpp"

#if __cpp_lib_reflection
template<typename E>
    requires (std::meta::is_enumerable_type(^^E))
constexpr
auto enum_values() -> std::span<const E>
{
    return std::define_static_array(std::meta::enumerators_of(^^E)
        | std::views::transform([](std::meta::info e) {
            return std::meta::extract<E>(e);
        }));
}

template<typename E>
    requires (std::meta::is_enumerable_type(^^E))
constexpr
auto enum_index(E value) -> std::optional<usz>
{
    usz i = 0;
    template for (constexpr auto e : std::define_static_array(std::meta::enumerators_of(^^E))) {
        if (value == [:e:]) return i;
        i++;
    }
    return std::nullopt;
}

template<typename E, bool Enumerable = std::meta::is_enumerable_type(^^E)>
    requires std::is_enum_v<E>
constexpr
auto enum_name(E value) -> std::string_view
{
    if constexpr (Enumerable) {
        template for (constexpr auto e : std::define_static_array(std::meta::enumerators_of(^^E))) {
            if (value == [:e:]) return std::meta::identifier_of(e);
        }
    }
  return "";
}
#else
template<typename E>
constexpr
auto enum_values() -> std::span<const E>
{
    static constexpr E e {};
    return {&e, 1};
}

template<typename E>
constexpr
auto enum_index(E e) -> std::optional<usz>
{
    return std::nullopt;
}

template<typename E>
constexpr
auto enum_name(E e) -> std::string_view
{
    return "";
}
#endif

template<typename E>
    requires std::is_enum_v<E>
struct std::formatter<E> {
    constexpr auto parse(auto& ctx) { return ctx.begin(); }
    constexpr auto format(E v, auto& ctx) const
    {
        return std::format_to(ctx.out(), "{}", enum_name(v));
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
struct std::formatter<Flags<E>>
{
    constexpr auto parse(auto& ctx) { return ctx.begin(); }
    constexpr auto format(Flags<E> bitfield, auto& ctx) const
    {
        bool first = true;
        auto out = ctx.out();
        auto append = [&]<typename ...Args>(std::format_string<Args...> fmt, Args&&... args) {
            if (!std::exchange(first, false)) out = std::format_to(out, "|");
            out = std::format_to(out, fmt, std::forward<Args>(args)...);
        };

#if __cpp_lib_reflection
        template for (constexpr auto n : std::define_static_array(std::meta::enumerators_of(^^E))) {
            if (bitfield.contains([:n:])) {
                append("{}", std::string_view(std::meta::identifier_of(n)));
                bitfield -= [:n:];
            }
        }
#endif
        if (bitfield) {
            append("<{:#b}>", std::to_underlying(bitfield.get()));
        }
        return out;
    }
};
