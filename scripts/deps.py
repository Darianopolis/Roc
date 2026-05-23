import json
import subprocess
from pathlib import Path
from .utils import write_file_lazy, ensure_parent

def stamp_path(vendor_dir: Path, name: str) -> Path:
    return vendor_dir / f"{name}.stamp"

def read_stamp(vendor_dir: Path, name: str) -> str | None:
    p = stamp_path(vendor_dir, name)
    return p.read_text().strip() if p.exists() else None

def write_stamp(vendor_dir: Path, name: str, commit: str):
    write_file_lazy(ensure_parent(stamp_path(vendor_dir, name)), commit)

def git(cmds, cwd=None):
    print(f"git: {cmds}")
    return subprocess.run(["git"] + cmds, cwd=cwd)

def fetch_dep(dir: Path, repo: str, branch: str, commit: str|None) -> str:

    # Grab dependency if not exists
    if not dir.exists():
        git(["clone", repo, "--branch", branch, "--depth", "1", "--recursive", str(dir)])

    # Update to exact commit or latest on branch
    if commit:
        git(["fetch", "--depth", "1", "origin", commit], cwd=dir)
        git(["checkout", "--force", commit], cwd=dir)
    else:
        git(["fetch", "--depth", "1", "origin", branch], cwd=dir)
        git(["reset", "--hard", f"origin/{branch}"],  cwd=dir)

    # Update submodules
    git(["submodule", "update", "--init", "--recursive"], cwd=dir)

    # Check actual commit
    return subprocess.run(
        ["git", "rev-parse", "HEAD"], cwd=dir, stdout=subprocess.PIPE,
    ).stdout.decode().strip()

def fetch_deps(
    vendor_dir: Path,
    build_data_path: Path,
    do_update: bool,
) -> dict[str, Path]:

    with build_data_path.open("r", encoding="utf-8") as f:
        lock = json.load(f)

    dep_dirs: dict[str, Path] = {}
    deps = lock["dependencies"]

    for name, entry in deps.items():
        dir = vendor_dir / name
        dep_dirs[name] = dir

        repo   = entry["repo"]
        branch = entry["branch"]
        commit = entry.get("commit") if not do_update else None

        previous_commit = read_stamp(vendor_dir, name)

        if commit and previous_commit == commit and dir.exists():
            continue

        if commit:
            print(f"[{name}] Switching from '{previous_commit[:12] if previous_commit else "INVALID"}' to '{commit[:12]}'")
        else:
            print(f"[{name}] Updating to latest on '{branch}'")

        head = fetch_dep(dir, repo, branch, commit)

        write_stamp(vendor_dir, name, head)

        if commit and head != commit:
            print(f"ERROR: [{name}] expected {commit[:12]} but HEAD is {head[:12]}")
        else:
            entry["commit"] = head

    write_file_lazy(build_data_path, json.dumps(lock, indent=4, sort_keys=True))

    return dep_dirs
