#include "process.hpp"

#include "fd.hpp"
#include "enum.hpp"
#include "debug.hpp"

#include <core/log.hpp>
#include <core/stack.hpp>

#include <linux/sched.h>

struct SpawnActionDup2
{
    fd_t from, to;
    auto operator()() const { return unix_call<dup2>(from, to); }
};

struct SpawnActionClose
{
    fd_t fd;
    auto operator()() const { return unix_call<close>(fd); }
};

struct SpawnActionSetFdFlags
{
    fd_t fd; int flags;
    auto operator()() const { return unix_call<fcntl>(fd, F_SETFD, flags); }
};

using SpawnAction = std::variant<SpawnActionDup2,  SpawnActionClose, SpawnActionSetFdFlags>;

extern "C"
{
    auto spawn_clone3(clone_args* args, size_t size) -> pid_t;
}

static
auto spawn(
    fd_t exe,
    fd_t dir,
    char** argv,
    char** envp,
    u32 fd_lim_cur,
    u32 fd_lim_max,
    std::span<const SpawnAction> actions) -> UnixResult<Fd>
{
    int pidfd = -1;

    usz stack_size = 65'536;
    void* stack = unix_check<mmap>(nullptr, stack_size,
        PROT_READ | PROT_WRITE,
        MAP_PRIVATE | MAP_ANONYMOUS | MAP_STACK,
        -1, 0).value;
    defer { unix_check<munmap>(stack, stack_size); };

    // Prepopulate stack with child spawn function

    int error = 0;
#define SPAWN_FAIL return 127
#define SPAWN_TRY(...) if ((error = (__VA_ARGS__).error)) SPAWN_FAIL

    auto prepare_and_exec = [&] -> int {

        // Change working directory
        if (dir != -1) SPAWN_TRY(unix_call<fchdir>(dir));

        // Unblock all signals
        sigset_t mask;
        SPAWN_TRY(unix_call<sigfillset>(&mask));
        SPAWN_TRY(unix_call<sigprocmask>(SIG_UNBLOCK, &mask, nullptr));

        // Mark all file descriptors below inherited limit for close-on-exec
        SPAWN_TRY(unix_call<close_range>(0u, fd_lim_cur - 1, literal_cast<int>(CLOSE_RANGE_CLOEXEC)));

        // Apply file descriptor actions
        for (auto& action : actions) {
            SPAWN_TRY(std::visit([](auto&& op) { return op(); }, action));
        }

        // Close file descriptors above target limit
        SPAWN_TRY(unix_call<close_range>(fd_lim_cur, ~0u, 0));

        // Update NOFILE limit
        SPAWN_TRY(unix_call<setrlimit>(RLIMIT_NOFILE, ptr_to(rlimit{fd_lim_cur, fd_lim_max})));

        // Exec
        SPAWN_TRY(unix_call<execveat>(exe, "", argv, envp, AT_EMPTY_PATH));

        SPAWN_FAIL;
    };

    {
        u64* values = byte_offset_pointer<u64>(stack, stack_size);
        values[-2] = __u64(&prepare_and_exec);
        values[-1] = __u64(+[](void* data) -> int {
            return (*(decltype(prepare_and_exec)*)data)();
        });
    }

    pid_t pid = spawn_clone3(ptr_to( clone_args{
        .flags = CLONE_PIDFD
               | CLONE_VM
               | CLONE_VFORK,
        .pidfd = __u64(&pidfd),
        .exit_signal = SIGCHLD,
        .stack = __u64(stack),
        .stack_size = stack_size - 16,
    }), sizeof(clone_args));

    if (pid < 0) {
        return {{}, errno};
    } else if (pid) {
        return {Fd(pidfd), error};
    } else {
        // Child never returns from our `spawn_clone3` wrapper
        std::unreachable();
    }
}

