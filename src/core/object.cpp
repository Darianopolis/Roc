#include "object.hpp"

#include "memory.hpp"
#include "chrono.hpp"
#include "log.hpp"

// -----------------------------------------------------------------------------

static
auto get(auto&& field, u32 index) -> decltype(auto)
{
    return field[index];
}

static
auto get(auto&& field, Allocation alloc) -> decltype(auto)
{
    return get(field, alloc.index);
}

#define GET(Field, Index) get(registry->Field, (Index))

static Allocator* registry;

Allocator::Allocator()
{
    debug_assert(!registry);

    slot_count = 1 << 20;

    data      = memory_map<            void*>(slot_count);
    size      = memory_map<              usz>(slot_count);
    version   = memory_map<AllocationVersion>(slot_count);
    ref_count = memory_map<              u32>(slot_count);
    free      = memory_map<   AllocationFree>(slot_count);

    registry = this;
}

Allocator::~Allocator()
{
    debug_assert(registry == this);

    memory_unmap(data,      slot_count);
    memory_unmap(size,      slot_count);
    memory_unmap(version,   slot_count);
    memory_unmap(ref_count, slot_count);
    memory_unmap(free,      slot_count);

    registry = nullptr;
}

// -----------------------------------------------------------------------------

auto allocation_new(usz size, AllocationFree free) -> Allocation
{
    u32 index;
    if (registry->freelist.empty()) {
        index = ++registry->last_index;
    } else {
        index = registry->freelist.back();
        registry->freelist.pop_back();
    }

    auto data = unix_check<malloc>(size).value;
    GET(data, index) = data;
    GET(size, index) = size;
    GET(version, index) = ++registry->last_version;
    GET(ref_count, index) = 1;
    GET(free, index) = free;

    registry->lookup.emplace(data, index);

    return {index};
}

static
void allocation_free(Allocation alloc)
{
    debug_assert(alloc);
    debug_assert(GET(version, alloc));
    debug_assert(GET(ref_count, alloc) == 0);

    GET(free, alloc)(alloc);
    GET(version, alloc) = 0;
    free(GET(data, alloc));

    registry->lookup.erase(GET(data, alloc));

    registry->freelist.emplace_back(alloc.index);
}

// -----------------------------------------------------------------------------

auto allocation_get_version(Allocation alloc) -> AllocationVersion
{
    return GET(version, alloc);
}

auto allocation_ref(Allocation alloc) -> u32
{
    if (!alloc) return 0;

    debug_assert(GET(version, alloc));
    return ++GET(ref_count, alloc);
}

auto allocation_unref(Allocation alloc) -> u32
{
    if (!alloc) return 0;

    debug_assert(GET(version, alloc));
    if (!--GET(ref_count, alloc)) {
        allocation_free(alloc);
        return 0;
    }
    return GET(ref_count, alloc);
}

auto allocation_get_ref_count(Allocation alloc) -> u32&
{
    return GET(ref_count, alloc);
}

auto allocation_get_data(Allocation alloc) -> void*
{
    return GET(data, alloc);
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
    uintptr_t lower = uintptr_t(GET(data, alloc));
    uintptr_t upper = lower + GET(size, alloc);
    if (upper <= uintptr_t(data)) return {};

    debug_assert(GET(version, alloc));

    return alloc;
}

#undef get
