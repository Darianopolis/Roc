#include "object.hpp"
#include "math.hpp"
#include "memory.hpp"
#include "debug.hpp"

#define REGISTRY_PROTECT_FREE 1
#define REGISTRY_DONT_FREE    0

#if REGISTRY_DONT_FREE
static_assert(REGISTRY_PROTECT_FREE);
#endif

// -----------------------------------------------------------------------------

struct Registry
{
    std::array<std::vector<Allocation*>, 64> bins;
    RegistryStats stats;

#if REGISTRY_DONT_FREE
    struct {
        std::vector<Allocation*> freed;
    } debug;
#endif

    ~Registry();
};

static struct Registry registry;

// -----------------------------------------------------------------------------

Registry::~Registry()
{
    // TODO: The destruction order of different systems is a mess here
    log_history_enable(false);

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

#if REGISTRY_DONT_FREE
    for (auto* header : debug.freed) {
        ::free(header);
    }
#endif

    log_debug("Peak registry allocation: {}", FmtBytes(total_allocation_size));
}

auto registry_get_stats() -> RegistryStats
{
    return registry.stats;
}

// -----------------------------------------------------------------------------

auto registry_allocate(u8 bin_idx) -> Allocation*
{
    auto size = 1 << bin_idx;
    auto& bin = registry.bins[bin_idx];

    registry.stats.active_allocations++;

    Allocation* header;
    if (bin.empty()) {
        header = static_cast<Allocation*>(unix_check<malloc>(size).value);
        new (header) Allocation { };
        header->version = 1;
    } else {
        header = bin.back();
        bin.pop_back();
        registry.stats.inactive_allocations--;
    }

    header->ref_count = 1;

    return header;
}

void registry_free(Allocation* header, u8 bin)
{
    registry.stats.active_allocations--;
    registry.stats.inactive_allocations++;

    header->version++;

#if REGISTRY_PROTECT_FREE
    header->free = nullptr;
    auto size = (1 << bin) - sizeof(Allocation);
    ::memset(allocation_get_data(header), 0xDD, size);
#endif

#if REGISTRY_DONT_FREE
    registry.debug.freed.emplace_back(header);
#else
    registry.bins[bin].emplace_back(header);
#endif
}
