#pragma once

#include "types.hpp"

namespace core
{
    struct ByteView
    {
        const void* data;
        usz         size;
    };

    inline
    auto view_bytes(auto&& object) -> core::ByteView
    {
        return { &object, sizeof(object) };
    }

// -----------------------------------------------------------------------------

    struct FmtBytes
    {
        u64 bytes;

        FmtBytes() = default;
        FmtBytes(u64 size): bytes(size) {}
    };

    auto to_string(core::FmtBytes size) -> std::string;

// -----------------------------------------------------------------------------

    template<typename T>
    T* byte_offset_pointer(void* source, isz offset)
    {
        return reinterpret_cast<T*>(reinterpret_cast<byte*>(source) + offset);
    }

// -----------------------------------------------------------------------------

    inline
    usz compute_geometric_growth(usz current_size, usz new_min_size)
    {
        usz geometric = current_size + (current_size >> 1);
        return std::max(geometric, new_min_size);
    }

// -----------------------------------------------------------------------------

    template<typename T>
    constexpr
    T align_up_power2(T v, u64 align) noexcept
    {
        return T((u64(v) + (align - 1)) &~ (align - 1));
    }
}
