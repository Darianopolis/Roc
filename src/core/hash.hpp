#pragma once

#include "types.hpp"

inline
auto hash_mix(u64 x) -> u64
{
    // From boost
    // https://github.com/boostorg/container_hash/blob/060d4aea6b5b59d2c9146b7d8e994735b2c0a582/include/boost/container_hash/detail/hash_mix.hpp#L67-L81

    static constexpr u64 m = (u64(0xe9846af) << 32) + 0x9b1a615d;

    x ^= x >> 32;
    x *= m;
    x ^= x >> 32;
    x *= m;
    x ^= x >> 28;

    return x;
}

template<typename T>
auto hash_single(const T& v) -> usz
{
    return std::hash<T>{}(v);
}

template<typename T>
void hash_combine(usz& seed, const T& v)
{
    // From boost
    // https://github.com/boostorg/container_hash/blob/060d4aea6b5b59d2c9146b7d8e994735b2c0a582/include/boost/container_hash/hash.hpp#L469-L473

    seed = hash_mix(seed + 0x9e3779b9 + hash_single(v));
}

inline
void hash_range(usz& seed, auto start, auto end)
{
    for (; start != end; ++start) {
        hash_combine(seed, *start);
    }
}

inline
auto hash_range(auto start, auto end) -> usz
{
    usz seed = 0;
    hash_range(seed, start, end);
    return seed;
}

inline
auto hash_range(auto&& range) -> usz
{
    usz seed = 0;
    hash_range(seed, std::begin(range), std::end(range));
    return seed;
}

#define MAKE_RANGE_HASHABLE(Type) \
    template<> struct std::hash<Type> { \
        auto operator()(const Type& v) -> usz { return hash_range(v.begin(), v.end()); } \
    };

inline
auto hash_variadic(const auto& first, const auto&... rest) -> usz
{
    usz seed = hash_single(first);
    (hash_combine(seed, rest), ...);
    return seed;
}

#define MAKE_STRUCT_HASHABLE(Type, ...) \
    template<> struct std::hash<Type> { \
        auto operator()(const Type& v) -> usz { return hash_variadic(__VA_ARGS__); } \
    };
