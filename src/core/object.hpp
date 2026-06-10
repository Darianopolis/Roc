#pragma once

#include "types.hpp"
#include "util.hpp"
#include "debug.hpp"
#include "hash.hpp"

// -----------------------------------------------------------------------------

static constexpr u32 allocation_max_count = 65'536;

using AllocationVersion = u32;

struct Allocation
{
    u32 index;

    [[nodiscard]] explicit constexpr operator bool() const noexcept
    {
        return index;
    }
};

using AllocationFree = void(*)(Allocation);

auto allocation_get_version(Allocation) -> AllocationVersion;

auto allocation_ref(Allocation) -> u32;
auto allocation_unref(Allocation) -> u32;

auto allocation_get_data(Allocation) -> void*;
auto allocation_from(void*) -> Allocation;

// -----------------------------------------------------------------------------

auto registry_allocate(usz size, AllocationFree free) -> Allocation;

// -----------------------------------------------------------------------------

template<typename T>
void object_free(Allocation alloc)
{
    if constexpr (!std::is_trivially_destructible_v<T>) {
        static_cast<T*>(allocation_get_data(alloc))->~T();
    }
}

template<typename T>
auto object_create_uninitialized() -> T*
{
    return static_cast<T*>(allocation_get_data(registry_allocate(sizeof(T), object_free<T>)));
}

template<typename T>
auto object_create_unsafe(auto&&... args) -> T*
{
    return new (object_create_uninitialized<T>()) T(std::forward<decltype(args)>(args)...);
}

inline
void object_destroy(void* v)
{
    debug_assert(!allocation_unref(allocation_from(v)));
}

// -----------------------------------------------------------------------------

template<typename T>
auto object_ref(T* t) -> T*
{
    return allocation_ref(allocation_from(t)) ? t : nullptr;
}

template<typename T>
auto object_unref(T* t) -> T*
{
    return allocation_unref(allocation_from(t)) ? t : nullptr;
}

// -----------------------------------------------------------------------------

template<typename T>
struct Ref;

template<typename T>
struct Weak;

// -----------------------------------------------------------------------------

struct RefAdoptTag {};

template<typename T>
struct Ref
{
    T* value;

    void reset(T* t = nullptr)
    {
        if (t == value) return;
        object_unref(value);
        value = object_ref(t);
    }

    // Destruction

    ~Ref()
    {
        object_unref(value);
    }

    void destroy()
    {
        if (value) {
            object_destroy(value);
            value = nullptr;
        }
    }

    // Construction

    Ref() = default;

    Ref(T* t)
        : value(t)
    {
        object_ref(value);
    }

    Ref(T* t, RefAdoptTag)
        : value(t)
    {}

    // Assignment

    Ref(const Ref& other)
        : value(object_ref(other.value))
    {}

    auto& operator=(const Ref& other)
    {
        reset(other.value);
        return *this;
    }

    Ref(Ref&& other)
        : value(std::exchange(other.value, nullptr))
    {}

    auto& operator=(Ref&& other)
    {
        if (value != other.value) {
            object_unref(value);
            value = std::exchange(other.value, nullptr);
        }
        return *this;
    }

    // Queries

    template<typename T2>
    auto operator==(const Ref<T2>& other) const -> bool { return value == other.value; };

    explicit operator bool() const       { return value; }
    auto               get() const -> T* { return value; }
    auto        operator->() const -> T* { return value; }

    // Conversions

    template<typename T2>
    explicit Ref(Weak<T2> other): Ref(other.get()) {}
    explicit Ref(Weak<T>  other): Ref(other.get()) {}

    template<typename T2>
    Ref(Ref<T2> other): Ref(other.value) {}
};

template<typename T>
auto ref_adopt(T* t) -> Ref<T>
{
    return {t, RefAdoptTag{}};
}

template<typename T>
auto ref_create(auto&&... args) -> Ref<T>
{
    return ref_adopt(object_create_unsafe<T>(std::forward<decltype(args)>(args)...));
}

// -----------------------------------------------------------------------------

template<typename T>
struct Weak
{
    T* value;
    AllocationVersion version;

    // Construction

    Weak() = default;

    Weak(T* t)
        : value(t)
        , version(allocation_get_version(allocation_from(value)))
    {}

    // Queries

    auto operator==(const Weak& other) const -> bool = default;

    template<typename T2>
    auto operator==(const Weak<T2>& other) const -> bool { return value == other.value && version == other.version; };

    explicit operator bool() const       { return value && allocation_get_version(allocation_from(value)) == version; }
    auto               get() const -> T* { return *this ? value : nullptr; }
    auto        operator->() const -> T* { return value; }

    // Conversions

    template<typename T2>
    Weak(Ref<T2> other): Weak(other.get()) {}
    Weak(Ref<T>  other): Weak(other.get()) {}

    template<typename T2>
    Weak(Weak<T2> other)
        : value(other.value)
        , version(other.version)
    {}
};

template<typename T>
struct std::hash<Weak<T>>
{
    auto operator()(const Weak<T>& v) -> usz { return hash_variadic(v.value, v.version); }
};
