"""Locate and load the local ``c_core`` shared library via ctypes.

Discovery order (no absolute paths are ever hardcoded):

1. ``CRG_C_CORE_LIB`` -- explicit override, full path to the library file.
2. ``c_core/build/`` next to the repository root.
3. ``c_core/build/lib*/`` -- multi-config generators nest one level deeper.
4. ``build/lib*/`` and ``build/`` at the repository root.

The loader never raises on a missing library. :func:`load` returns ``None`` so
callers can fall back to the Python backend, which is the documented behaviour
while the C core is being brought up.
"""

from __future__ import annotations

import ctypes
import os
import sys
from pathlib import Path

__all__ = ["LIB_NAME", "candidate_paths", "load"]

LIB_NAME = "crg_c_core"

_ENV_OVERRIDE = "CRG_C_CORE_LIB"

_STEM = {
    "linux": f"lib{LIB_NAME}.so",
    "darwin": f"lib{LIB_NAME}.dylib",
    "win32": f"{LIB_NAME}.dll",
}


def _repo_root() -> Path:
    # .../<repo>/code_review_graph/_c_core/ctypes_loader.py -> repo root is 2 up.
    return Path(__file__).resolve().parents[2]


def _filename() -> str:
    return _STEM.get(sys.platform, _STEM["linux"])


def _search_dirs(root: Path) -> list[Path]:
    build = root / "c_core" / "build"
    dirs = [build]
    # Multi-config generators put the artifact under <build>/lib/<config>.
    for parent in (build, root / "build"):
        if parent.is_dir():
            dirs.extend(sorted(p for p in parent.glob("lib*") if p.is_dir()))
    if (root / "build").is_dir():
        dirs.append(root / "build")
    return dirs


def candidate_paths() -> list[Path]:
    """Every path that may hold the shared library, in priority order."""
    override = os.environ.get(_ENV_OVERRIDE)
    if override:
        return [Path(override)]

    filename = _filename()
    root = _repo_root()
    paths = [directory / filename for directory in _search_dirs(root)]
    # Deduplicate while preserving order; several glob hits can collapse here.
    return list(dict.fromkeys(path for path in paths if path.is_file()))


def load() -> ctypes.CDLL | None:
    """Return the loaded library, or ``None`` when it is not built yet."""
    for path in candidate_paths():
        try:
            return ctypes.CDLL(str(path))
        except OSError:
            # Wrong ABI or stale artifact: keep looking rather than guessing.
            continue
    return None
