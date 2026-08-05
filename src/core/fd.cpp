#include "fd.hpp"

#include "debug.hpp"
#include "log.hpp"
#include "memory.hpp"
#include "chrono.hpp"

struct FdRegistry
{
    FdLimits limit;

    u32 * ref_counts;
    bool* inherited;
};

static FdRegistry registry;

void fd_registry_init()
{
    rlimit lim;
    unix_check<getrlimit>(RLIMIT_NOFILE, &lim);
    registry.limit.max = num_cast<fd_t>(lim.rlim_max);
    registry.limit.inherited = num_cast<fd_t>(lim.rlim_cur);
    registry.limit.current = std::min(registry.limit.max, 1 << 20);

    lim.rlim_cur = num_cast<rlim_t>(registry.limit.current);
    auto res = unix_check<setrlimit>(RLIMIT_NOFILE, &lim);
    debug_assert(res);

    registry.ref_counts = memory_map<u32 >(num_cast<usz>(registry.limit.current));
    registry.inherited  = memory_map<bool>(num_cast<usz>(registry.limit.current));
}

static
void fd_leak_check();

void fd_registry_deinit()
{
    fd_leak_check();

    memory_unmap(registry.ref_counts, num_cast<usz>(registry.limit.current));
    memory_unmap(registry.inherited,  num_cast<usz>(registry.limit.current));
}

// -----------------------------------------------------------------------------

auto fd_get_limits() -> const FdLimits&
{
    return registry.limit;
}

// -----------------------------------------------------------------------------

auto fd_is_valid(fd_t fd) -> bool
{
    return fd >= 0 && fd < num_cast<fd_t>(registry.limit.current);
}

auto fd_are_same(fd_t fd0, fd_t fd1) -> bool
{
    struct stat st0 = {};
    if (unix_check<fstat>(fd0, &st0).err()) return false;

    struct stat st1 = {};
    if (unix_check<fstat>(fd0, &st1).err()) return false;

    return st0.st_ino == st1.st_ino;
}

auto fd_dup_unsafe(fd_t fd) -> fd_t
{
    if (fd < 0) return {};

    return unix_check<fcntl>(fd, F_DUPFD_CLOEXEC, 0).value;
}

// -----------------------------------------------------------------------------

[[maybe_unused]] static
auto fd_exists(fd_t fd) -> bool
{
    auto path = std::format("/proc/self/fd/{}", fd);
    return access(path.c_str(), F_OK) == 0;
}

static
auto iterate_open_fds()
{
   return std::filesystem::directory_iterator("/proc/self/fd")
        | std::views::transform([](auto& entry) -> fd_t {
            fd_t fd = -1;
            auto str = entry.path().filename().string();
            auto res = std::from_chars(str.data(), str.data() + str.size(), fd);
            debug_assert(res.ec == std::errc{}, "Fd :: Parsing [/proc/self/fd/{}] failed with error: {}", str, std::make_error_code(res.ec).message());
            return fd;
        });
}

void fd_mark_open_as_inherited()
{
    for (fd_t open : iterate_open_fds()) {
        registry.inherited[fd_to_index(open)] = true;
    }
}

static
void fd_leak_check()
{
    auto leaked = std::ranges::to<std::vector>(iterate_open_fds()
        | std::views::filter([&](fd_t fd) { return !registry.inherited[fd_to_index(fd)] && fd_exists(fd); }));

    if (!leaked.empty()) {
        log_error("Fd :: {} file descriptor(s) leaked", leaked);
    }
}

auto fd_get_ref_count(fd_t fd) -> u32
{
    if (!fd_is_valid(fd)) return 0;

    return registry.ref_counts[fd_to_index(fd)];
}

auto fd_ref(fd_t fd) -> fd_t
{
    if (!fd_is_valid(fd)) return -1;

    registry.ref_counts[fd_to_index(fd)]++;
    return fd;
}

static
void destroy_fd(fd_t fd)
{
    unix_check<close>(fd);
}

auto fd_unref(fd_t fd) -> fd_t
{
    if (!fd_is_valid(fd)) return -1;

    if (!--registry.ref_counts[fd_to_index(fd)]) {
        destroy_fd(fd);
        return -1;
    }

    return fd;
}

auto fd_extract(fd_t fd) -> fd_t
{
    debug_assert(fd_is_valid(fd));
    debug_assert(fd_get_ref_count(fd) == 1);
    registry.ref_counts[fd_to_index(fd)] = 0;
    return fd;
}

auto Fd::extract() noexcept -> fd_t
{
    return fd_extract(std::exchange(fd, -1));
}
