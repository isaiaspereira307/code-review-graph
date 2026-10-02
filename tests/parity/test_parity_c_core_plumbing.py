"""Parity smoke tests for the C core plumbing.

These do not compare algorithm output yet -- they pin the contract the module
parity tests will rely on: the shared library is discoverable, the backend
flag round-trips, and an unbuilt library degrades to the Python path instead
of raising.
"""

from __future__ import annotations

import ctypes

import pytest

from code_review_graph import backend
from code_review_graph._c_core import ctypes_loader


@pytest.fixture(autouse=True)
def _reset_backend():
    backend.set_backend(None)
    yield
    backend.set_backend(None)


def test_c_library_is_discoverable_after_build():
    paths = ctypes_loader.candidate_paths()

    if not paths:
        pytest.skip("c_core not built; run cmake --build c_core/build")

    assert paths[0].is_file()
    assert "crg_c_core" in paths[0].name


def test_c_library_exposes_the_version_symbol():
    lib = ctypes_loader.load()
    if lib is None:
        pytest.skip("c_core not built; run cmake --build c_core/build")

    lib.crg_c_core_version.restype = ctypes.c_char_p

    assert lib.crg_c_core_version() == b"0.1.0"


def test_env_var_selects_the_backend(monkeypatch):
    monkeypatch.setenv("CRG_BACKEND", "c")

    assert backend.get_backend() == "c"
    assert backend.is_c_backend() is True


def test_default_backend_is_python(monkeypatch):
    monkeypatch.delenv("CRG_BACKEND", raising=False)

    assert backend.get_backend() == backend.DEFAULT_BACKEND == "python"
    assert backend.is_c_backend() is False


def test_process_override_beats_the_environment(monkeypatch):
    monkeypatch.setenv("CRG_BACKEND", "c")

    assert backend.set_backend("python") == "python"
    assert backend.set_backend(None) == "c"


def test_unknown_backend_falls_back_to_python(monkeypatch):
    monkeypatch.setenv("CRG_BACKEND", "rust")

    assert backend.get_backend() == "python"


def test_missing_library_is_not_fatal(monkeypatch, tmp_path):
    monkeypatch.setenv("CRG_C_CORE_LIB", str(tmp_path / "absent.so"))

    assert ctypes_loader.candidate_paths() == [tmp_path / "absent.so"]
    assert ctypes_loader.load() is None
    assert backend.available() is False


def test_explicit_override_wins_over_discovery(monkeypatch, tmp_path):
    default_candidates = ctypes_loader.candidate_paths()
    monkeypatch.setenv("CRG_C_CORE_LIB", str(tmp_path / "explicit.so"))

    assert ctypes_loader.candidate_paths() == [tmp_path / "explicit.so"]
    monkeypatch.delenv("CRG_C_CORE_LIB")

    assert ctypes_loader.candidate_paths() == default_candidates


def test_loader_never_returns_duplicates():
    paths = ctypes_loader.candidate_paths()

    assert len(paths) == len(set(paths))
