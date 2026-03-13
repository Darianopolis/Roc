#pragma once

#include "pch.hpp"
#include "log.hpp"
#include "types.hpp"

// -----------------------------------------------------------------------------

#if defined(__clang__)
#define CORE_NOINLINE [[clang::noinline]]

#elif defined(__GNUC__) || defined(__GNUG__)
#define CORE_NOINLINE __attribute__ ((noinline))
#endif

// -----------------------------------------------------------------------------

CORE_NOINLINE inline
void core_debugbreak()
{
    std::cerr << std::stacktrace::current() << std::endl;
    raise(SIGTRAP);
}

[[noreturn]] CORE_NOINLINE inline
void core_debugkill()
{
    std::cerr << std::stacktrace::current() << std::endl;
    std::terminate();
}

[[noreturn]] inline
void core_unreachable()
{
#ifdef NDEBUG
    std::unreachable();
#else
    core_debugkill();
#endif
}

[[noreturn]] CORE_NOINLINE inline
void core_assert_fail(std::string_view expr, std::string_view reason = {})
{
    log_error("assert({}) failed{}{}", expr, reason.empty() ? "" : ": ", reason);
    core_debugkill();
}

#define core_assert(Expr, ...) \
    (static_cast<bool>(Expr) ? void() : core_assert_fail(#Expr __VA_OPT__(, std::format(__VA_ARGS__))))

// -----------------------------------------------------------------------------

template<typename Enum>
struct core_enum_name_props
{
    static constexpr usz prefix = 0;
    static constexpr usz suffix = 0;
};

#define CORE_DEFINE_ENUM_NAME_PROPS(Enum, Prefix, Suffix) \
    template<> \
    struct core_enum_name_props<Enum> \
    { \
        static constexpr usz prefix = std::string_view(Prefix).size(); \
        static constexpr usz suffix = std::string_view(Suffix).size(); \
    }

template<typename Enum>
std::string_view core_enum_to_string(Enum value)
{
    std::string_view name = magic_enum::enum_name(value);
    name.remove_prefix(core_enum_name_props<Enum>::prefix);
    name.remove_suffix(core_enum_name_props<Enum>::suffix);
    return name;
}

// -----------------------------------------------------------------------------

template<typename Fn>
struct core_defer_guard
{
    Fn fn;

    core_defer_guard(Fn&& fn): fn(std::move(fn)) {}
    ~core_defer_guard() { fn(); };
};

#define defer core_defer_guard _ = [&]

// -----------------------------------------------------------------------------

template<typename... Ts>
struct core_overload_set : Ts... {
    using Ts::operator()...;
};

template<typename... Ts> core_overload_set(Ts...) -> core_overload_set<Ts...>;

// -----------------------------------------------------------------------------

constexpr
std::string core_ascii_to_upper(std::string_view in)
{
    std::string out(in);
    for (char& c : out) c = std::toupper(c);
    return out;
}

// -----------------------------------------------------------------------------

struct core_byte_view
{
    const void* data;
    usz         size;
};

inline
auto core_view_bytes(auto&& object) -> core_byte_view
{
    return { &object, sizeof(object) };
}

// -----------------------------------------------------------------------------

constexpr auto ptr_to(auto&& value) { return &value; }

// -----------------------------------------------------------------------------

#define CORE_FUNC_REF(func) [](void* d, auto... args) { return (*static_cast<decltype(func)*>(d))(std::forward<decltype(args)>(args)...); }, &func

// -----------------------------------------------------------------------------

#define CORE_DELETE_COPY(Type) \
               Type(const Type& ) = delete; \
    Type& operator=(const Type& ) = delete; \

#define CORE_DELETE_COPY_MOVE(Type)         \
    CORE_DELETE_COPY(Type)                  \
               Type(      Type&&) = delete; \
    Type& operator=(      Type&&) = delete;

// -----------------------------------------------------------------------------

template<typename E, typename T>
struct core_enum_map
{
    T _data[magic_enum::enum_count<E>()];

    static constexpr auto enum_values = magic_enum::enum_values<E>();

    constexpr       T& operator[](E value)       { return _data[magic_enum::enum_index(value).value()]; }
    constexpr const T& operator[](E value) const { return _data[magic_enum::enum_index(value).value()]; }
};

// -----------------------------------------------------------------------------

template<typename T>
struct core_counting_set
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

// -----------------------------------------------------------------------------

constexpr vec2f64 core_copysign(     vec2f64 v, vec2f64 s) { return vec2f64(std::copysign(v.x, s.x), std::copysign(v.y, s.y)); }
constexpr vec2f64 core_round_to_zero(vec2f64 v)            { return core_copysign(glm::floor(glm::abs(v)), v);                 }

// -----------------------------------------------------------------------------

template<typename T>
auto core_iterate(std::span<T> view, bool reverse = false)
{
    struct iterator
    {
        std::span<T> view;
        i64 cur, end, step;

        bool operator==(std::default_sentinel_t) const { return cur == end; }
        void operator++() { cur += step; }
        T&   operator*()  { return view[cur]; }
    };

    struct iterable
    {
        std::span<T> view;
        bool backward;

        iterator begin() {
            return backward
                ? iterator { view, i64(view.size()) - 1, -1, -1 }
                : iterator { view, 0, i64(view.size()), 1 };
        }

        std::default_sentinel_t end() { return {}; }
    };

    return iterable{view, reverse};
}

// -----------------------------------------------------------------------------

struct core_command_parser
{
    std::span<const std::string_view> args;
    u32 index;

    operator bool() const { return index < args.size(); }

    bool match(std::string_view arg)
    {
        if (index < args.size() && args[index] == arg) {
            index++;
            return true;
        }
        return false;
    }

    std::span<const std::string_view> peek_rest() { return args.subspan(index); }

    std::string_view peek()       { return index < args.size() ? args[index]   : std::string_view{}; }
    std::string_view get_string() { return index < args.size() ? args[index++] : std::string_view{}; }

    template<typename T>
    std::optional<T> get_from_chars()
    {
        if (index >= args.size()) return std::nullopt;

        T value;
        auto res = std::from_chars(args[index].begin(), args[index].end(), value);
        if (!res) return std::nullopt;

        index++;

        return value;
    }

    std::optional<int> get_int()    { return get_from_chars<int>(); }
    std::optional<int> get_double() { return get_from_chars<f64>(); }
};

// -----------------------------------------------------------------------------

inline
std::chrono::system_clock::time_point core_time_current()
{
    return std::chrono::system_clock::now();
}

// Assume steady_clock is implemented as CLOCK_MONOTONIC.
static constexpr int core_steady_clock_id = CLOCK_MONOTONIC;

template<int ClockID>
std::chrono::steady_clock::time_point core_steady_clock_from_timespec(const timespec& ts)
{
    static_assert(core_steady_clock_id == ClockID);

    auto ns = ts.tv_sec * 1'000'000'000 + ts.tv_nsec;
    auto dur = std::chrono::nanoseconds(ns);
    return std::chrono::steady_clock::time_point(dur);
}

template<int ClockID>
timespec core_steady_clock_to_timespec(std::chrono::steady_clock::time_point tp)
{
    static_assert(core_steady_clock_id == ClockID);

    auto ns = tp.time_since_epoch().count();

    timespec ts;
    ts.tv_sec  = ns / 1'000'000'000;
    ts.tv_nsec = ns % 1'000'000'000;
    return ts;
}

enum class core_time_format : u32
{
    iso8601,
    date_pretty,
    datetime,
    datetime_ms,
    time,
    time_ms,
};

std::string core_time_to_string(std::chrono::system_clock::time_point, core_time_format);

std::string core_duration_to_string(std::chrono::duration<f64, std::nano> dur);
std::string core_byte_size_to_string(u64 bytes);

// -----------------------------------------------------------------------------

template<typename T>
T* core_byte_offset_pointer(void* source, isz offset)
{
    return reinterpret_cast<T*>(reinterpret_cast<byte*>(source) + offset);
}

// -----------------------------------------------------------------------------

inline
usz core_compute_geometric_growth(usz current_size, usz new_min_size)
{
    usz geometric = current_size + (current_size >> 1);
    return std::max(geometric, new_min_size);
}

// -----------------------------------------------------------------------------

inline
std::string core_replace_suffix(std::string_view in, std::string_view old_suffix, std::string_view new_suffix)
{
    return std::format("{}{}", in.substr(0, in.size() - old_suffix.size()), new_suffix);
}

inline
std::string core_escape_utf8(std::string_view in)
{
    std::string out;
    for (char c : in) {
        switch (c) {
            break;case '\r': out += "\\r";
            break;case '\n': out += "\\n";
            break;case '\b': out += "\\b";
            break;case '\t': out += "\\t";
            break;case '\f': out += "\\f";
            break;default:
                if (::isalpha(c) || ::isdigit(c)) {
                    out += c;
                } else {
                    out += std::format("\\{:x}", c);
                }
        }
    }
    return out;
}

// -----------------------------------------------------------------------------

inline
void core_log_unix_error(std::string_view message, int err = 0)
{
    err = err ?: errno;

    if (message.empty()) { log_error("({}) {}",              err, strerror(err)); }
    else                 { log_error("{}: ({}) {}", message, err, strerror(err)); }
}

template<typename T>
struct core_unix_result
{
    T   value;
    int error;

    bool ok()  const noexcept { return !error; }
    bool err() const noexcept { return  error; }
};

template<typename T>
    requires std::is_integral_v<T> || std::is_pointer_v<T>
core_unix_result<T> core_unix_check(T value, auto... quiet)
{
    static constexpr int fallback_error_code = INT_MAX;
    int error_code = 0;
    if constexpr (std::is_signed_v<T>) {
        if (value < 0) {
            if (value == -1 && errno) {
                // TODO: This has a failure case where `errno` is set spuriously when
                //       EPERM was the intended error code
                error_code = errno;
            } else {
                error_code = -int(value);
            }
        }
    } else if (!value) {
        error_code = errno ?: fallback_error_code;
    }

    if (!error_code || (... || (error_code == quiet))) [[likely]] {
        return { value, error_code };
    }

    core_log_unix_error("core_unix_check", error_code);

    return { value, error_code };
}

#define unix_check(Expr, ...) \
    core_unix_check((errno = 0, (Expr)) __VA_OPT__(,) __VA_ARGS__)

// -----------------------------------------------------------------------------

inline
u64 core_eventfd_read(int fd)
{
    u64 count = 0;
    return (unix_check(read(fd, &count, sizeof(count)), EAGAIN, EINTR).value == sizeof(count)) ? count : 0;
}

inline
void core_eventfd_signal(int fd, u64 inc)
{
    unix_check(write(fd, &inc, sizeof(inc)));
}

// -----------------------------------------------------------------------------

template<typename T> std::string core_to_string(const core_vec<2, T>& vec) { return std::format("({}, {})",         vec.x, vec.y);               }
template<typename T> std::string core_to_string(const core_vec<3, T>& vec) { return std::format("({}, {}, {})",     vec.x, vec.y, vec.z);        }
template<typename T> std::string core_to_string(const core_vec<4, T>& vec) { return std::format("({}, {}, {}, {})", vec.x, vec.y, vec.z, vec.w); }

template<typename T>
std::string core_to_string(const core_rect<T>& rect)
{
    return std::format("(({}, {}) : ({}, {}))", rect.origin.x, rect.origin.y, rect.extent.x, rect.extent.y);
}

template<typename T>
std::string core_to_string(const core_aabb<T>& aabb)
{
    return std::format("(({}, {}) < ({}, {}))", aabb.min.x, aabb.min.y, aabb.max.x, aabb.max.y);
}

// -----------------------------------------------------------------------------

template<typename T>
core_vec<2, T> core_aabb_clamp_point(const core_aabb<T>& rect, core_vec<2, T> point)
{
    return glm::clamp(point, rect.min, rect.max);
}

template<typename T>
bool core_aabb_contains(const core_aabb<T>& rect, core_vec<2, T> point)
{
    return point.x >= rect.min.x && point.x < rect.max.x
        && point.y >= rect.min.y && point.y < rect.max.y;
}

template<typename T>
core_aabb<T> core_aabb_outer(const core_aabb<T>& a, const core_aabb<T>& b)
{
    return {glm::min(a.min, b.min), glm::max(a.max, b.max), core_minmax};
}

template<typename T>
core_aabb<T> core_aabb_inner(const core_aabb<T>& a, const core_aabb<T>& b)
{
    return {glm::max(a.min, b.min), glm::min(a.max, b.max), core_minmax};
}

template<typename T>
bool core_aabb_intersects(const core_aabb<T>& a, const core_aabb<T>& b, core_aabb<T>* intersection = nullptr)
{
    auto i = core_aabb_inner(a, b);

    if (i.max.x <= i.min.x || i.max.y <= i.min.y) {
        if (intersection) *intersection = {};
        return false;
    } else {
        if (intersection) *intersection = i;
        return true;
    }
}

template<typename T>
static
u32 core_aabb_subtract(const core_aabb<T>& minuend, const core_aabb<T>& subtrahend, core_aabb<T>* out)
{
    core_aabb<T> intersection;
    if (core_aabb_intersects(minuend, subtrahend, &intersection)) {
        u32 count = 0;
        if (minuend.min.x != intersection.min.x) /* left   */ out[count++] = {{     minuend.min.x, intersection.min.y}, {intersection.min.x, intersection.max.y}, core_minmax};
        if (minuend.max.x != intersection.max.x) /* right  */ out[count++] = {{intersection.max.x, intersection.min.y}, {     minuend.max.x, intersection.max.y}, core_minmax};
        if (minuend.min.y != intersection.min.y) /* top    */ out[count++] = {{     minuend.min},                       {     minuend.max.x, intersection.min.y}, core_minmax};
        if (minuend.max.y != intersection.max.y) /* bottom */ out[count++] = {{     minuend.min.x, intersection.max.y}, {     minuend.max                      }, core_minmax};
        return count;
    } else {
        *out = minuend;
        return 1;
    }
}

// -----------------------------------------------------------------------------

template<typename T>
core_vec<2, T> core_rect_clamp_point(const core_rect<T>& rect, core_vec<2, T> point)
{
    return core_aabb_clamp_point<T>(rect, point);
}

template<typename T>
bool core_rect_contains(const core_rect<T>& rect, core_vec<2, T> point)
{
    return core_aabb_contains<T>(rect, point);
}

template<typename T>
bool core_rect_intersects(const core_rect<T>& a, const core_rect<T>& b, core_rect<T>* intersection = nullptr)
{
    core_aabb<T> i;
    bool intersects = core_aabb_intersects<T>(a, b, &i);
    if (intersection) *intersection = i;
    return intersects;
}

template<typename T>
core_rect<T> core_rect_constrain(core_rect<T> rect, const core_rect<T>& bounds)
{
    static constexpr auto constrain_axis = [](T start, T length, T& origin, T& extent) {
        if (extent > length) {
            origin = start;
            extent = length;
        } else {
            origin = std::max(origin, start) - std::max(T(0), (origin + extent) - (start + length));
        }
    };
    constrain_axis(bounds.origin.x, bounds.extent.x, rect.origin.x, rect.extent.x);
    constrain_axis(bounds.origin.y, bounds.extent.y, rect.origin.y, rect.extent.y);
    return rect;
}

// -----------------------------------------------------------------------------

template<typename T>
core_rect<T> core_rect_fit(core_vec<2, T> outer, core_vec<2, T> inner)
{
    T scale = glm::min(outer.x / inner.x, outer.y / inner.y);
    auto extent = inner * scale;
    auto offset = (outer - extent) / T(2);
    return {offset, extent, core_xywh};
}

// -----------------------------------------------------------------------------

template<typename Out, typename In>
core_vec<2, Out> core_round(core_vec<2, In> pos, core_vec<2, In>* remainder = nullptr)
{
    // For points, we floor to treat the position as any point within a given integer region
    auto rounded = glm::floor(pos);
    if (remainder) *remainder = pos - rounded;
    return rounded;
}

template<typename Out, typename In>
core_rect<Out> core_round(core_rect<In> rect, core_rect<In>* remainder = nullptr)
{
    core_aabb<In> bounds = rect;
    auto min = bounds.min;
    auto max = bounds.max;
    // For rects, we round as the min and max are treated as integer boundaries
    auto extent = glm::round(max - min);
    auto origin = glm::round(min);
    if (remainder) {
        *remainder = {
            min - origin,
            max - min - (extent),
            core_xywh,
        };
    }
    return { origin, extent, core_xywh };
}

// -----------------------------------------------------------------------------

constexpr usz core_round_up_power2(usz v) noexcept
{
    v--;
    v |= v >> 1;
    v |= v >> 2;
    v |= v >> 4;
    v |= v >> 8;
    v |= v >> 16;
    v |= v >> 32;
    v++;

    return v;
}

// -----------------------------------------------------------------------------

constexpr
u8 core_hex_to_value(char digit)
{
    switch (digit) {
        break;case 'a' ... 'f': return 10 + digit - 'a';
        break;case 'A' ... 'F': return 10 + digit - 'A';
        break;case '0' ... '9': return digit - '0';
        break;default:
            core_unreachable();
    }
}

constexpr
vec4u8 core_color_from_hex(std::string_view str)
{
    vec4u8 color;
    if (str.starts_with("#")) str.remove_prefix(1);

    auto hex_to_value = [&](u32 i) -> u8 {
        return core_hex_to_value(str[i]) * 16 + core_hex_to_value(str[i + 1]);
    };

    core_assert(str.size() >= 6);

    color.x = hex_to_value(0);
    color.y = hex_to_value(2);
    color.z = hex_to_value(4);
    color.w = str.size() >= 8 ? hex_to_value(6) : 255;

    return color;
}

// -----------------------------------------------------------------------------

inline
u64 core_hash_mix(u64 x)
{
    // From boost
    // https://github.com/boostorg/container_hash/blob/060d4aea6b5b59d2c9146b7d8e994735b2c0a582/include/boost/container_hash/detail/hash_mix.hpp#L67-L81

    static constexpr u64 m = (u64(0xe9846af) << 32) + 0x9b1a615d;

    x ^= x >> 32;
    x *= m;
    x ^= x >> 32;
    x *= m;
    x ^= x >> 28;

    return x;
}

template<typename T>
usz core_hash_single(const T& v)
{
    return std::hash<T>{}(v);
}

template<typename T>
void core_hash_combine(usz& seed, const T& v)
{
    // From boost
    // https://github.com/boostorg/container_hash/blob/060d4aea6b5b59d2c9146b7d8e994735b2c0a582/include/boost/container_hash/hash.hpp#L469-L473

    seed = core_hash_mix(seed + 0x9e3779b9 + core_hash_single(v));
}

inline
void core_hash_range(usz& seed, auto start, auto end)
{
    for (; start != end; ++start) {
        core_hash_combine(seed, *start);
    }
}

inline
usz core_hash_range(auto start, auto end)
{
    usz seed = 0;
    core_hash_range(seed, start, end);
    return seed;
}

#define CORE_MAKE_RANGE_HASHABLE(Type) \
    template<> struct std::hash<Type> { \
        usz operator()(const Type& v) { return core_hash_range(v.begin(), v.end()); } \
    };

inline
usz core_hash_variadic(const auto& first, const auto&... rest)
{
    usz seed = core_hash_single(first);
    (core_hash_combine(seed, rest), ...);
    return seed;
}

#define CORE_MAKE_STRUCT_HASHABLE(Type, ...) \
    template<> struct std::hash<Type> { \
        usz operator()(const Type& v) { return core_hash_variadic(__VA_ARGS__); } \
    };

// -----------------------------------------------------------------------------

struct core_stacktrace_entry_data
{
    bool populated = false;
    std::string description;
    std::filesystem::path source_file;
    u32 source_line;
};

struct core_stacktrace_entry
{
    const core_stacktrace_entry_data* data;

    const std::string& description() const noexcept { return data->description; }
    const std::filesystem::path& source_file() const noexcept { return data->source_file; }
    u32 source_line() const noexcept { return data->source_line; }
};

struct core_stacktrace
{
    std::vector<core_stacktrace_entry> entries;

    core_stacktrace() = default;

    void populate(struct core_stacktrace_cache& cache, const std::stacktrace& stacktrace);

    usz size() const noexcept { return entries.size(); }
    core_stacktrace_entry at(usz i) const { return entries.at(i); }

    auto begin() const noexcept { return entries.begin(); }
    auto end() const noexcept { return entries.end(); }
};

std::string core_to_string(const core_stacktrace& st);

struct core_stacktrace_cache
{
    ankerl::unordered_dense::segmented_map<std::stacktrace_entry, core_stacktrace_entry_data> entries;
    ankerl::unordered_dense::segmented_map<std::stacktrace, core_stacktrace> traces;

    std::pair<const core_stacktrace*, bool> insert(const std::stacktrace& st);
};

// -----------------------------------------------------------------------------

template<typename T, u32 Max>
struct core_fixed_array {
    T data[Max];
    u32 count;

    auto begin(this auto&& self) { return self.data; }
    auto   end(this auto&& self) { return self.data + self.count; }

    auto& operator[](this auto&& self, usz i) { return self.data[i]; }
};

// -----------------------------------------------------------------------------

bool core_capability_has( cap_value_t cap);
void core_capability_drop(cap_value_t cap);

// -----------------------------------------------------------------------------

template<typename Base>
struct core_intrusive_list_base
{
    core_intrusive_list_base* next = this;
    core_intrusive_list_base* prev = this;
};

template<typename Base>
struct core_intrusive_list_iterator
{
    core_intrusive_list_base<Base>* cur;

    void insert_after(core_intrusive_list_base<Base>* base)
    {
        base->prev = cur;
        base->next = cur->next;

        cur->next->prev = base;
        cur->next = base;
    }

    core_intrusive_list_iterator remove()
    {
        cur->next->prev = cur->prev;
        cur->prev->next = cur->next;

        cur->next = cur;
        cur->prev = cur;

        return *this;
    }

    Base* operator->() { return get(); }
    Base* get() { return static_cast<Base*>(cur); }

    bool operator==(const core_intrusive_list_iterator&) const noexcept = default;

    core_intrusive_list_iterator next() { return {cur->next}; }
    core_intrusive_list_iterator prev() { return {cur->prev}; }
};

template<typename Base>
struct core_intrusive_list
{
    using iterator = core_intrusive_list_iterator<Base>;

    core_intrusive_list_base<Base> root;

    iterator first() { return {root.next}; }
    iterator last()  { return {root.prev}; }
    iterator end()   { return {&root};      }

    bool empty() const { return root.next == &root; }
};
