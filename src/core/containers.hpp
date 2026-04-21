#pragma once

#include "types.hpp"
#include "object.hpp"

// -----------------------------------------------------------------------------
//      Enum Map
// -----------------------------------------------------------------------------

template<typename E, typename T>
struct EnumMap
{
    T _data[magic_enum::enum_count<E>()];

    static constexpr auto enum_values = magic_enum::enum_values<E>();

    constexpr auto operator[](E value)       ->       T& { return _data[magic_enum::enum_index(value).value()]; }
    constexpr auto operator[](E value) const -> const T& { return _data[magic_enum::enum_index(value).value()]; }
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
//      Intrusive Linked List
// -----------------------------------------------------------------------------

template<typename Base>
struct IntrusiveListBase
{
    IntrusiveListBase* next = this;
    IntrusiveListBase* prev = this;
};

template<typename Base>
struct IntrusiveListIterator
{
    IntrusiveListBase<Base>* cur;

    void insert_after(IntrusiveListBase<Base>* base)
    {
        base->prev = cur;
        base->next = cur->next;

        cur->next->prev = base;
        cur->next = base;
    }

    auto remove() -> IntrusiveListIterator
    {
        cur->next->prev = cur->prev;
        cur->prev->next = cur->next;

        cur->next = cur;
        cur->prev = cur;

        return *this;
    }

    auto operator->() -> Base* { return get(); }
    auto get() -> Base* { return static_cast<Base*>(cur); }

    auto operator==(const IntrusiveListIterator&) const noexcept -> bool = default;

    auto next() -> IntrusiveListIterator { return {cur->next}; }
    auto prev() -> IntrusiveListIterator { return {cur->prev}; }
};

template<typename Base>
struct IntrusiveList
{
    using Iterator = IntrusiveListIterator<Base>;

    IntrusiveListBase<Base> root;

    auto first() -> Iterator { return {root.next}; }
    auto last()  -> Iterator { return {root.prev}; }
    auto end()   -> Iterator { return {&root};     }

    auto empty() const -> bool { return root.next == &root; }
};

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
            object_add_ref(value);
        }
    }

    auto& operator=(const RefVector& other)
    {
        if (this != &other) {
            clear();
            values = other.values;
            for (auto* value : values) {
                object_add_ref(value);
            }
        }
        return *this;
    }

    RefVector(RefVector&& other)
        : values(std::move(other))
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
        return values.emplace_back(object_add_ref(value));
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
                object_remove_ref(c);
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

    void clear()
    {
        for (auto* v : values) object_remove_ref(v);
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
        object_add_ref(v);
        return values.insert(i, v);
    }

    auto begin(this auto&& self) { return self.values.begin(); }
    auto   end(this auto&& self) { return self.values.end();   }

    ~RefVector()
    {
        for (auto* v : values) object_remove_ref(v);
    }
};