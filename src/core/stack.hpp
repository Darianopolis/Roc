#pragma once

#include "debug.hpp"
#include "util.hpp"
#include "types.hpp"
#include "memory.hpp"

struct ThreadStackStorage
{
    byte* head;

    byte* start;
    byte* end;

    static constexpr usz size = usz(1) * 1024 * 1024;

    ThreadStackStorage()
        : head(static_cast<byte*>(unix_check<mmap>(nullptr, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0).value))
        , start(head)
        , end(head + size)
    {
        log_warn("head: {}", (void*)head);
        log_warn("start: {}", (void*)start);
        log_warn("end: {}", (void*)end);
    }

    ~ThreadStackStorage()
    {
        munmap(start, size);
    }

    auto remaining_bytes() const -> usz
    {
        return usz(end - head);
    }
};

inline
ThreadStackStorage& get_thread_stack_storage()
{
    thread_local ThreadStackStorage stack;
    return stack;
}

class ThreadStack
{
    ThreadStackStorage& stack;
    byte* old_head;

public:
    ThreadStack()
        : stack(get_thread_stack_storage())
        , old_head(stack.head)
    {}

    ~ThreadStack()
    {
        stack.head = old_head;
    }

    DELETE_COPY_MOVE(ThreadStack);

    auto get_head() noexcept -> void*
    {
        return stack.head;
    }

    void set_head(void* address) noexcept
    {
        stack.head = align_up_power2(static_cast<byte*>(address), 16);
    }

    constexpr
    auto allocate(usz byte_size) noexcept -> void*
    {
        void* ptr = stack.head;
        set_head(stack.head + byte_size);
        return ptr;
    }

    template<typename T>
        requires std::is_trivially_default_constructible_v<T>
    constexpr
    auto allocate(usz count) noexcept -> T*
    {
        T* ptr = reinterpret_cast<T*>(stack.head);
        set_head(stack.head + sizeof(T) * count);
        return ptr;
    }
};