static
auto generate_fd_actions(std::span<const SpawnFdInherit> remaps, fd_t free_slot) -> std::vector<SpawnAction>
{
    std::vector<SpawnAction> ops;

    struct Unresolved
    {
        fd_t parent;
        int blocker_count;
    };

    ankerl::unordered_dense::map<fd_t, Unresolved> unresolved;

    for (auto[parent, child] : remaps) {
        if (parent == child) {
            // Trivial inheritance case, drop close-on-exec flag
            ops.emplace_back(SpawnActionSetFdFlags{child, 0});
        } else {
            // Add to unresolved set
            auto res = unresolved.insert({child, {parent, 0}});
            debug_assert(res.second, "Spawn :: Duplicate remap target fd");
        }
    }

    // A given copy (A) is blocked by any other copy (B) where B.read == A.write
    for (auto[parent, child] : remaps) {
        if (parent == child) continue;

        auto blocked = unresolved.find(parent);
        if (blocked != unresolved.end()) {
            blocked->second.blocker_count++;
        }
    }

    std::deque<fd_t> to_unblock;

    // Enqueue any initial unblocked entries
    for (auto&[child, pending] : unresolved) {
        if (pending.blocker_count == 0) {
            to_unblock.emplace_back(child);
        }
    }

    fd_t scratch_content = -1;

    for (;;) {
        while (!to_unblock.empty()) {
            fd_t child = to_unblock.front();
            to_unblock.pop_front();

            fd_t parent = unresolved.at(child).parent;
            unresolved.erase(child);

            ops.emplace_back(SpawnActionDup2{parent == scratch_content ? free_slot : parent, child});

            auto blocked = unresolved.find(parent);
            if (blocked != unresolved.end()) {
                if (--blocked->second.blocker_count == 0) {
                    to_unblock.emplace_back(parent);
                }
            }
        }

        if (unresolved.empty()) break;

        // Arbitrarily pick the first unresolved copy to unblock
        auto first = unresolved.begin();
        auto&[child, pending] = *first;
        ops.emplace_back(SpawnActionDup2{pending.parent, free_slot});
        scratch_content = pending.parent;
        to_unblock.emplace_back(pending.parent);
    }

    if (scratch_content != -1) {
        ops.emplace_back(SpawnActionClose{free_slot});
    }

    return ops;
}

struct SpawnCStrs
{
    std::deque<std::string> strs;
    std::vector<char*> cstrs;

    SpawnCStrs(const auto& src)
    {
        for (const auto& str : src) {
            cstrs.emplace_back(strs.emplace_back(str).data());
        }
        cstrs.emplace_back(nullptr);
    }

    char** get()
    {
        return cstrs.data();
    }
};

auto spawn(const SpawnInfo& info) -> Fd
{
    for (auto& map : info.fds) {
        debug_assert(map.child < info.fd_limit,
            "spawn : FD mapping invalid : child FD {} must be lower than child FD limit {}", map.child, info.fd_limit);
    }

    // Map out protected file descriptors that we can't trample

    auto find_free_fd = [&](fd_t limit, const auto& ...used) {
        for (fd_t fd = 0; fd < limit; ++fd) {
            if ((... && !used.contains(fd))) {
                return fd;
            }
        }
        return -1;
    };

    std::vector<SpawnFdInherit> fd_map{info.fds.begin(), info.fds.end()};

    std::flat_set<fd_t> parent_fds;
    parent_fds.insert_range(info.fds | std::views::transform([](auto f) { return f.parent; }));

    std::flat_set<fd_t> child_fds;
    child_fds.insert_range(info.fds | std::views::transform([](auto f) { return f.child; }));

    // Allocate spot in child file descriptor space for executable

    fd_t exe_child = find_free_fd(info.fd_limit, child_fds);
    if (exe_child < 0) {
        log_error("Spawn : Failed to allocate fd for executable in child fd space");
        return {};
    }
    parent_fds.emplace(info.executable);
    child_fds.emplace(exe_child);
    fd_map.emplace_back(info.executable, exe_child);

    // Allocate scratch file descriptor for performing file actions

    fd_t scratch_fd = find_free_fd(fd_get_limits().current, child_fds, parent_fds);
    if (scratch_fd < 0) {
        log_error("Spawn : Failed to allocate scratch fd for fd actions");
        return {};
    }

    // Generate file actions

    auto actions = generate_fd_actions(fd_map, scratch_fd);
    actions.emplace_back(SpawnActionSetFdFlags{exe_child, FD_CLOEXEC});

    // Convert args and env to char** for exec

    SpawnCStrs args{info.args};
    SpawnCStrs envp{info.env | std::views::transform([](const auto& e) { return std::format("{}={}", e.first, e.second); })};

    // Spawn process

    auto res = spawn(
        /*        exe = */ exe_child,
        /*        dir = */ info.directory,
        /*       argv = */ args.get(),
        /*       envp = */ envp.get(),
        /* fd_lim_cur = */ num_cast<u32>(info.fd_limit),
        /* fd_lim_max = */ num_cast<u32>(fd_get_limits().max),
        /* fd_actions = */ actions);

    if (res.ok()) {
        return std::move(res.value);
    } else {
        log_error("Process spawn failed: ({} - {})", strerror(res.error), res.error);
        return {};
    }
}
