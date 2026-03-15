#include "object.hpp"
#include "math.hpp"
#include "memory.hpp"
#include "debug.hpp"

#define CORE_REGISTRY_PROTECT_FREE 1
#define CORE_REGISTRY_DONT_FREE    0

#if CORE_REGISTRY_DONT_FREE
static_assert(CORE_REGISTRY_PROTECT_FREE);
#endif

// -----------------------------------------------------------------------------

namespace
{
    struct Registry
    {
        std::array<std::vector<core::Allocation*>, 64> bins;
        core::RegistryStats stats;

    #if CORE_REGISTRY_DONT_FREE
        struct {
            std::vector<core::Allocation*> freed;
        } debug;
    #endif

        ~Registry();
    };

    struct Registry registry;
}

// -----------------------------------------------------------------------------

Registry::~Registry()
{
    if (stats.active_allocations) {
        log_error("Registry found {} remaining active allocations", stats.active_allocations);
    }

    usz total_allocation_size = 0;

    for (auto[i, bin] : bins | std::views::enumerate) {
        total_allocation_size += bin.size() * (usz(1) << i);
        if (!bin.empty()) {
            log_debug("Registry cleaning up {} allocations from bin size: {}", bin.size(), 1 << i);
        }
        for (auto* header : bin) {
            ::free(header);
        }
    }

#if CORE_REGISTRY_DONT_FREE
    for (auto* header : debug.freed) {
        ::free(header);
    }
#endif

    log_debug("Peak registry allocation: {}", core::to_string(core::FmtBytes(total_allocation_size)));
}

auto core::registry::get_stats() -> core::RegistryStats
{
    return ::registry.stats;
}

// -----------------------------------------------------------------------------

auto core::registry::allocate(u8 bin_idx) -> core::Allocation*
{
    auto size = 1 << bin_idx;
    auto& bin = ::registry.bins[bin_idx];

    ::registry.stats.active_allocations++;

    core::Allocation* header;
    if (bin.empty()) {
        header = static_cast<core::Allocation*>(core::check<malloc>(size).value);
        new (header) core::Allocation { };
        header->version = 1;
    } else {
        header = bin.back();
        bin.pop_back();
        ::registry.stats.inactive_allocations--;
    }

    header->ref_count = 1;

    return header;
}

void core::registry::free(core::Allocation* header, u8 bin)
{
    ::registry.stats.active_allocations--;
    ::registry.stats.inactive_allocations++;

    header->version++;

#if CORE_REGISTRY_PROTECT_FREE
    header->free = nullptr;
    auto size = (1 << bin) - sizeof(core::Allocation);
    ::memset(core::allocation::get_data(header), 0xDD, size);
#endif

#if CORE_REGISTRY_DONT_FREE
    registry.debug.freed.emplace_back(header);
#else
    ::registry.bins[bin].emplace_back(header);
#endif
}
