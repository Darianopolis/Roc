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
    void operator()() const { dup2(from, to); }
};

struct SpawnActionClose
{
    fd_t fd;
    void operator()() const { close(fd); }
};

struct SpawnActionSetFdFlags
{
    fd_t fd; int flags;
    void operator()() const { fcntl(fd, F_SETFD, flags); }
};

using SpawnAction = std::variant<SpawnActionDup2,  SpawnActionClose, SpawnActionSetFdFlags>;

static
auto spawn(
    fd_t exec_file,
    char** argv,
    char** env,
    fd_t dir,
    std::span<const SpawnAction> actions) -> UnixResult<Fd>
{
    int pidfd = -1;
    pid_t pid = pid_t(syscall(SYS_clone3, ptr_to(clone_args {
        .flags       = CLONE_PIDFD | CLONE_CLEAR_SIGHAND,
        .pidfd       = __u64(&pidfd),
        .exit_signal = SIGCHLD,
    }), sizeof(clone_args)));

    if (pid < 0) {
        return {{}, errno};
    } else if (pid) {
        return {Fd(pidfd)};
    } else {
        // Change working directory
        if (dir != -1) fchdir(dir);

        // Unblock any signals currently blocked for signalfd handling purposes
        sigset_t mask;
        sigfillset(&mask);
        sigprocmask(SIG_UNBLOCK, &mask, nullptr);

        // Apply file descriptor actions
        for (auto& action : actions) {
            std::visit([](auto&& op) { op(); }, action);
        }

        execveat(exec_file, "", argv, env, AT_EMPTY_PATH);

        // Exec failed
        _exit(127);
    }
}

static
auto generate_fd_actions(std::span<const SpawnFdInherit> remaps, fd_t free_slot) -> std::vector<SpawnAction>
{
    std::vector<SpawnAction> ops;

    bool free_slot_used = false;

    std::unordered_map<int, int> mapping;
    for (auto& m : remaps) mapping[m.parent] = m.child;

    while (!mapping.empty()) {
        auto[cycle_start, next] = *mapping.begin();

        if (cycle_start == next) {
            // Identity, simply mark inherited
            ops.push_back(SpawnActionSetFdFlags{cycle_start, 0});
            mapping.erase(cycle_start);
            continue;
        }

        std::vector<int> elements{cycle_start, next};
        while (next != cycle_start && mapping.contains(next)) {
            elements.emplace_back((next = mapping.at(next)));
        }

        if (next == cycle_start) {
            ops.push_back(SpawnActionDup2{next, free_slot});
            elements[0] = free_slot;
            free_slot_used = true;
        }

        for (int i = elements.size() - 1; i --> 0;) {
            ops.push_back(SpawnActionDup2{elements[i], elements[i + 1]});
        }

        for (auto e : elements) {
            mapping.erase(e);
        }
    }

    if (free_slot_used) {
        ops.push_back(SpawnActionClose{free_slot});
    }

    return ops;
}

auto spawn(
    fd_t exe,
    std::span<const std::string_view> args,
    const Environment* env,
    std::span<const SpawnFdInherit> fds) -> Fd
{
    // Generate file descriptor actions

    std::flat_set<fd_t> parent_fds;
    std::flat_set<fd_t> child_fds;
    parent_fds.insert_range(fds | std::views::transform([](auto f) { return f.parent; }));
    child_fds.insert_range( fds | std::views::transform([](auto f) { return f.child;  }));
    child_fds.emplace(exe);

    auto allocate_fd_slot = [&] {
        for (int i = fd_limit; i --> 0;) {
            if (!child_fds.contains(i) && !parent_fds.contains(i)) {
                child_fds.emplace(i);
                return i;
            }
        }

        debug_assert_fail("allocate_fd_slot", "File descriptors exhausted!");
    };

    auto actions = generate_fd_actions(fds, allocate_fd_slot());

    for (fd_t fd : {STDIN_FILENO, STDOUT_FILENO, STDERR_FILENO}) {
        if (!child_fds.contains(fd)) actions.emplace_back(SpawnActionClose{fd});
    }

    // Convert args and env to char** for exec

    std::vector<std::string> strs;
    std::vector<char*> cstrs;

    strs.reserve( args.size() + (env ? env->entries.size() : 0));
    cstrs.reserve(args.size() + (env ? env->entries.size() : 0) + 2);

    for (auto& a : args) {
        cstrs.emplace_back(strs.emplace_back(a).data());
    }
    cstrs.emplace_back(nullptr);

    char** argp = cstrs.data();
    char** envp;

    if (env) {
        envp = cstrs.data() + cstrs.size();
        for (auto[key, value] : env->entries) {
            cstrs.emplace_back(strs.emplace_back(std::format("{}={}", key, value)).data());
        }
        cstrs.emplace_back(nullptr);
    } else {
        envp = environ;
    }

    // Spawn process

    auto res = spawn(exe, argp, envp, env ? env->dir.get() : -1, actions);

    if (res.ok()) {
        return std::move(res.value);
    } else {
        log_error("Process spawn failed: ({} - {})", strerror(res.error), res.error);
        return {};
    }
}
