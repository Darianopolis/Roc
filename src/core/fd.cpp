#include "fd.hpp"

#include "debug.hpp"
#include "log.hpp"

bool core_fd_are_same(int fd0, int fd1)
{
    struct stat st0 = {};
    if (unix_check<fstat>(fd0, &st0).err()) return false;

    struct stat st1 = {};
    if (unix_check<fstat>(fd0, &st1).err()) return false;

    return st0.st_ino == st1.st_ino;
}

int core_fd_dup_unsafe(int fd)
{
    if (fd < 0) return {};

    return unix_check<fcntl>(fd, F_DUPFD_CLOEXEC, 0).value;
}

// -----------------------------------------------------------------------------

#define CORE_FD_LEAK_CHECK 1

struct core_fd_data
{
    std::array<u32,  core_fd_limit> ref_counts = {};
#if CORE_FD_LEAK_CHECK
    std::array<bool, core_fd_limit> inherited  = {};
#endif

#if CORE_FD_LEAK_CHECK
    core_fd_data()
    {
        for (int fd = 0; fd < core_fd_limit; ++fd) {
            if (fcntl(fd, F_GETFD) == 0) {
                inherited[fd] = true;
            }
        }
    }

    ~core_fd_data()
    {
        for (int fd = 0; fd < core_fd_limit; ++fd) {
            if (inherited[fd]) continue;
            if (fcntl(fd, F_GETFD) == -1) continue;

            log_error("fd[{}] leaked (refs: {})", fd, ref_counts[fd]);
        }
    }
#endif
};

static
core_fd_data fds;

auto core_fd_get_ref_count(int fd) -> u32
{
    if (!core_fd_is_valid(fd)) return 0;

    return fds.ref_counts[fd];
}

#define CORE_NOISY_FDS 0

#if CORE_NOISY_FDS
#define FD_LOG(...) log_debug(__VA_ARGS__)
#else
#define FD_LOG(...)
#endif

auto core_fd_add_ref(int fd) -> int
{
    if (!core_fd_is_valid(fd)) return -1;

    FD_LOG("core_fd_add_ref({}) {} -> {}", fd, fds.ref_counts[fd], fds.ref_counts[fd] + 1);
    fds.ref_counts[fd]++;
    return fd;
}

static
void destroy_fd(int fd)
{
    FD_LOG("  close({})", fd);
    // std::cout << std::stacktrace::current();
    unix_check<close>(fd);
}

auto core_fd_remove_ref(int fd) -> int
{
    if (!core_fd_is_valid(fd)) return -1;

    FD_LOG("core_fd_remove_ref({}) {} -> {}", fd, fds.ref_counts[fd], fds.ref_counts[fd] - 1);
    if (!--fds.ref_counts[fd]) {
        destroy_fd(fd);
        return -1;
    }

    return fd;
}

auto core_fd_extract(int fd) -> int
{
    core_assert(core_fd_is_valid(fd));
    core_assert(core_fd_get_ref_count(fd) == 1);
    fds.ref_counts[fd] = 0;
    return fd;
}

int core_fd::extract() noexcept
{
    return core_fd_extract(std::exchange(fd, -1));
}
