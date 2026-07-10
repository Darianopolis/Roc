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
    auto operator()() const { return unix_check<dup2>(from, to); }
};

struct SpawnActionClose
{
    fd_t fd;
    auto operator()() const { return unix_check<close>(fd); }
};

struct SpawnActionSetFdFlags
{
    fd_t fd; int flags;
    auto operator()() const { return unix_check<fcntl>(fd, F_SETFD, flags); }
};

using SpawnAction = std::variant<SpawnActionDup2,  SpawnActionClose, SpawnActionSetFdFlags>;

extern "C"
{
    auto spawn_clone3(clone_args* args, size_t size) -> pid_t;
}

static
auto spawn(
    fd_t exec_file,
    char** argv,
    char** env,
    fd_t dir,
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
#define SPAWN_TRY(...) if ((error = (__VA_ARGS__).error)) goto spawn_failure

    auto prepare_and_exec = [&] -> int {
        // Change working directory
        if (dir != -1) SPAWN_TRY(unix_check<fchdir>(dir));

        // Unblock any signals currently blocked for signalfd handling purposes
        sigset_t mask;
        SPAWN_TRY(unix_check<sigfillset>(&mask));
        SPAWN_TRY(unix_check<sigprocmask>(SIG_UNBLOCK, &mask, nullptr));

        // Apply file descriptor actions
        for (auto& action : actions) {
            SPAWN_TRY(std::visit([](auto&& op) { return op(); }, action));
        }

        SPAWN_TRY(unix_check<execveat>(exec_file, "", argv, env, AT_EMPTY_PATH));

    spawn_failure:
        return 127;
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
