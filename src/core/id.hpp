#pragma once

#include "types.hpp"

// -----------------------------------------------------------------------------

template<typename T, typename Tag>
struct TaggedInteger
{
    using underlying_type = T;

    T value;

    constexpr TaggedInteger() = default;

    constexpr explicit TaggedInteger(T value)
        : value(value)
    {}

    constexpr auto operator<=>(const TaggedInteger&) const = default;

    constexpr TaggedInteger operator+(T other) const
    {
        return TaggedInteger{value + other};
    }

    constexpr explicit operator bool()
    {
        return bool(value);
    }

    constexpr auto& operator++()
    {
        value++;
        return *this;
    }
};

#define DECLARE_TAGGED_INTEGER(Name, Type) \
    namespace tag { struct Name {}; } \
    using Name = TaggedInteger<Type, tag::Name>

// -----------------------------------------------------------------------------

DECLARE_TAGGED_INTEGER(Uid, u64);

auto uid_allocate() -> Uid;
