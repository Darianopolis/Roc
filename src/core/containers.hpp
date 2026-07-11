#pragma once

#include "types.hpp"
#include "object.hpp"
#include "enum.hpp"

// -----------------------------------------------------------------------------
//      Enum Map
// -----------------------------------------------------------------------------

template<typename E, typename T>
struct EnumMap
{
    std::array<T, enum_values<E>().size()> _data;

    constexpr auto operator[](E value)       ->       T& { return _data[enum_index(value).value()]; }
    constexpr auto operator[](E value) const -> const T& { return _data[enum_index(value).value()]; }
};

// -----------------------------------------------------------------------------
//      Counting Set
// -----------------------------------------------------------------------------

template<typename T>
struct CountingSet
{
    using value_type = T;

    std::flat_map<T, u32> counts;

    auto inc(auto&& t) -> bool
    {
        return !counts[t]++;
    }

    auto dec(auto&& t) -> bool
    {
        auto iter = counts.find(t);
        debug_assert(iter != counts.end());
        if (!--iter->second) {
            counts.erase(iter);
            return true;
        }
        return false;
    }

    auto begin() const { return counts.keys().begin(); }
    auto   end() const { return counts.keys().end();   }

    auto contains(const T& t) const -> bool { return counts.contains(t); }
    auto size()               const -> usz  { return counts.size();      }
    auto empty()              const -> bool { return counts.empty();     }
};

// -----------------------------------------------------------------------------
//      Fixed Array
// -----------------------------------------------------------------------------

template<typename T, u32 Max>
struct FixedArray {
    std::array<T, Max> data = {};
    u32 count = 0;

    auto begin(this auto&& self) { return self.data.begin();              }
    auto   end(this auto&& self) { return self.data.begin() + self.count; }

    auto& operator[](this auto&& self, usz i) { return self.data[i]; }
};

// -----------------------------------------------------------------------------

template<typename T>
struct Link
{
    Link* prev = this;
    Link* next = this;

    Link() = default;

    void unlink()
    {
        prev->next = next;
        next->prev = prev;
        prev = this;
        next = this;
    }

    ~Link()
    {
        unlink();
    }

    DELETE_COPY(Link)

    Link(Link&& other)
        : prev(std::exchange(other.prev, &other))
        , next(std::exchange(other.next, &other))
    {
        prev->next = this;
        next->prev = this;
    }

    auto operator=(Link&& other) -> Link&
    {
        if (this != &other) {
            unlink();
            prev = std::exchange(other.prev, &other);
            next = std::exchange(other.next, &other);
            prev->next = this;
            next->prev = this;
        }
        return *this;
    }

    void link_prev(Link<T>* other)
    {
        if (other->is_linked()) {
            other->unlink();
        }
        prev->next = other;
        other->prev = prev;
        other->next = this;
        prev = other;
    }

    void link_next(Link<T>* other)
    {
        if (other->is_linked()) {
            other->unlink();
        }
        next->prev = other;
        other->next = next;
        other->prev = this;
        next = other;
    }

    auto is_linked() const -> bool
    {
        return next != this;
    }
};

#define LINK_GET(Type, Member, Ptr) \
    ((Type*)(uintptr_t(static_cast<Link<Type>*>(Ptr)) - offsetof(Type, Member)))

// -----------------------------------------------------------------------------

template<typename T>
class RefVector
{
    std::vector<T*> values;

    using const_iterator = decltype(values)::const_iterator;
    using iterator = decltype(values)::iterator;

public:
    RefVector() {}

public:
    RefVector(const RefVector& other)
        : values(other.values)
    {
        for (auto* value : values) {
            object_ref(value);
        }
    }

    auto& operator=(const RefVector& other)
    {
        if (this != &other) {
            clear();
            values = other.values;
            for (auto* value : values) {
                object_ref(value);
            }
        }
        return *this;
    }

    RefVector(RefVector&& other)
        : values(std::move(other.values))
    {
        other.values.clear();
    }

    auto& operator=(RefVector&& other)
    {
        if (this != &other) {
            clear();
            values = std::move(other.values);
            other.values.clear();
        }
        return *this;
    }

public:
    auto emplace_back(T* value) -> T*
    {
        return values.emplace_back(object_ref(value));
    }

    auto emplace_back(Ref<T>&& value) -> T*
    {
        return values.emplace_back(std::exchange(value.value, nullptr));
    }

    template<typename Fn>
    auto erase_if(Fn&& fn) -> usz
    {
        return std::erase_if(values, [&](auto* c) {
            if (fn(c)) {
                object_unref(c);
                return true;
            }
            return false;
        });
    }

    auto erase(T* v) -> usz
    {
        return erase_if([v](T* c) { return c == v; });
    }

    auto size()  const -> usz  { return values.size();  }
    auto empty() const -> bool { return values.empty(); }
    auto front() const -> T*   { return values.front(); }
    auto back()  const -> T*   { return values.back();  }

    T* operator[](usz index) const
    {
        return values[index];
    }

    void destroy_all()
    {
        for (auto* v : values) object_destroy(v);
        values.clear();
    }

    void clear()
    {
        for (auto* v : values) object_unref(v);
        values.clear();
    }

    auto pop_front() -> Ref<T>
    {
        auto value = ref_adopt(values.front());
        values.pop_front();
        return value;
    }

    auto pop_back() -> Ref<T>
    {
        auto value = ref_adopt(values.back());
        values.pop_back();
        return value;
    }

    auto insert(const_iterator i, T* v)
    {
        object_ref(v);
        return values.insert(i, v);
    }

    auto begin(this auto&& self) { return self.values.begin(); }
    auto   end(this auto&& self) { return self.values.end();   }

    ~RefVector()
    {
        for (auto* v : values) object_unref(v);
    }
};
