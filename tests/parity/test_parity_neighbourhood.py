"""Parity tests for the C neighbourhood core.

Each test runs the Python implementation and the C implementation over the
*same* input and requires identical results. The generators below lean on
randomised graphs because these functions are pure and total: a mismatch is a
semantic divergence, not a timing artefact, so more shapes strictly increases
coverage without adding flakiness.

The C library is skipped, not failed, when it has not been built -- the default
backend stays Python and the suite must stay green on a fresh checkout.
"""

from __future__ import annotations

import random

import pytest

from code_review_graph import backend
from code_review_graph import neighbourhood as py
from code_review_graph._c_core import bindings_neighbourhood as c
from code_review_graph._c_core import ctypes_loader

pytestmark = pytest.mark.skipif(
    ctypes_loader.load() is None,
    reason="c_core not built; run cmake --build c_core/build",
)

ALL_KINDS = ("CALLS", "IMPORTS_FROM", "INHERITS", "IMPLEMENTS", "CONTAINS")


def _random_graph(rng: random.Random, name_count: int, edge_count: int):
    """Random names and a matching edge list."""
    names = [f"pkg/mod{i}.py::sym{i}" for i in range(name_count)]
    edges = []
    for _ in range(edge_count):
        source = names[rng.randrange(name_count)]
        target = names[rng.randrange(name_count)]
        edges.append({
            "source": source,
            "target": target,
            "kind": rng.choice(ALL_KINDS),
        })
    return names, edges


def _export_graph(seed: int):
    """A shape ``extract()`` accepts: nodes, edges, and the nested CONTAINS."""
    rng = random.Random(seed)
    names, edges = _random_graph(rng, rng.randrange(4, 14), rng.randrange(2, 30))
    nodes = [
        {
            "id": index,
            "qualified_name": name,
            "name": name.split("::")[-1],
            "file_path": f"pkg/mod{index}.py",
            "kind": "Symbol",
        }
        for index, name in enumerate(names)
    ]
    for index in range(0, len(nodes), 3):
        file_node = {
            "id": 1000 + index,
            "qualified_name": nodes[index]["file_path"],
            "name": nodes[index]["file_path"],
            "kind": "File",
        }
        nodes.append(file_node)
        edges.append({
            "source": file_node["qualified_name"],
            "target": nodes[index]["qualified_name"],
            "kind": "CONTAINS",
        })
    return {"nodes": nodes, "edges": edges, "flows": [], "stats": {}}


def test_extract_end_to_end_parity():
    """``extract()`` must return the same payload on either backend."""
    data = _export_graph(11)
    spec = py.NeighbourhoodSpec(
        symbols=["sym0"], depth=2, render_depth=1, max_nodes=12
    )

    backend.set_backend(None)
    expected = py.extract(data, spec)

    backend.set_backend("c")
    try:
        assert backend.available()
        assert py.extract(data, spec) == expected
    finally:
        backend.set_backend(None)


def test_extract_path_query_parity():
    """The path branch runs before the neighbourhood and pins its own answer."""
    data = _export_graph(5)
    spec = py.NeighbourhoodSpec(
        path_from="sym0", path_to="sym3", depth=2, max_nodes=20
    )

    backend.set_backend(None)
    expected = py.extract(data, spec)

    backend.set_backend("c")
    try:
        if not backend.available():
            pytest.skip("c_core not built")
        assert py.extract(data, spec) == expected
    finally:
        backend.set_backend(None)


@pytest.mark.parametrize("seed", range(20))
@pytest.mark.parametrize("directed", [False, True])
def test_build_adjacency_parity(seed, directed):
    rng = random.Random(seed)
    names, edges = _random_graph(rng, rng.randrange(2, 14), rng.randrange(0, 30))
    kinds = rng.choice((None, ("CALLS",), ("CALLS", "CONTAINS")))

    assert c.build_adjacency(edges, kinds, exclude=("CONTAINS",),
                             directed=directed) == py.build_adjacency(
        edges, kinds, exclude=("CONTAINS",), directed=directed)


