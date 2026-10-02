"""Runtime selection of the graph/analysis backend.

``CRG_BACKEND=python|c`` chooses the default; :func:`set_backend` overrides it
for the running process. Either way the *default* stays ``python`` so the C
path can never become the only working path before its parity tests are green.
"""

from __future__ import annotations

import os

__all__ = [
    "BACKENDS",
    "DEFAULT_BACKEND",
    "ENV_VAR",
    "available",
    "get_backend",
    "is_c_backend",
    "set_backend",
]

ENV_VAR = "CRG_BACKEND"
BACKENDS = ("python", "c")
DEFAULT_BACKEND = "python"

_backend: str | None = None


def _normalise(value: str) -> str:
    candidate = value.strip().lower()
    return candidate if candidate in BACKENDS else DEFAULT_BACKEND


def get_backend() -> str:
    """Active backend name; the process override wins over the environment."""
    if _backend is not None:
        return _backend
    return _normalise(os.environ.get(ENV_VAR, ""))


def set_backend(backend: str | None) -> str:
    """Pin the backend for this process, or clear the pin with ``None``."""
    global _backend
    _backend = None if backend is None else _normalise(backend)
    return get_backend()


def is_c_backend() -> bool:
    return get_backend() == "c"


def available() -> bool:
    """True when the C library can actually be loaded."""
    from ._c_core import ctypes_loader

    return ctypes_loader.load() is not None
