#pragma once

#include "types.hpp"
#include "debug.hpp"
#include "util.hpp"
#include "memory.hpp"
#include "fd.hpp"
#include "process.hpp"

template<typename T>
auto ring_buffer_align_capacity(usz capacity) -> usz
{
    // Count must be a power of 2, and byte size must be a multiple of the system page size.
    // Try increasing powers of 2 until one aligns correctly.

    capacity = round_up_power2(capacity);
    debug_assert(capacity);

    usz page_size = process_get_pagesize();
    while ((capacity * sizeof(T)) % page_size != 0) {
        capacity <<= 1;
        debug_assert(capacity);
    }

    return capacity;
}

template<typename T>
struct RingBuffer
{
    T* data;
    usz capacity;
    usz head;
    usz tail;

    // -------------------------------------

    RingBuffer()
        : data(nullptr)
        , capacity(0)
        , head(0)
        , tail(0)
    {}

    RingBuffer(fd_t fd, __off_t offset, usz capacity)
        : capacity(capacity)
    {
        debug_assert(ring_buffer_align_capacity<T>(capacity) == capacity, "RingBuffer capacity must satisfy alignment requirmeents");

        usz cap_bytes = capacity * sizeof(T);

        data = static_cast<T*>(unix_check<mmap>(nullptr, cap_bytes * 2, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, FD_INVALID, 0).value);

        int prot  = PROT_READ | PROT_WRITE;
        int flags = MAP_SHARED | MAP_FIXED | MAP_FILE;
        unix_check<mmap>(data,                                       cap_bytes, prot, flags, fd, offset);
        unix_check<mmap>(byte_offset_pointer<void>(data, cap_bytes), cap_bytes, prot, flags, fd, offset);
    }

    RingBuffer(usz capacity)
        : RingBuffer(({
            Fd fd = Fd(unix_check<memfd_create>("RingBuffer", MFD_CLOEXEC).value);
            unix_check<ftruncate>(fd.get(), num_cast<__off_t>(capacity * sizeof(T)));
            fd;
        }).get(), 0, capacity)
    {}

    RingBuffer(    const RingBuffer&) = delete;
    auto operator=(const RingBuffer&) = delete;

    RingBuffer(RingBuffer&& other)
        : data(    std::exchange(other.data,     nullptr))
        , capacity(std::exchange(other.capacity, 0))
        , head(    std::exchange(other.head,     0))
        , tail(    std::exchange(other.tail,     0))
    {}

    auto operator=(RingBuffer&& other) -> RingBuffer&
    {
        if (this != &other) {
            this->~RingBuffer();
            new (this) RingBuffer(std::move(other));
        }
        return *this;
    }

    ~RingBuffer()
    {
        if (data) {
            usz cap_bytes = capacity * sizeof(T);
            unix_check<munmap>(data, cap_bytes);
            unix_check<munmap>(data + capacity, cap_bytes);
        }
    }

    // -------------------------------------

    auto get_mask() const -> usz
    {
        return capacity - 1;
    }

    auto get_used() const -> usz
    {
        return head - tail;
    }

    auto get_free() const -> usz
    {
        return capacity - get_used();
    }

    auto get_used_bytes() const -> usz
    {
        return get_used() * sizeof(T);
    }

    auto get_free_bytes() const -> usz
    {
        return get_free() * sizeof(T);
    }

    auto get_head() -> T*
    {
        return data + (head & get_mask());
    }

    auto get_tail() -> T*
    {
        return data + (tail & get_mask());
    }
};