@pytest.mark.parametrize("seed", range(20))
def test_build_adjacency_excludes_parity(seed):
    rng = random.Random(seed)
    _, edges = _random_graph(rng, 8, rng.randrange(0, 25))

    assert c.build_adjacency(edges, exclude=ALL_KINDS) == py.build_adjacency(
        edges, exclude=ALL_KINDS)


@pytest.mark.parametrize("seed", range(20))
def test_hop_distances_parity(seed):
    rng = random.Random(seed)
    names, edges = _random_graph(rng, rng.randrange(2, 14), rng.randrange(1, 30))
    adjacency = py.build_adjacency(edges, exclude=py.STRUCTURAL_EDGE_KINDS)
    seed_count = rng.randrange(0, 4)
    seeds = [rng.choice(names) for _ in range(seed_count)]
    depth = rng.randrange(0, 5)

    assert c.hop_distances(adjacency, seeds, depth) == py.hop_distances(
        adjacency, seeds, depth)


@pytest.mark.parametrize("seed", range(20))
def test_hop_distances_directed_parity(seed):
    rng = random.Random(seed)
    names, edges = _random_graph(rng, rng.randrange(2, 14), rng.randrange(1, 30))
    adjacency = py.build_adjacency(edges, directed=True)
    seeds = [rng.choice(names) for _ in range(rng.randrange(1, 4))]
    depth = rng.randrange(0, 6)

    assert c.hop_distances(adjacency, seeds, depth) == py.hop_distances(
        adjacency, seeds, depth)


@pytest.mark.parametrize("seed", range(20))
@pytest.mark.parametrize("kinds", [py.PATH_EDGE_KINDS, ALL_KINDS, ("CALLS",)])
def test_shortest_path_parity(seed, kinds):
    rng = random.Random(seed)
    names, edges = _random_graph(rng, rng.randrange(2, 14), rng.randrange(1, 30))
    source = rng.choice(names)
    target = rng.choice(names)

    assert c.shortest_path(edges, source, target, kinds) == py.shortest_path(
        edges, source, target, kinds)


@pytest.mark.parametrize("seed", range(20))
def test_parent_files_parity(seed):
    rng = random.Random(seed)
    names, edges = _random_graph(rng, rng.randrange(2, 14), rng.randrange(0, 25))
    members = {name for name in names if rng.random() < 0.6}

    assert c.parent_files(edges, members) == py._parent_files(edges, members)


@pytest.mark.parametrize("seed", range(20))
def test_select_within_budget_parity(seed):
    rng = random.Random(seed)
    names, edges = _random_graph(rng, rng.randrange(2, 14), rng.randrange(0, 30))

    degrees: dict[str, int] = {}
    for edge in edges:
        degrees[edge["source"]] = degrees.get(edge["source"], 0) + 1
        degrees[edge["target"]] = degrees.get(edge["target"], 0) + 1

    adjacency = py.build_adjacency(edges, exclude=py.STRUCTURAL_EDGE_KINDS)
    seeds = [rng.choice(names) for _ in range(rng.randrange(0, 4))]
    hops = py.hop_distances(adjacency, seeds, rng.randrange(0, 5))
    parents = py._parent_files(edges, set(hops))
    pinned = [rng.choice(names) for _ in range(rng.randrange(0, 3))]
    max_nodes = rng.randrange(1, 8)

    assert c.select_within_budget(hops, degrees, parents, max_nodes,
                                  pinned) == py._select_within_budget(
        hops, degrees, parents, max_nodes, pinned=pinned)


