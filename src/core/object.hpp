#pragma once

#include "types.hpp"
#include "log.hpp"
#include "util.hpp"

// -----------------------------------------------------------------------------

/**
 * Common polymorphic helper base for dynamic objects.
 *
 * Note that no registry operations actually depend on deriving from this type.
 * It is merely provided as a convenience.
 */
struct core_object
{
    core_object() = default;

    CORE_DELETE_COPY_MOVE(core_object)

    virtual ~core_object() = default;
};

// -----------------------------------------------------------------------------

using core_allocation_version = u64;

struct alignas(16) core_allocation_header
{
    core_allocation_version version;
    u32 ref_count;
    u8 bin;
};

template<typename T>
core_allocation_header* core_allocation_from(T* t)
{
    // NOTE: Assumes that `t` points to start of allocated data section
    //       This will break for objects with virtual hierarchies, but we don't support them anyway..
    return reinterpret_cast<core_allocation_header*>(t) - 1;
}

inline
void* core_allocation_get_data(core_allocation_header* header)
{
    return header + 1;
}

// -----------------------------------------------------------------------------

struct core_registry
{
    std::array<std::vector<core_allocation_header*>, 64> bins;
    u32 active_allocations;
    u32 inactive_allocations;

    struct {
        std::vector<core_allocation_header*> freed;
    } debug;

    ~core_registry();

    core_allocation_header* allocate(usz size);
    void free(core_allocation_header*);
};

extern struct core_registry core_registry;

// -----------------------------------------------------------------------------

template<typename T>
void core_destruct(T* t)
{
    if constexpr (!std::is_trivially_destructible_v<T>) {
        t->~T();
    }
}

#define CORE_OBJECT_EXPLICIT_DECLARE(Type) \
    template<> void core_destruct(Type* t)

#define CORE_OBJECT_EXPLICIT_DEFINE(Type) \
    template<> void core_destruct(Type* t) \
    { \
        t->~Type(); \
    }

// -----------------------------------------------------------------------------

template<typename T>
T* core_create_uninitialized()
{
    return static_cast<T*>(core_allocation_get_data(core_registry.allocate(sizeof(T))));
}

template<typename T>
T* core_create_unsafe(auto&&... args)
{
    return new (core_create_uninitialized<T>()) T(std::forward<decltype(args)>(args)...);
}

template<typename T>
void core_destroy(T* t)
{
    core_destruct(t);
    core_registry.free(core_allocation_from(t));
}

// -----------------------------------------------------------------------------

template<typename T>
T* core_add_ref(T* t)
{
    if (t) core_allocation_from(t)->ref_count++;
    return t;
}

template<typename T>
T* core_remove_ref(T* t)
{
    if (!t) return nullptr;
    if (!--core_allocation_from(t)->ref_count) {
        core_destroy(t);
        return nullptr;
    }
    return t;
}

// -----------------------------------------------------------------------------

struct core_ref_adopt_tag {};

template<typename T>
struct core_ref
{
    T* value;

    // Destruction

    ~core_ref()
    {
        core_remove_ref(value);
    }

    void destroy()
    {
        if (value) {
            core_assert(core_allocation_from(value)->ref_count == 1);
            reset();
        }
    }

    // Construction + Assignment

    core_ref() = default;

    core_ref(T* t)
        : value(t)
    {
        core_add_ref(value);
    }

    core_ref(T* t, core_ref_adopt_tag)
        : value(t)
    {}

    void reset(T* t = nullptr)
    {
        if (t == value) return;
        core_remove_ref(value);
        value = core_add_ref(t);
    }

    core_ref& operator=(T* t)
    {
        reset(t);
        return *this;
    }

    core_ref(const core_ref& other)
        : value(core_add_ref(other.value))
    {}

    core_ref& operator=(const core_ref& other)
    {
        if (value != other.value) {
            core_remove_ref(value);
            value = core_add_ref(other.value);
        }
        return *this;
    }

    core_ref(core_ref&& other)
        : value(std::exchange(other.value, nullptr))
    {}

    core_ref& operator=(core_ref&& other)
    {
        if (value != other.value) {
            core_remove_ref(value);
            value = std::exchange(other.value, nullptr);
        }
        return *this;
    }

    // Queries

    explicit operator bool() const { return value; }
    T*        get() const { return value; }
    T* operator->() const { return value; }

    // Conversions

    template<typename T2>
    operator core_ref<T2>() { return core_ref<T2>(value); }
};

template<typename T>
core_ref<T> core_adopt_ref(T* t)
{
    return {t, core_ref_adopt_tag{}};
}

template<typename T>
core_ref<T> core_create(auto&&... args)
{
    return core_adopt_ref(core_create_unsafe<T>(std::forward<decltype(args)>(args)...));
}

// -----------------------------------------------------------------------------

template<typename T>
struct core_weak
{
    T* value;
    core_allocation_version version;

    // Construction + Assignment

    core_weak() = default;

    core_weak(T* t)
    {
        reset(t);
    }

    void reset(T* t = nullptr)
    {
        value = t;
        if (value) {
            version = core_allocation_from(value)->version;
        }
    }

    core_weak& operator=(T* t)
    {
        reset(t);
        return *this;
    }

    // Queries

    constexpr bool operator==(const core_weak<T>& other) { return get() == other.get(); }
    constexpr bool operator!=(const core_weak<T>& other) { return get() != other.get(); }

    explicit operator bool() const { return value && core_allocation_from(value)->version == version; }
    T*        get() const { return *this ? value : nullptr; }
    T* operator->() const { return value; }

    // Conversions

    template<typename T2>
    operator core_weak<T2>() { return core_weak<T2>{get()}; }
};

template<typename T>
bool weak_container_contains(const auto& haystack, T* needle)
{
    return std::ranges::contains(haystack, needle, &core_weak<T>::get);
}

// -----------------------------------------------------------------------------

template<typename T>
using ref = core_ref<T>;

template<typename T>
using weak = core_weak<T>;

template<typename T>
struct core_object_equals
{
    T* value;

    constexpr bool operator()(const auto& c) const noexcept {
        return c.get() == value;
    }
};

// -----------------------------------------------------------------------------

template<typename T>
T* core_object_cast(core_object* base)
{
    if (!base) return nullptr;
    auto derived = dynamic_cast<T*>(base);
    core_assert(derived, "Fatal error casting void to object: expected {} got {}", typeid(T).name(), typeid(*base).name());
    return derived;
}

// -----------------------------------------------------------------------------

template<typename T>
class core_ref_vector
{
    std::vector<T*> values;

    using iterator = decltype(values)::iterator;

public:
    auto* push_back(T* value)
    {
        return values.emplace_back(core_add_ref(value));
    }

    template<typename Fn>
    usz erase_if(Fn&& fn)
    {
        return std::erase_if(values, [&](auto* c) {
            if (fn(c)) {
                core_remove_ref(c);
                return true;
            }
            return false;
        });
    }

    usz erase(T* v)
    {
        return erase_if([v](T* c) { return c == v; });
    }

    usz   size()  const { return values.size();  }
    bool  empty() const { return values.empty(); }
    auto& front() const { return values.front(); }
    auto& back()  const { return values.back();  }

    auto insert(iterator i, T* v)
    {
        core_add_ref(v);
        return values.insert(i, v);
    }

    auto begin(this auto&& self) { return self.values.begin(); }
    auto   end(this auto&& self) { return self.values.end();   }

    ~core_ref_vector()
    {
        for (auto* v : values) core_remove_ref(v);
    }
};

// TODO: Add `core_weak_vector`.
//       Will require destructor callback list to erase destroyed elements
