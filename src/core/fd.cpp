#include "fd.hpp"

#include "debug.hpp"
#include "log.hpp"

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

struct FdRegistry
{
    std::array<u32,  fd_limit> ref_counts = {};
    std::array<bool, fd_limit> inherited  = {};
};

static
auto get_registry() -> FdRegistry&
{
    static FdRegistry registry;
    return registry;
}

void fd_leak_mark_inherited()
{
    auto& fds = get_registry();
    for (fd_t fd = 0; fd < fd_limit; ++fd) {
        if (fd_exists(fd)) {
            fds.inherited[fd] = true;
        }
    }
}

void fd_leak_check()
{
    auto& fds = get_registry();
    auto leaked = std::views::iota(fd_t(0))
        | std::views::take(fd_limit)
        | std::views::filter([&](fd_t fd) { return !fds.inherited[fd] && fd_exists(fd); });

    if (!leaked.empty()) {
        log_error("File Descriptors leaked: {}", leaked);
    }
}

auto fd_get_ref_count(fd_t fd) -> u32
{
    if (!fd_is_valid(fd)) return 0;

    auto& fds = get_registry();
    return fds.ref_counts[fd];
}

auto fd_ref(fd_t fd) -> fd_t
{
    if (!fd_is_valid(fd)) return -1;

    auto& fds = get_registry();
    fds.ref_counts[fd]++;
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

    auto& fds = get_registry();
    if (!--fds.ref_counts[fd]) {
        destroy_fd(fd);
        return -1;
    }

    return fd;
}

auto fd_extract(fd_t fd) -> fd_t
{
    debug_assert(fd_is_valid(fd));
    debug_assert(fd_get_ref_count(fd) == 1);
    auto& fds = get_registry();
    fds.ref_counts[fd] = 0;
    return fd;
}

auto Fd::extract() noexcept -> fd_t
{
    return fd_extract(std::exchange(fd, -1));
}
