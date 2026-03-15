#pragma once

#include "debug.hpp"
#include "util.hpp"
#include "types.hpp"
#include "memory.hpp"

namespace core
{
    struct ThreadStackStorage
    {
        byte* head;

        byte* start;
        byte* end;

        static constexpr usz size = usz(1) * 1024 * 1024;

        ThreadStackStorage()
            : head(static_cast<byte*>(core::check<mmap>(nullptr, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0).value))
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

        usz remaining_bytes() const
        {
            return usz(end - head);
        }
    };

    inline
    auto core_get_thread_stack_storage() -> core::ThreadStackStorage&
    {
        thread_local core::ThreadStackStorage stack;
        return stack;
    }

    class ThreadStack
    {
        core::ThreadStackStorage& stack;
        byte* old_head;

    public:
        ThreadStack()
            : stack(core_get_thread_stack_storage())
            , old_head(stack.head)
        {}

        ~ThreadStack()
        {
            stack.head = old_head;
        }

        CORE_DELETE_COPY_MOVE(ThreadStack);

        void* get_head() noexcept
        {
            return stack.head;
        }

        void set_head(void* address) noexcept
        {
            stack.head = core::align_up_power2(static_cast<byte*>(address), 16);
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
}
