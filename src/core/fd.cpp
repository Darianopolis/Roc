#include "fd.hpp"

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
    static constexpr u32 max_fds = core_fd_max + 1;

    struct {
        std::array<ref<core_fd_listener>, max_fds> listeners  = {};
        std::array<u32,                   max_fds> ref_counts = {};
        std::array<bool,                  max_fds> no_close   = {};
#if CORE_FD_LEAK_CHECK
        std::array<bool,                  max_fds> inherited  = {};
#endif
    } data;

    ref<core_fd_listener>* listeners  = data.listeners.data()  + 1;
    u32*                   ref_counts = data.ref_counts.data() + 1;
    bool*                  no_close   = data.no_close.data()   + 1;

#if CORE_FD_LEAK_CHECK
    core_fd_data()
    {
        for (int fd = 0; fd < core_fd_max; ++fd) {
            if (fcntl(fd, F_GETFD) == 0) {
                data.inherited[fd] = true;
            }
        }
    }

    ~core_fd_data()
    {
        for (int fd = 0; fd < core_fd_max; ++fd) {
            if (data.inherited[fd]) continue;
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
    return fds.ref_counts[fd];
}

auto core_fd_get_listener(int fd) -> core_fd_listener*
{
    return fds.listeners[fd].get();
}

void core_fd_set_listener(int fd, core_fd_listener* listener)
{
    if (fd < 0) return;

    fds.listeners[fd] = listener;
}

#define CORE_NOISY_FDS 0

#if CORE_NOISY_FDS
#define FD_LOG(...) log_debug(__VA_ARGS__)
#else
#define FD_LOG(...)
#endif

auto core_fd_add_ref(int fd) -> int
{
    if (fd == -1) return -1;

    FD_LOG("core_fd_add_ref({}) {} -> {}", fd, fds.ref_counts[fd], fds.ref_counts[fd] + 1);
    fds.ref_counts[fd]++;
    return fd;
}

static
void destroy_fd(int fd)
{
    if (fds.listeners[fd]) {
        FD_LOG("  core_fd_remove_listener({})", fd);
        core_fd_remove_listener(fd);
    }

    if (!fds.no_close[fd]) {
        FD_LOG("  close({})", fd);
        unix_check<close>(fd);
    } else {
        // Next
        fds.no_close[fd] = false;
    }
}

auto core_fd_remove_ref(int fd) -> int
{
    if (fd == -1) return -1;

    FD_LOG("core_fd_remove_ref({}) {} -> {}", fd, fds.ref_counts[fd], fds.ref_counts[fd] - 1);
    if (!--fds.ref_counts[fd]) {
        destroy_fd(fd);
        return -1;
    }

    return fd;
}

// -----------------------------------------------------------------------------

core_fd core_fd_adopt(int fd)
{
    FD_LOG("core_fd_adopt({})", fd);
    core_assert(core_fd_get_ref_count(fd) == 0);
    core_assert(fds.no_close[fd] == false);
    return core_fd(fd);
}

core_fd core_fd_reference(int fd)
{
    FD_LOG("core_fd_reference({})", fd);
    core_assert(core_fd_get_ref_count(fd) == 0);
    core_assert(fds.no_close[fd] == false);
    fds.no_close[fd] = true;
    return core_fd(fd);
}

core_fd core_fd_dup(int fd)
{
    return fd >= 0 ? core_fd_adopt(core_fd_dup_unsafe(fd)) : core_fd();
}
