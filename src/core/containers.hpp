#pragma once

#include "types.hpp"

// -----------------------------------------------------------------------------
//      Map / Set
// -----------------------------------------------------------------------------

namespace core
{
    template<typename K, typename V>
    using Map = ankerl::unordered_dense::map<K, V>;

    template<typename K, typename V>
    using SegmentedMap = ankerl::unordered_dense::segmented_map<K, V>;

    template<typename T>
    using Set = ankerl::unordered_dense::set<T>;

    template<typename T>
    using FlatSet = std::flat_set<T>;
}

// -----------------------------------------------------------------------------
//      Enum Map
// -----------------------------------------------------------------------------

namespace core
{
    template<typename E, typename T>
    struct EnumMap
    {
        T _data[magic_enum::enum_count<E>()];

        static constexpr auto enum_values = magic_enum::enum_values<E>();

        constexpr       T& operator[](E value)       { return _data[magic_enum::enum_index(value).value()]; }
        constexpr const T& operator[](E value) const { return _data[magic_enum::enum_index(value).value()]; }
    };
}

// -----------------------------------------------------------------------------
//      Counting Set
// -----------------------------------------------------------------------------

namespace core
{
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
            core_assert(iter != counts.end());
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
}

// -----------------------------------------------------------------------------
//      Fixed Array
// -----------------------------------------------------------------------------

namespace core
{
    template<typename T, u32 Max>
    struct FixedArray {
        std::array<T, Max> data = {};
        u32 count = 0;

        auto begin(this auto&& self) { return self.data.begin();              }
        auto   end(this auto&& self) { return self.data.begin() + self.count; }

        auto& operator[](this auto&& self, usz i) { return self.data[i]; }
    };
}

// -----------------------------------------------------------------------------
//      Intrusive Linked List
// -----------------------------------------------------------------------------

namespace core
{
    template<typename Base>
    struct IntrusiveListBase
    {
        IntrusiveListBase* next = this;
        IntrusiveListBase* prev = this;
    };

    template<typename Base>
    struct IntrusiveListIterator
    {
        core::IntrusiveListBase<Base>* cur;

        void insert_after(core::IntrusiveListBase<Base>* base)
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
        using iterator = core::IntrusiveListIterator<Base>;

        core::IntrusiveListBase<Base> root;

        iterator first() { return {root.next}; }
        iterator last()  { return {root.prev}; }
        iterator end()   { return {&root};      }

        bool empty() const { return root.next == &root; }
    };
}
