#pragma once

#include "types.hpp"
#include "object.hpp"

// -----------------------------------------------------------------------------
//      Unique Integer Type
// -----------------------------------------------------------------------------

template<typename T, typename Unique>
struct UniqueInteger
{
    using underlying_type = T;

    T value;

    constexpr UniqueInteger() = default;

    constexpr explicit UniqueInteger(T value)
        : value(value)
    {}

    constexpr auto operator<=>(const UniqueInteger&) const = default;

    constexpr UniqueInteger operator+(T other) const
    {
        return UniqueInteger{value + other};
    }

    constexpr explicit operator bool()
    {
        return bool(value);
    }

    constexpr UniqueInteger& operator++()
    {
        value++;
        return *this;
    }
};

// -----------------------------------------------------------------------------
//      Enum Map
// -----------------------------------------------------------------------------

template<typename E, typename T>
struct EnumMap
{
    T _data[magic_enum::enum_count<E>()];

    static constexpr auto enum_values = magic_enum::enum_values<E>();

    constexpr       T& operator[](E value)       { return _data[magic_enum::enum_index(value).value()]; }
    constexpr const T& operator[](E value) const { return _data[magic_enum::enum_index(value).value()]; }
};

// -----------------------------------------------------------------------------
//      Counting Set
// -----------------------------------------------------------------------------

template<typename T>
struct CountingSet
{
    using value_type = T;

    std::flat_map<T, u32> counts;

    bool inc(auto&& t)
    {
        return !counts[t]++;
    }

    bool dec(auto&& t)
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

    bool contains(const T& t) const { return counts.contains(t); }
    usz      size()           const { return counts.size();      }
    bool    empty()           const { return counts.empty();     }
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

    IntrusiveListIterator remove()
    {
        cur->next->prev = cur->prev;
        cur->prev->next = cur->next;

        cur->next = cur;
        cur->prev = cur;

        return *this;
    }

    Base* operator->() { return get(); }
    Base* get() { return static_cast<Base*>(cur); }

    bool operator==(const IntrusiveListIterator&) const noexcept = default;

    IntrusiveListIterator next() { return {cur->next}; }
    IntrusiveListIterator prev() { return {cur->prev}; }
};

template<typename Base>
struct IntrusiveList
{
    using iterator = IntrusiveListIterator<Base>;

    IntrusiveListBase<Base> root;

    iterator first() { return {root.next}; }
    iterator last()  { return {root.prev}; }
    iterator end()   { return {&root};      }

    bool empty() const { return root.next == &root; }
};

// -----------------------------------------------------------------------------
//      Ref Vector
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

    RefVector& operator=(const RefVector& other)
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

    RefVector& operator=(RefVector&& other)
    {
        if (this != &other) {
            clear();
            values = std::move(other.values);
            other.values.clear();
        }
        return *this;
    }

public:
    T* emplace_back(T* value)
    {
        return values.emplace_back(object_add_ref(value));
    }

    T* emplace_back(Ref<T>&& value)
    {
        return values.emplace_back(std::exchange(value.value, nullptr));
    }

    template<typename Fn>
    usz erase_if(Fn&& fn)
    {
        return std::erase_if(values, [&](auto* c) {
            if (fn(c)) {
                object_remove_ref(c);
                return true;
            }
            return false;
        });
    }

    usz erase(T* v)
    {
        return erase_if([v](T* c) { return c == v; });
    }

    usz  size()  const { return values.size();  }
    bool empty() const { return values.empty(); }
    T*   front() const { return values.front(); }
    T*   back()  const { return values.back();  }

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