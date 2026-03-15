#include "memory.hpp"
#include "string.hpp"

std::string core::to_string(core::FmtBytes size)
{
    auto bytes = size.bytes;

#define CORE_BYTE_SIZE_FORMAT_CASE(Size, Suffix) \
    if (bytes >= (Size)) { \
        f64 in_size = f64(bytes) / (Size); \
        return core::format_with_suffix(Suffix, in_size, core::decimals_for_3sf(in_size)); \
    }

    CORE_BYTE_SIZE_FORMAT_CASE(1ul << 60, "EiB");
    CORE_BYTE_SIZE_FORMAT_CASE(1ul << 50, "PiB");
    CORE_BYTE_SIZE_FORMAT_CASE(1ul << 40, "TiB");
    CORE_BYTE_SIZE_FORMAT_CASE(1ul << 30, "GiB");
    CORE_BYTE_SIZE_FORMAT_CASE(1ul << 20, "MiB");
    CORE_BYTE_SIZE_FORMAT_CASE(1ul << 10, "KiB");

    return std::format("{} byte{}", bytes, bytes == 1 ? "" : "s");
}
