#pragma once

#include "types.hpp"

inline
auto as_bytes(const void* data, usz size) -> std::span<const byte>
{
    return { reinterpret_cast<const byte*>(data), size };
}

inline
auto view_bytes(auto&& object) -> std::span<const byte>
{
    return { reinterpret_cast<const byte*>(&object), sizeof(object) };
}

// -----------------------------------------------------------------------------

struct FmtBytes
{
    u64 bytes;

    FmtBytes() = default;
    FmtBytes(u64 size): bytes(size) {}
};

template<>
struct std::formatter<FmtBytes> {
    constexpr auto parse(auto& ctx) { return ctx.begin(); }
    constexpr auto format(FmtBytes size, auto& ctx) const
    {
        auto bytes = size.bytes;

        auto with_suffix = [&](std::string_view suffix, f64 amount) {
            u32 decimals = amount < 10 ? 2 : (amount < 100 ? 1 : 0);
            return std::format_to(ctx.out(), "{:.{}f}{}", amount, decimals, suffix);
        };

        if (bytes >= (1ul << 60)) return with_suffix("EiB", f64(bytes) / (1ul << 60));
        if (bytes >= (1ul << 50)) return with_suffix("PiB", f64(bytes) / (1ul << 50));
        if (bytes >= (1ul << 40)) return with_suffix("TiB", f64(bytes) / (1ul << 40));
        if (bytes >= (1ul << 30)) return with_suffix("GiB", f64(bytes) / (1ul << 30));
        if (bytes >= (1ul << 20)) return with_suffix("MiB", f64(bytes) / (1ul << 20));
        if (bytes >= (1ul << 10)) return with_suffix("KiB", f64(bytes) / (1ul << 10));

        return std::format_to(ctx.out(), "{} byte{}", bytes, bytes == 1 ? "" : "s");
    }
};

// -----------------------------------------------------------------------------

template<typename T>
auto byte_offset_pointer(void* source, isz offset) -> T*
{
    return reinterpret_cast<T*>(reinterpret_cast<byte*>(source) + offset);
}

// -----------------------------------------------------------------------------

inline
auto compute_geometric_growth(usz current_size, usz new_min_size) -> usz
{
    usz geometric = current_size + (current_size >> 1);
    return std::max(geometric, new_min_size);
}

// -----------------------------------------------------------------------------

template<typename T>
constexpr
auto align_up_power2(T v, u64 align) noexcept -> T
{
    return T((u64(v) + (align - 1)) &~ (align - 1));
}
