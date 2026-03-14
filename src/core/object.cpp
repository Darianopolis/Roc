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

core_registry::~core_registry()
{
    if (active_allocations) {
        log_error("Registry found {} remaining active allocations", active_allocations);
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

    log_debug("Peak registry allocation: {}", core_to_string(core_fmt_bytes(total_allocation_size)));
}

auto core_registry::allocate(usz size) -> core_allocation_header*
{
    size = core_round_up_power2(size + sizeof(core_allocation_header));

    active_allocations++;

    u8 bin_idx = std::countr_zero(size);
    auto& bin = bins[bin_idx];

    core_allocation_header* header;
    if (bin.empty()) {
        header = static_cast<core_allocation_header*>(unix_check<malloc>(size).value);
        new (header) core_allocation_header {
            .bin = bin_idx,
        };
        header->version = 1;
    } else {
        header = bin.back();
        bin.pop_back();
        inactive_allocations--;
    }

    header->ref_count = 1;

    return header;
}

void core_registry::free(core_allocation_header* header)
{
    active_allocations--;
    inactive_allocations++;

    header->version++;

#if CORE_REGISTRY_PROTECT_FREE
    auto size = (1 << header->bin) - sizeof(core_allocation_header);
    ::memset(core_allocation_get_data(header), 0xDD, size);
#endif

#if CORE_REGISTRY_DONT_FREE
    debug.freed.emplace_back(header);
#else
    bins[header->bin].emplace_back(header);
#endif
}

struct core_registry core_registry;
