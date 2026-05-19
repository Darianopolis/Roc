#include "object.hpp"

// -----------------------------------------------------------------------------

struct Registry
{
    void** data;
    usz* size;
    u64* version;
    u32* ref_count;
    AllocationFree* free;

    std::flat_map<void*, u32, std::greater<void*>> lookup;

    std::vector<u32> freelist;
    u32 last_index = 0;

    u64 last_version = 0;
};

static struct Registry* registry;

static
auto get(auto* field, u32 index) -> decltype(auto)
{
    return field[index];
}

static
auto get(auto* field, Allocation alloc) -> decltype(auto)
{
    return get(field, alloc.index);
}

void registry_init()
{
    registry = new Registry {};

    registry->data = new void*[allocation_max_count];
    registry->size = new usz[allocation_max_count];
    registry->version = new usz[allocation_max_count];
    registry->ref_count = new u32[allocation_max_count];
    registry->free = new AllocationFree[allocation_max_count];

    get(registry->data,      0) = nullptr;
    get(registry->size,      0) = 0;
    get(registry->version,   0) = 0;
    get(registry->ref_count, 0) = 0;
    get(registry->free,      0) = nullptr;
}

void registry_deinit()
{
    delete[] registry->data;
    delete[] registry->size;
    delete[] registry->version;
    delete[] registry->ref_count;
    delete[] registry->free;

    delete registry;
}

// -----------------------------------------------------------------------------

auto registry_allocate(usz size, AllocationFree free) -> Allocation
{
    u32 index;
    if (registry->freelist.empty()) {
        index = ++registry->last_index;
    } else {
        index = registry->freelist.back();
        registry->freelist.pop_back();
    }

    auto data = unix_check<malloc>(size).value;
    get(registry->data, index) = data;
    get(registry->size, index) = size;
    get(registry->version, index) = ++registry->last_version;
    get(registry->ref_count, index) = 1;
    get(registry->free, index) = free;

    registry->lookup.emplace(data, index);

    return {index};
}

static
void registry_free(Allocation alloc)
{
    debug_assert(alloc);
    debug_assert(get(registry->version, alloc));
    debug_assert(get(registry->ref_count, alloc) == 0);

    get(registry->free, alloc)(alloc);
    get(registry->version, alloc) = 0;
    free(get(registry->data, alloc));

    registry->lookup.erase(get(registry->data, alloc));

    registry->freelist.emplace_back(alloc.index);
}

// -----------------------------------------------------------------------------

auto allocation_get_version(Allocation alloc) -> AllocationVersion
{
    return get(registry->version, alloc);
}

auto allocation_ref(Allocation alloc) -> u32
{
    if (!alloc) return 0;
    debug_assert(get(registry->version, alloc));
    return ++get(registry->ref_count, alloc);
}

auto allocation_unref(Allocation alloc) -> u32
{
    if (!alloc) return 0;
    debug_assert(get(registry->version, alloc));
    if (!--get(registry->ref_count, alloc)) {
        registry_free(alloc);
        return 0;
    }
    return get(registry->ref_count, alloc);
}

auto allocation_get_ref_count(Allocation alloc) -> u32&
{
    return get(registry->ref_count, alloc);
}

auto allocation_get_data(Allocation alloc) -> void*
{
    return get(registry->data, alloc);
}

auto allocation_from(void* data) -> Allocation
{
    if (!data) return {};

    // std::flat_map::lower_bound returns the first entry that is `>= data`
    // We reverse this by using `std::greater` in place of `std::less`
    // So this gives us the last entry that is `<= data`
    auto iter = registry->lookup.lower_bound(data);
    if (iter == registry->lookup.end()) return {};

    Allocation alloc = {iter->second};

    // We then bounds check to see if this pointer is contained within the specified allocation
    uintptr_t lower = uintptr_t(get(registry->data, alloc));
    uintptr_t upper = lower + get(registry->size, alloc);
    if (upper <= uintptr_t(data)) return {};

    debug_assert(get(registry->version, alloc));

    return alloc;
}

#undef get