@pytest.mark.parametrize("seed", range(15))
def test_select_within_budget_starved_parity(seed):
    """max_nodes of 1 is where truncation and the parent give-up branch show."""
    rng = random.Random(seed)
    names, edges = _random_graph(rng, rng.randrange(2, 10), rng.randrange(1, 20))

    degrees: dict[str, int] = {}
    for edge in edges:
        degrees[edge["source"]] = degrees.get(edge["source"], 0) + 1
        degrees[edge["target"]] = degrees.get(edge["target"], 0) + 1

    hops = py.hop_distances(
        py.build_adjacency(edges, exclude=py.STRUCTURAL_EDGE_KINDS),
        [rng.choice(names)], rng.randrange(0, 4))
    parents = py._parent_files(edges, set(hops))

    assert c.select_within_budget(hops, degrees, parents, 1) == \
        py._select_within_budget(hops, degrees, parents, 1)


def test_empty_inputs_match_python():
    """Empty input is an empty result, not a C-side caller error."""
    assert c.build_adjacency([]) == py.build_adjacency([])
    assert c.build_adjacency([{"source": None, "target": "b", "kind": "CALLS"}]) \
        == py.build_adjacency([{"source": None, "target": "b", "kind": "CALLS"}])
    assert c.hop_distances({}, [], 2) == py.hop_distances({}, [], 2)
    assert c.parent_files([], set()) == py._parent_files([], set())
    assert c.select_within_budget({}, {}, {}, 3) == \
        py._select_within_budget({}, {}, {}, 3)


def test_repeated_calls_do_not_depend_on_call_order():
    """Guards the cached-library prototypes: every call must see the same ABI.

    ctypes keeps ``argtypes`` on the per-access function pointer, so a library
    object that is loaded, bound, and then dropped would silently pass every
    pointer as a truncated int on the next load.
    """
    names, edges = _random_graph(random.Random(7), 6, 12)
    expected = py.build_adjacency(edges, exclude=py.STRUCTURAL_EDGE_KINDS)

    for _ in range(5):
        assert c.build_adjacency(edges, exclude=py.STRUCTURAL_EDGE_KINDS) \
            == expected


def test_seeds_outside_the_adjacency_still_get_hop_zero():
    """Python seeds hops at 0 for every seed, touched by an edge or not."""
    adjacency = py.build_adjacency(
        [{"source": "a", "target": "b", "kind": "CALLS"}])
    seeds = ["a", "nowhere"]

    assert c.hop_distances(adjacency, seeds, 2) == py.hop_distances(
        adjacency, seeds, 2)


@pytest.mark.parametrize("seed", range(20))
def test_combined_pipeline_parity(seed):
    """Run build -> hops -> parents -> budget the way extract() does.

    Both pipelines see the same seeds and the same graph, so the only
    difference is which backend computed each stage.
    """
    rng = random.Random(seed)
    names, edges = _random_graph(rng, rng.randrange(3, 16), rng.randrange(1, 35))

    seeds = [rng.choice(names) for _ in range(rng.randrange(1, 3))]
    depth = 2
    max_nodes = 5

    degrees: dict[str, int] = {}
    for edge in edges:
        degrees[edge["source"]] = degrees.get(edge["source"], 0) + 1
        degrees[edge["target"]] = degrees.get(edge["target"], 0) + 1

    py_adjacency = py.build_adjacency(edges, exclude=py.STRUCTURAL_EDGE_KINDS)
    py_hops = py.hop_distances(py_adjacency, seeds, depth)
    py_parents = py._parent_files(edges, set(py_hops))
    py_result = py._select_within_budget(
        py_hops, degrees, py_parents, max_nodes)

    c_adjacency = c.build_adjacency(edges, exclude=py.STRUCTURAL_EDGE_KINDS)
    c_hops = c.hop_distances(c_adjacency, seeds, depth)
    c_parents = c.parent_files(edges, set(c_hops))
    c_result = c.select_within_budget(c_hops, degrees, c_parents, max_nodes)

    assert c_adjacency == py_adjacency
    assert c_hops == py_hops
    assert c_parents == py_parents
    assert c_result == py_result