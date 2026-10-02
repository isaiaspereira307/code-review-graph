"""ctypes marshalling for the C neighbourhood core.

Every function here mirrors the identically named function in
:mod:`code_review_graph.neighbourhood` and takes and returns the same Python
containers, so a parity test can call both and diff the results. No algorithm
lives in this layer: it only builds buffers, calls C, and reads results back.

Ids are assigned in first-seen order, matching Python dict insertion order, so
the two backends see the same node numbering.
"""

from __future__ import annotations

import ctypes
from contextlib import contextmanager
from typing import Iterable, Iterator, Sequence

from . import ctypes_loader

__all__ = [
    "NO_NODE",
    "build_adjacency",
    "hop_distances",
    "parent_files",
    "select_within_budget",
    "shortest_path",
]

#: Sentinel for "no parent"; mirrors ``CRG_NO_NODE``.
NO_NODE = 0xFFFFFFFF

_u8 = ctypes.c_uint8
_u32 = ctypes.c_uint32
_i32 = ctypes.c_int32
_size = ctypes.c_size_t


class _CGraph(ctypes.Structure):
    _fields_ = [
        ("names", ctypes.POINTER(ctypes.c_char_p)),
        ("node_count", _size),
        ("edge_source", ctypes.POINTER(_u32)),
        ("edge_target", ctypes.POINTER(_u32)),
        ("edge_kind", ctypes.POINTER(_u32)),
        ("edge_count", _size),
    ]


class _CAdjacency(ctypes.Structure):
    _fields_ = [
        ("offset", ctypes.POINTER(_size)),
        ("target", ctypes.POINTER(_u32)),
        ("present", ctypes.POINTER(_u8)),
        ("node_count", _size),
        ("row_length", ctypes.POINTER(_size)),
        ("row_length_final", ctypes.POINTER(_size)),
    ]


class _CSelection(ctypes.Structure):
    _fields_ = [
        ("selected", ctypes.POINTER(_u8)),
        ("out_hops", ctypes.POINTER(_i32)),
        ("selected_count", _size),
        ("truncated", ctypes.c_int),
    ]


# Edge kinds become small integers on the C side. The ids are per-process and
# never cross a boundary, so a plain first-come counter is enough.
_KIND_IDS: dict[str, int] = {}


def _kind_id(kind: str | None) -> int:
    """Intern an edge kind name into a stable per-process id."""
    ident = _KIND_IDS.get(kind)
    if ident is None:
        ident = len(_KIND_IDS)
        _KIND_IDS[kind] = ident
    return ident


_LIB = None


def _bind(lib):
    """Attach ctypes prototypes to every exported function we call.

    ``argtypes`` lives on the function-pointer object ctypes hands back from
    ``lib.<name>``, not on the ``CDLL`` itself, and a fresh ``CDLL`` yields a
    fresh pointer every time it is asked. So this must run against the one
    library object we keep -- see :func:`_lib`.
    """
    u32p = ctypes.POINTER(_u32)
    lib.crg_graph_init.argtypes = [
        ctypes.POINTER(_CGraph), ctypes.POINTER(ctypes.c_char_p), _size,
        u32p, u32p, u32p, _size,
    ]
    lib.crg_graph_init.restype = ctypes.c_int
    lib.crg_graph_clear.argtypes = [ctypes.POINTER(_CGraph)]
    lib.crg_graph_clear.restype = None
    lib.crg_graph_degrees.argtypes = [ctypes.POINTER(_CGraph), u32p]
    lib.crg_graph_degrees.restype = ctypes.c_int
    lib.crg_graph_parent_files.argtypes = [
        ctypes.POINTER(_CGraph), _u32, ctypes.POINTER(_u8), u32p,
    ]
    lib.crg_graph_parent_files.restype = ctypes.c_int
    lib.crg_adjacency_build.argtypes = [
        ctypes.POINTER(_CGraph), u32p, _size, u32p, _size, ctypes.c_int,
        ctypes.POINTER(_CAdjacency),
    ]
    lib.crg_adjacency_build.restype = ctypes.c_int
    lib.crg_adjacency_clear.argtypes = [ctypes.POINTER(_CAdjacency)]
    lib.crg_adjacency_clear.restype = None
    lib.crg_adjacency_hop_distances.argtypes = [
        ctypes.POINTER(_CAdjacency), u32p, _size, _i32,
        ctypes.POINTER(_i32), u32p, ctypes.POINTER(_size),
    ]
    lib.crg_adjacency_hop_distances.restype = ctypes.c_int
    lib.crg_graph_shortest_path.argtypes = [
        ctypes.POINTER(_CGraph), u32p, _size, _u32, _u32, u32p, _size,
        ctypes.POINTER(_size), ctypes.POINTER(ctypes.c_int),
    ]
    lib.crg_graph_shortest_path.restype = ctypes.c_int
    lib.crg_select_within_budget.argtypes = [
        ctypes.POINTER(_CGraph), ctypes.POINTER(_i32), u32p, u32p, _size,
        u32p, _size, ctypes.POINTER(_CSelection),
    ]
    lib.crg_select_within_budget.restype = ctypes.c_int
    return lib


