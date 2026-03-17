import json
import subprocess
from pathlib import Path

def run(cmds, cwd=None):
    print(cmds)
    subprocess.run(cmds, cwd=cwd)

def stamp_path(vendor_dir: Path, name: str) -> Path:
    return vendor_dir / f"{name}.stamp"

def read_stamp(vendor_dir: Path, name: str) -> str | None:
    p = stamp_path(vendor_dir, name)
    return p.read_text().strip() if p.exists() else None

def write_stamp(vendor_dir: Path, name: str, commit: str):
    vendor_dir.mkdir(parents=True, exist_ok=True)
    p = stamp_path(vendor_dir, name)
    if not p.exists() or p.read_text().strip() != commit:
        p.write_text(commit)

def fetch_dep(dir: Path, repo: str, branch: str, commit: str) -> str:

    # Grab dependency if not exists
    if not dir.exists():
        run(["git", "clone", repo, "--branch", branch, "--depth", "1", "--recursive", str(dir)])

    # Update to exact commit or latest on branch
    if commit:
        run(["git", "fetch", "--depth", "1", "origin", commit], cwd=dir)
        run(["git", "checkout", commit], cwd=dir)
    else:
        run(["git", "fetch", "--depth", "1", "origin", branch], cwd=dir)
        run(["git", "checkout", branch],  cwd=dir)
        run(["git", "pull", "--ff-only"], cwd=dir)

    # Update submodules
    run(["git", "submodule", "update", "--init", "--recursive"], cwd=dir)

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

    data = json.dumps(lock, indent=4, sort_keys=True)
    if not build_data_path.exists() or build_data_path.read_text() != data:
        build_data_path.write_text(data)

    return dep_dirs
