#pragma once

#include "types.hpp"
#include "log.hpp"
#include "util.hpp"
#include "debug.hpp"

// -----------------------------------------------------------------------------

namespace core
{
    using AllocationVersion = u32;

    struct alignas(16) Allocation
    {
        void (*free)(core::Allocation*);
        core::AllocationVersion version;
        u32 ref_count;
    };

    namespace allocation
    {
        inline
        auto from(const void* v) -> core::Allocation*
        {
            // `const_cast` is safe as the `core::Allocation` is always mutable
            return static_cast<core::Allocation*>(const_cast<void*>(v)) - 1;
        }

        inline
        void* get_data(core::Allocation* header)
        {
            return header + 1;
        }
    }
}

// -----------------------------------------------------------------------------

namespace core
{
    struct RegistryStats
    {
        u32 active_allocations;
        u32 inactive_allocations;
    };

    namespace registry
    {
        auto get_stats() -> core::RegistryStats;

        auto allocate(u8 bin) -> core::Allocation*;
        void free(core::Allocation*, u8 bin);

        constexpr
        u8   get_bin_index(usz size)
        {
            return std::countr_zero(core::round_up_power2(size + sizeof(core::Allocation)));
        }
    }
}

// -----------------------------------------------------------------------------

namespace core
{
    template<typename T>
    T* create_uninitialized()
    {
        static constexpr auto bin = core::registry::get_bin_index(sizeof(T));
        auto header = core::registry::allocate(bin);
        header->free = [](core::Allocation* header) {
            if constexpr (!std::is_trivially_destructible_v<T>) {
                static_cast<T*>(core::allocation::get_data(header))->~T();
            }
            core::registry::free(header, bin);
        };
        return static_cast<T*>(core::allocation::get_data(header));
    }

    template<typename T>
    T* create_unsafe(auto&&... args)
    {
        return new (core::create_uninitialized<T>()) T(std::forward<decltype(args)>(args)...);
    }

    inline
    void destroy(void* v)
    {
        auto header = core::allocation::from(v);
        header->free(header);
    }
}

// -----------------------------------------------------------------------------

namespace core
{
    template<typename T>
    T* add_ref(T* t)
    {
        if (t) core::allocation::from(t)->ref_count++;
        return t;
    }

    template<typename T>
    T* remove_ref(T* t)
    {
        if (!t) return nullptr;
        auto header = core::allocation::from(t);
        if (!--header->ref_count) {
            header->free(header);
            return nullptr;
        }
        return t;
    }
}

// -----------------------------------------------------------------------------

namespace core
{
    struct RefAdoptTag {};

    template<typename T>
    struct Ref
    {
        T* value;

        // Destruction

        ~Ref()
        {
            core::remove_ref(value);
        }

        void destroy()
        {
            if (value) {
                core_assert(core::allocation::from(value)->ref_count == 1);
                reset();
            }
        }

        // Construction + Assignment

        Ref() = default;

        Ref(T* t)
            : value(t)
        {
            core::add_ref(value);
        }

        Ref(T* t, core::RefAdoptTag)
            : value(t)
        {}

        void reset(T* t = nullptr)
        {
            if (t == value) return;
            core::remove_ref(value);
            value = core::add_ref(t);
        }

        Ref& operator=(T* t)
        {
            reset(t);
            return *this;
        }

        Ref(const Ref& other)
            : value(core::add_ref(other.value))
        {}

        Ref& operator=(const Ref& other)
        {
            if (value != other.value) {
                core::remove_ref(value);
                value = core::add_ref(other.value);
            }
            return *this;
        }

        Ref(Ref&& other)
            : value(std::exchange(other.value, nullptr))
        {}

        Ref& operator=(Ref&& other)
        {
            if (value != other.value) {
                core::remove_ref(value);
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
        operator core::Ref<T2>() { return core::Ref<T2>(value); }
    };

    template<typename T>
    core::Ref<T> adopt_ref(T* t)
    {
        return {t, core::RefAdoptTag{}};
    }

    template<typename T>
    core::Ref<T> create(auto&&... args)
    {
        return core::adopt_ref(core::create_unsafe<T>(std::forward<decltype(args)>(args)...));
    }
}

// -----------------------------------------------------------------------------

namespace core
{
    template<typename T>
    struct Weak
    {
        T* value;
        core::AllocationVersion version;

        // Construction + Assignment

        Weak() = default;

        Weak(T* t)
        {
            reset(t);
        }

        void reset(T* t = nullptr)
        {
            value = t;
            if (value) {
                version = core::allocation::from(value)->version;
            }
        }

        Weak& operator=(T* t)
        {
            reset(t);
            return *this;
        }

        // Queries

        constexpr bool operator==(const core::Weak<T>& other) { return get() == other.get(); }
        constexpr bool operator!=(const core::Weak<T>& other) { return get() != other.get(); }

        explicit operator bool() const { return value && core::allocation::from(value)->version == version; }
        T*        get() const { return *this ? value : nullptr; }
        T* operator->() const { return value; }

        // Conversions

        template<typename T2>
        operator core::Weak<T2>() { return core::Weak<T2>{get()}; }
    };

    template<typename T>
    bool weak_container_contains(const auto& haystack, T* needle)
    {
        return std::ranges::contains(haystack, needle, &core::Weak<T>::get);
    }
}

// -----------------------------------------------------------------------------

namespace core
{
    template<typename T>
    struct ObjectEquals
    {
        T* value;

        constexpr bool operator()(const auto& c) const noexcept {
            return c.get() == value;
        }
    };
}

// -----------------------------------------------------------------------------

namespace core
{
    template<typename T>
    class RefVector
    {
        std::vector<T*> values;

        using iterator = decltype(values)::iterator;

    public:
        auto* push_back(T* value)
        {
            return values.emplace_back(core::add_ref(value));
        }

        template<typename Fn>
        usz erase_if(Fn&& fn)
        {
            return std::erase_if(values, [&](auto* c) {
                if (fn(c)) {
                    core::remove_ref(c);
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
            core::add_ref(v);
            return values.insert(i, v);
        }

        auto begin(this auto&& self) { return self.values.begin(); }
        auto   end(this auto&& self) { return self.values.end();   }

        ~RefVector()
        {
            for (auto* v : values) core::remove_ref(v);
        }
    };
}
