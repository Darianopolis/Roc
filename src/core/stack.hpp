#pragma once

#include "debug.hpp"
#include "util.hpp"
#include "types.hpp"
#include "memory.hpp"

struct core_thread_stack_storage
{
    byte* head;

    byte* start;
    byte* end;

    static constexpr usz size = usz(1) * 1024 * 1024;

    core_thread_stack_storage()
        : head(static_cast<byte*>(unix_check<mmap>(nullptr, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0).value))
        , start(head)
        , end(head + size)
    {
        log_warn("head: {}", (void*)head);
        log_warn("start: {}", (void*)start);
        log_warn("end: {}", (void*)end);
    }

    ~core_thread_stack_storage()
    {
        munmap(start, size);
    }

    usz remaining_bytes() const
    {
        return usz(end - head);
    }
};

inline
core_thread_stack_storage& core_get_thread_stack_storage()
{
    thread_local core_thread_stack_storage stack;
    return stack;
}

class core_thread_stack
{
    core_thread_stack_storage& stack;
    byte* old_head;

public:
    core_thread_stack()
        : stack(core_get_thread_stack_storage())
        , old_head(stack.head)
    {}

    ~core_thread_stack()
    {
        stack.head = old_head;
    }

    CORE_DELETE_COPY_MOVE(core_thread_stack);

    void* get_head() noexcept
    {
        return stack.head;
    }

    void set_head(void* address) noexcept
    {
        stack.head = core_align_up_power2(static_cast<byte*>(address), 16);
    }

    constexpr void* allocate(usz byte_size) noexcept
    {
        void* ptr = stack.head;
        set_head(stack.head + byte_size);
        return ptr;
    }

    template<typename T>
        requires std::is_trivially_default_constructible_v<T>
    constexpr T* allocate(usz count) noexcept
    {
        T* ptr = reinterpret_cast<T*>(stack.head);
        set_head(stack.head + sizeof(T) * count);
        return ptr;
    }
};
