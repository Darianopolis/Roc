import os
from pathlib import Path

def ensure_dir(path: Path | str) -> Path:
    os.makedirs(path, exist_ok=True)
    return Path(path)

def ensure_parent(path: Path | str) -> Path:
    path = Path(path)
    os.makedirs(path.parent, exist_ok=True)
    return path

def write_file_lazy(path: Path, data: str | bytes) -> bool:
    match data:
        case bytes(b):
            if not path.exists() or b != path.read_bytes():
                os.remove(path)
                path.write_bytes(b)
                return True
        case str(t):
            if not path.exists() or t != path.read_text():
                os.remove(path)
                path.write_text(t)
                return True
    return False
