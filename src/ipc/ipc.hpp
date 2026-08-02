#pragma once

#include "socket.hpp"

// -----------------------------------------------------------------------------

inline
auto ipc_reserve(SocketConnection* conn, u32 size) -> usz
{
    auto& out = conn->out.data;

    debug_assert(out.get_free_bytes() >= size);
    auto offset = out.head;
    out.head += size;
    return offset;
}

// -----------------------------------------------------------------------------

template<typename T>
auto ipc_peek(SocketConnection*) -> std::optional<T>;

template<typename T>
auto ipc_pop(SocketConnection*) -> std::optional<T>;

template<typename T>
void ipc_push(SocketConnection*, T);

// -----------------------------------------------------------------------------

template<typename>
struct IpcTypeInfo
{
    static constexpr bool is_simple = false;
};

template<> struct IpcTypeInfo<u8>  { static constexpr bool is_simple = true; };
template<> struct IpcTypeInfo<u16> { static constexpr bool is_simple = true; };
template<> struct IpcTypeInfo<u32> { static constexpr bool is_simple = true; };
template<> struct IpcTypeInfo<u64> { static constexpr bool is_simple = true; };

template<> struct IpcTypeInfo<i8>  { static constexpr bool is_simple = true; };
template<> struct IpcTypeInfo<i16> { static constexpr bool is_simple = true; };
template<> struct IpcTypeInfo<i32> { static constexpr bool is_simple = true; };
template<> struct IpcTypeInfo<i64> { static constexpr bool is_simple = true; };

template<> struct IpcTypeInfo<f32> { static constexpr bool is_simple = true; };
template<> struct IpcTypeInfo<f64> { static constexpr bool is_simple = true; };

// -----------------------------------------------------------------------------
//      Numbers
// -----------------------------------------------------------------------------

template<typename T>
    requires IpcTypeInfo<T>::is_simple
auto ipc_peek(SocketConnection* conn) -> std::optional<T>
{
    auto& in = conn->in.data;

    if (in.get_used_bytes() < sizeof(T)) return std::nullopt;

    T value;
    std::memcpy(&value, in.get_tail(), sizeof(T));

    return value;
}

template<typename T>
    requires IpcTypeInfo<T>::is_simple
auto ipc_pop(SocketConnection* conn) -> std::optional<T>
{
    auto value = ipc_peek<T>(conn);
    if (!value) return value;
    conn->in.data.tail += sizeof(T);
    return value;
}

template<typename T>
    requires IpcTypeInfo<T>::is_simple
void ipc_push(SocketConnection* conn, T value)
{
    auto& out = conn->out.data;

    debug_assert(out.get_free_bytes() >= sizeof(T));

    std::memcpy(out.get_head(), &value, sizeof(T));
    out.head += sizeof(T);
}

// -----------------------------------------------------------------------------
//      Strings
// -----------------------------------------------------------------------------

using ipc_string_size_type = u32;

static
auto ipc_get_string_encoded_size(usz size_with_null) -> usz
{
    return align_up_power2(sizeof(ipc_string_size_type) + size_with_null, sizeof(ipc_string_size_type));
}

/*
 * Null strings are denoted when `view.data() == nullptr`
 *
 * Otherwise, `view.data()` is guaranteed to be a null-terminated string of length `view.size()`
 */
template<>
inline
auto ipc_peek<std::string_view>(SocketConnection* conn) -> std::optional<std::string_view>
{
    auto& in = conn->in.data;

    auto size_with_null = ipc_peek<ipc_string_size_type>(conn);
    if (!size_with_null) return std::nullopt;

    if (*size_with_null == 0) return std::string_view{nullptr, 0u};

    if (in.get_used_bytes() < ipc_get_string_encoded_size(*size_with_null)) return std::nullopt;

    return std::string_view{reinterpret_cast<const char*>(in.get_tail() + sizeof(ipc_string_size_type)), *size_with_null - 1};
}

template<>
inline
auto ipc_pop<std::string_view>(SocketConnection* conn) -> std::optional<std::string_view>
{
    auto view = ipc_peek<std::string_view>(conn);
    if (!view) return std::nullopt;
    conn->in.data.tail += ipc_get_string_encoded_size(view->size() + bool(view->data()));
    return view;
}

template<>
inline
void ipc_push<std::string_view>(SocketConnection* conn, std::string_view view)
{
    auto& out = conn->out.data;

    ipc_string_size_type size_with_null = num_cast<ipc_string_size_type>(view.size()) + 1u;
    usz encoded_size = ipc_get_string_encoded_size(size_with_null);
    debug_assert(out.get_free_bytes() >= encoded_size);

    auto* head = out.get_head();
    std::memcpy(head, &size_with_null, sizeof(ipc_string_size_type));
    head += sizeof(ipc_string_size_type);
    std::memcpy(head, view.data(), view.size());
    head[view.size()] = std::byte('\0');
    out.head += encoded_size;
}

template<>
inline
void ipc_push<const char*>(SocketConnection* conn, const char* str)
{
    if (str) {
        ipc_push<std::string_view>(conn, str);
    } else {
        ipc_push<u32>(conn, 0u);
    }
}

// -----------------------------------------------------------------------------
//      File Descriptors
// -----------------------------------------------------------------------------

inline
auto ipc_peek_fd(SocketConnection* conn) -> Fd
{
    auto& in = conn->in.fds;

    if (!in.get_used()) return {};
    return Fd(*in.get_tail());
}

inline
auto ipc_pop_fd(SocketConnection* conn) -> Fd
{
    auto fd = ipc_peek_fd(conn);
    if (!fd) return {};
    conn->in.fds.tail++;
    return fd;
}

inline
void ipc_push_fd(SocketConnection* conn, Fd fd)
{
    auto& out = conn->out.fds;

    debug_assert(out.get_free());
    *out.get_head() = fd.extract();
    out.head++;
}