def _lib():
    """The shared library, bound once and cached.

    Caching is load-bearing, not a micro-optimisation: prototypes set on a
    throwaway ``CDLL`` are discarded with it, and the next call would pass
    every pointer as a truncated ``int``.
    """
    global _LIB

    if _LIB is not None:
        return _LIB

    lib = ctypes_loader.load()
    if lib is None:
        raise RuntimeError(
            "the crg_c_core shared library is not built; run "
            "'cmake -S c_core -B c_core/build && cmake --build c_core/build'"
        )
    _LIB = _bind(lib)
    return _LIB


def _check(status: int) -> None:
    if status != 0:
        raise RuntimeError(f"crg_c_core call failed with status {status}")


def _u32_buffer(values: Sequence[int]):
    """A ``uint32_t[]`` holding *values*; always at least one element wide."""
    buffer = (_u32 * max(len(values), 1))()
    for slot, value in enumerate(values):
        buffer[slot] = value
    return buffer


class _Ids:
    """Interns names to ids in first-seen order, like a Python dict."""

    def __init__(self) -> None:
        self.names: list[str] = []
        self.index: dict[str, int] = {}

    def id(self, name: str) -> int:
        found = self.index.get(name)
        if found is None:
            found = len(self.names)
            self.index[name] = found
            self.names.append(name)
        return found


@contextmanager
def _graph(lib, ids: _Ids, edges: Sequence[tuple[int, int, int]]) -> Iterator[_CGraph]:
    """Build a ``crg_graph`` from interned names and (src, dst, kind) rows."""
    encoded = [name.encode("utf-8") for name in ids.names]
    # The array is borrowed for the graph's lifetime, so both it and the
    # bytes it points at have to outlive the with-block.
    name_array = (ctypes.c_char_p * max(len(encoded), 1))(*encoded)
    source_array = _u32_buffer([row[0] for row in edges])
    target_array = _u32_buffer([row[1] for row in edges])
    kind_array = _u32_buffer([row[2] for row in edges])
    graph = _CGraph()
    _check(lib.crg_graph_init(
        ctypes.byref(graph), name_array, len(ids.names),
        source_array, target_array, kind_array, len(edges),
    ))
    try:
        yield graph
    finally:
        lib.crg_graph_clear(ctypes.byref(graph))


def _kind_buffer(kinds: Sequence[str] | None):
    """``(buffer, count)`` for a kind set; ``None`` means "no restriction"."""
    if kinds is None:
        return None, 0
    return _u32_buffer([_kind_id(kind) for kind in kinds]), len(kinds)


def build_adjacency(edges: Iterable[dict], kinds: Sequence[str] | None = None, *,
                    exclude: Sequence[str] = (), directed: bool = False
                    ) -> dict[str, set[str]]:
    """Adjacency map over *edges*, restricted to *kinds* and minus *exclude*."""
    lib = _lib()
    ids = _Ids()
    rows: list[tuple[int, int, int]] = []

    for edge in edges:
        kind = edge.get("kind")
        source = edge.get("source")
        target = edge.get("target")
        if not source or not target:
            continue
        rows.append((
            ids.id(source), ids.id(target), _kind_id(kind)
        ))

    # An edge list with no usable edge is an empty map, not a caller error.
    if not ids.names:
        return {}

    allowed_buffer, allowed_count = _kind_buffer(kinds)
    excluded_buffer, excluded_count = _kind_buffer(tuple(exclude))
    adjacency = _CAdjacency()

    with _graph(lib, ids, rows) as graph:
        _check(lib.crg_adjacency_build(
            ctypes.byref(graph),
            allowed_buffer, allowed_count,
            excluded_buffer, excluded_count,
            1 if directed else 0,
            ctypes.byref(adjacency),
        ))
        try:
            result: dict[str, set[str]] = {}
            for index in range(len(ids.names)):
                if adjacency.present[index] == 0:
                    continue
                start = adjacency.offset[index]
                stop = adjacency.offset[index + 1]
                result[ids.names[index]] = {
                    ids.names[adjacency.target[slot]]
                    for slot in range(start, stop)
                }
            return result
        finally:
            lib.crg_adjacency_clear(ctypes.byref(adjacency))


