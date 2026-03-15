#include "fd.hpp"

bool core::fd::are_same(int fd0, int fd1)
{
    struct stat st0 = {};
    if (core::check<fstat>(fd0, &st0).err()) return false;

    struct stat st1 = {};
    if (core::check<fstat>(fd0, &st1).err()) return false;

    return st0.st_ino == st1.st_ino;
}

int core::fd::dup_unsafe(int fd)
{
    if (fd < 0) return {};

    return core::check<fcntl>(fd, F_DUPFD_CLOEXEC, 0).value;
}

// -----------------------------------------------------------------------------

#define CORE_FD_LEAK_CHECK 1

namespace
{
    struct FdData
    {
        static constexpr u32 max_fds = core::fd::max_fds + 1;

        struct {
            std::array<core::Ref<core::FdListener>, max_fds> listeners  = {};
            std::array<u32,                   max_fds> ref_counts = {};
            std::array<bool,                  max_fds> no_close   = {};
#if CORE_FD_LEAK_CHECK
            std::array<bool,                  max_fds> inherited  = {};
#endif
        } data;

        core::Ref<core::FdListener>* listeners  = data.listeners.data()  + 1;
        u32*                   ref_counts = data.ref_counts.data() + 1;
        bool*                  no_close   = data.no_close.data()   + 1;

#if CORE_FD_LEAK_CHECK
        FdData()
        {
            for (int fd = 0; fd < core::fd::max_fds; ++fd) {
                if (fcntl(fd, F_GETFD) == 0) {
                    data.inherited[fd] = true;
                }
            }
        }

        ~FdData()
        {
            for (int fd = 0; fd < core::fd::max_fds; ++fd) {
                if (data.inherited[fd]) continue;
                if (fcntl(fd, F_GETFD) == -1) continue;

                log_error("fd[{}] leaked (refs: {})", fd, ref_counts[fd]);
            }
        }
#endif
    };

    FdData fds;
}

auto core::fd::get_ref_count(int fd) -> u32
{
    return fds.ref_counts[fd];
}

auto core::fd::get_listener(int fd) -> core::FdListener*
{
    return fds.listeners[fd].get();
}

void core::fd::set_listener(int fd, core::FdListener* listener)
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

auto core::fd::add_ref(int fd) -> int
{
    if (fd == -1) return -1;

    FD_LOG("core::fd::add_ref({}) {} -> {}", fd, fds.ref_counts[fd], fds.ref_counts[fd] + 1);
    fds.ref_counts[fd]++;
    return fd;
}

static
void destroy_fd(int fd)
{
    if (fds.listeners[fd]) {
        FD_LOG("  core::fd::remove_listener({})", fd);
        core::fd::remove_listener(fd);
    }

    if (!fds.no_close[fd]) {
        FD_LOG("  close({})", fd);
        core::check<close>(fd);
    } else {
        // Next
        fds.no_close[fd] = false;
    }
}

auto core::fd::remove_ref(int fd) -> int
{
    if (fd == -1) return -1;

    FD_LOG("core::fd::remove_ref({}) {} -> {}", fd, fds.ref_counts[fd], fds.ref_counts[fd] - 1);
    if (!--fds.ref_counts[fd]) {
        destroy_fd(fd);
        return -1;
    }

    return fd;
}

// -----------------------------------------------------------------------------

core::Fd core::fd::adopt(int fd)
{
    FD_LOG("core::fd::adopt({})", fd);
    core_assert(core::fd::get_ref_count(fd) == 0);
    core_assert(fds.no_close[fd] == false);
    return core::Fd(fd);
}

core::Fd core::fd::reference(int fd)
{
    FD_LOG("core::fd::reference({})", fd);
    core_assert(core::fd::get_ref_count(fd) == 0);
    core_assert(fds.no_close[fd] == false);
    fds.no_close[fd] = true;
    return core::Fd(fd);
}

core::Fd core::fd::dup(int fd)
{
    return fd >= 0 ? core::fd::adopt(core::fd::dup_unsafe(fd)) : core::Fd();
}
