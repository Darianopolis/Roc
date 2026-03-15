#pragma once

#include "types.hpp"

namespace core
{
    inline
    u64 hash_mix(u64 x)
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
    usz hash_single(const T& v)
    {
        return std::hash<T>{}(v);
    }

    template<typename T>
    void hash_combine(usz& seed, const T& v)
    {
        // From boost
        // https://github.com/boostorg/container_hash/blob/060d4aea6b5b59d2c9146b7d8e994735b2c0a582/include/boost/container_hash/hash.hpp#L469-L473

        seed = core::hash_mix(seed + 0x9e3779b9 + core::hash_single(v));
    }

    inline
    void hash_range(usz& seed, auto start, auto end)
    {
        for (; start != end; ++start) {
            core::hash_combine(seed, *start);
        }
    }

    inline
    usz hash_range(auto start, auto end)
    {
        usz seed = 0;
        core::hash_range(seed, start, end);
        return seed;
    }

    inline
    usz hash_variadic(const auto& first, const auto&... rest)
    {
        usz seed = core::hash_single(first);
        (core::hash_combine(seed, rest), ...);
        return seed;
    }
}

#define CORE_MAKE_RANGE_HASHABLE(Type) \
    template<> struct std::hash<Type> { \
        usz operator()(const Type& v) { return core::hash_range(v.begin(), v.end()); } \
    };

#define CORE_MAKE_STRUCT_HASHABLE(Type, ...) \
    template<> struct std::hash<Type> { \
        usz operator()(const Type& v) { return core::hash_variadic(__VA_ARGS__); } \
    };