def hop_distances(adjacency: dict[str, set[str]], seeds: Iterable[str],
                  depth: int) -> dict[str, int]:
    """Breadth-first hop distance from *seeds*, capped at *depth*."""
    lib = _lib()
    ids = _Ids()
    rows: list[tuple[int, int, int]] = []

    # hop_distances only ever reads adjacency.get(node, ()), so the rows are
    # directed; the traversal itself is what applies the undirected lookups.
    for source, members in adjacency.items():
        source_id = ids.id(source)
        for target in members:
            rows.append((source_id, ids.id(target), 0))
    seed_ids = [ids.id(seed) for seed in seeds]

    if not ids.names:
        return {}

    hops = (_i32 * max(len(ids.names), 1))()
    order = (_u32 * max(len(ids.names), 1))()
    reachable = _size()
    seed_buffer = _u32_buffer(seed_ids)
    adjacency_struct = _CAdjacency()

    with _graph(lib, ids, rows) as graph:
        _check(lib.crg_adjacency_build(
            ctypes.byref(graph), None, 0, None, 0, 1,
            ctypes.byref(adjacency_struct),
        ))
        try:
            _check(lib.crg_adjacency_hop_distances(
                ctypes.byref(adjacency_struct), seed_buffer, len(seed_ids),
                depth, hops, order, ctypes.byref(reachable),
            ))
            return {
                ids.names[index]: hops[index]
                for index in range(len(ids.names))
                if hops[index] >= 0
            }
        finally:
            lib.crg_adjacency_clear(ctypes.byref(adjacency_struct))


def shortest_path(edges: Iterable[dict], source: str, target: str,
                  kinds: Sequence[str]) -> tuple[list[str], bool]:
    """Shortest path from *source* to *target* through *kinds*."""
    lib = _lib()
    rows = list(edges)
    ids = _Ids()
    mapped: list[tuple[int, int, int]] = []

    for edge in rows:
        edge_source = edge.get("source")
        edge_target = edge.get("target")
        if not edge_source or not edge_target:
            continue
        mapped.append((
            ids.id(edge_source), ids.id(edge_target),
            _kind_id(edge.get("kind")),
        ))
    source_id = ids.id(source)
    target_id = ids.id(target)

    kind_buffer, kind_count = _kind_buffer(tuple(kinds))
    path = (_u32 * max(len(ids.names), 1))()
    length = _size()
    directed = ctypes.c_int()

    with _graph(lib, ids, mapped) as graph:
        _check(lib.crg_graph_shortest_path(
            ctypes.byref(graph), kind_buffer, kind_count,
            source_id, target_id, path, len(ids.names),
            ctypes.byref(length), ctypes.byref(directed),
        ))
        return (
            [ids.names[path[slot]] for slot in range(length.value)],
            bool(directed.value),
        )


def parent_files(edges: Iterable[dict], members: Iterable[str]
                 ) -> dict[str, str]:
    """Map member qualified name -> containing File qualified name."""
    lib = _lib()
    ids = _Ids()
    mapped: list[tuple[int, int, int]] = []
    member_set = set(members)

    for edge in edges:
        edge_source = edge.get("source")
        edge_target = edge.get("target")
        if not edge_source or not edge_target:
            continue
        mapped.append((
            ids.id(edge_source), ids.id(edge_target),
            _kind_id(edge.get("kind")),
        ))

    # Members that never appear in an edge still need a flag slot.
    for name in member_set:
        ids.id(name)

    if not ids.names:
        return {}

    flags = (_u8 * max(len(ids.names), 1))()
    for index, name in enumerate(ids.names):
        flags[index] = 1 if name in member_set else 0
    parents = (_u32 * max(len(ids.names), 1))()

    with _graph(lib, ids, mapped) as graph:
        _check(lib.crg_graph_parent_files(
            ctypes.byref(graph), _kind_id("CONTAINS"), flags, parents
        ))
        return {
            ids.names[index]: ids.names[parents[index]]
            for index in range(len(ids.names))
            if parents[index] != NO_NODE
        }


def select_within_budget(hops: dict[str, int], degrees: dict[str, int],
                         parents: dict[str, str], max_nodes: int,
                         pinned: Sequence[str] = ()
                         ) -> tuple[dict[str, int], bool]:
    """Apply the payload node budget; returns ``(selected, truncated)``."""
    lib = _lib()
    ids = _Ids()

    for name in hops:
        ids.id(name)
    for name, parent in parents.items():
        ids.id(name)
        ids.id(parent)
    pinned_ids = [ids.id(name) for name in pinned]

    if not ids.names:
        return {}, False

    count = len(ids.names)
    hop_buffer = (_i32 * max(count, 1))()
    degree_buffer = _u32_buffer([degrees.get(name, 0) for name in ids.names])
    parent_buffer = _u32_buffer([
        ids.index[parents[name]] if name in parents else NO_NODE
        for name in ids.names
    ])
    pinned_buffer = _u32_buffer(pinned_ids)
    selected = (_u8 * max(count, 1))()
    selected_hops = (_i32 * max(count, 1))()
    selection = _CSelection(selected, selected_hops, 0, 0)

    for index, name in enumerate(ids.names):
        hop_buffer[index] = hops.get(name, -1)

    with _graph(lib, ids, []) as graph:
        _check(lib.crg_select_within_budget(
            ctypes.byref(graph), hop_buffer, degree_buffer, parent_buffer,
            max_nodes, pinned_buffer, len(pinned_ids), ctypes.byref(selection),
        ))
        return (
            {
                ids.names[index]: selected_hops[index]
                for index in range(count)
                if selected[index]
            },
            bool(selection.truncated),
        )