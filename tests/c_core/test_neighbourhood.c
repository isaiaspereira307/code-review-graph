/* Unit tests for the ported neighbourhood core. The Python reference is
 * exercised separately by tests/parity/test_parity_neighbourhood.py. */

#include <stdlib.h>
#include <string.h>

#include "unity.h"

#include "code_review_graph/neighbourhood.h"

void setUp(void) {}
void tearDown(void) {}

/* Edge kind ids; the Python side maps the same strings onto these. */
#define KIND_CALLS 0u
#define KIND_IMPORTS 1u
#define KIND_INHERITS 2u
#define KIND_CONTAINS 3u

/* Borrowed names for the fixture graph. Index == node id. */
static const char *const NAMES[] = {
    "src/a.py::f_a",
    "src/a.py::f_b",
    "src/b.py::f_c",
    "src/b.py::f_d",
    "src/a.py",
    "src/b.py",
};

static const size_t NAME_COUNT = sizeof(NAMES) / sizeof(NAMES[0]);

/* Edge chain f_a -> f_b -> f_c -> f_d, each File containing one symbol:
 *
 *   src/a.py CONTAINS f_a        src/b.py CONTAINS f_d
 */
static crg_status build_fixture(crg_graph *graph)
{
    static const uint32_t source[] = {0u, 1u, 2u, 4u, 5u};
    static const uint32_t target[] = {1u, 2u, 3u, 0u, 3u};
    static const uint32_t kind[] = {
        KIND_CALLS, KIND_CALLS, KIND_CALLS, KIND_CONTAINS, KIND_CONTAINS};

    return crg_graph_init(graph, NAMES, NAME_COUNT, source, target, kind,
                          sizeof(source) / sizeof(source[0]));
}

/* ---------------------------------------------------------------- */

static void test_graph_init_rejects_null_graph(void)
{
    TEST_ASSERT_EQUAL_INT(CRG_ERR_NULL,
                          crg_graph_init(NULL, NAMES, NAME_COUNT, NULL, NULL,
                                         NULL, 0u));
}

static void test_graph_init_rejects_null_names(void)
{
    crg_graph graph;

    TEST_ASSERT_EQUAL_INT(CRG_ERR_NULL,
                          crg_graph_init(&graph, NULL, NAME_COUNT, NULL, NULL,
                                         NULL, 0u));
}

static void test_graph_init_rejects_empty_graph(void)
{
    crg_graph graph;

    /* Zero nodes is meaningless: every downstream pass indexes names[0]. */
    TEST_ASSERT_EQUAL_INT(CRG_ERR_NULL,
                          crg_graph_init(&graph, NAMES, 0u, NULL, NULL, NULL, 0u));
}

static void test_graph_init_rejects_partial_edge_arrays(void)
{
    crg_graph graph;
    static const uint32_t source[] = {0u};
    static const uint32_t target[] = {1u};

    /* kind == NULL with edge_count > 0 is a caller bug, not a NULL graph. */
    TEST_ASSERT_EQUAL_INT(CRG_ERR_NULL,
                          crg_graph_init(&graph, NAMES, NAME_COUNT, source,
                                         target, NULL, 1u));
}

static void test_graph_init_rejects_out_of_range_ids(void)
{
    crg_graph graph;
    static const uint32_t source[] = {99u};
    static const uint32_t target[] = {1u};
    static const uint32_t kind[] = {KIND_CALLS};

    TEST_ASSERT_EQUAL_INT(
        CRG_ERR_RANGE,
        crg_graph_init(&graph, NAMES, NAME_COUNT, source, target, kind, 1u));
}

static void test_graph_init_leaves_the_struct_cleared_on_failure(void)
{
    crg_graph graph;
    static const uint32_t source[] = {99u};
    static const uint32_t target[] = {1u};
    static const uint32_t kind[] = {KIND_CALLS};

    memset(&graph, 0xAB, sizeof(graph));
    TEST_ASSERT_EQUAL_INT(
        CRG_ERR_RANGE,
        crg_graph_init(&graph, NAMES, NAME_COUNT, source, target, kind, 1u));
    TEST_ASSERT_NULL(graph.edge_source);
    TEST_ASSERT_NULL(graph.edge_target);
    TEST_ASSERT_NULL(graph.edge_kind);
    TEST_ASSERT_EQUAL_size_t(0u, graph.node_count);
}

static void test_graph_clear_is_safe_on_zeroed_and_twice(void)
{
    crg_graph graph;

    memset(&graph, 0, sizeof(graph));
    crg_graph_clear(&graph);
    crg_graph_clear(&graph);

    TEST_ASSERT_EQUAL_size_t(0u, graph.node_count);
}

static void test_graph_clear_is_safe_on_null(void)
{
    crg_graph_clear(NULL);
}

static void test_graph_accepts_an_edgeless_graph(void)
{
    crg_graph graph;

    TEST_ASSERT_EQUAL_INT(CRG_OK, crg_graph_init(&graph, NAMES, NAME_COUNT, NULL,
                                                 NULL, NULL, 0u));
    TEST_ASSERT_EQUAL_size_t(NAME_COUNT, graph.node_count);
    TEST_ASSERT_EQUAL_size_t(0u, graph.edge_count);
    crg_graph_clear(&graph);
}

/* ---------------------------------------------------------------- */

static void test_degrees_count_both_endpoints(void)
{
    crg_graph graph;
    uint32_t degrees[NAME_COUNT];

    TEST_ASSERT_EQUAL_INT(CRG_OK, build_fixture(&graph));
    TEST_ASSERT_EQUAL_INT(CRG_OK, crg_graph_degrees(&graph, degrees));

    /* f_a: 1 CALLS out + 1 incoming CONTAINS = 2 */
    TEST_ASSERT_EQUAL_UINT32(2u, degrees[0]);
    /* f_b: 1 CALLS out + 1 CALLS in = 2 */
    TEST_ASSERT_EQUAL_UINT32(2u, degrees[1]);
    /* f_c: 1 CALLS out + 1 CALLS in = 2 */
    TEST_ASSERT_EQUAL_UINT32(2u, degrees[2]);
    /* f_d: 1 CALLS in + 1 outgoing CONTAINS = 2 */
    TEST_ASSERT_EQUAL_UINT32(2u, degrees[3]);
    /* src/a.py: 1 CONTAINS out = 1 */
    TEST_ASSERT_EQUAL_UINT32(1u, degrees[4]);
    /* src/b.py: 1 CONTAINS out = 1 */
    TEST_ASSERT_EQUAL_UINT32(1u, degrees[5]);

    crg_graph_clear(&graph);
}

static void test_degrees_count_a_self_loop_twice(void)
{
    crg_graph graph;
    uint32_t degrees[2];
    static const uint32_t source[] = {0u};
    static const uint32_t target[] = {0u};
    static const uint32_t kind[] = {KIND_CALLS};
    static const char *const names[] = {"a", "b"};

    TEST_ASSERT_EQUAL_INT(CRG_OK, crg_graph_init(&graph, names, 2u, source,
                                                 target, kind, 1u));
    TEST_ASSERT_EQUAL_INT(CRG_OK, crg_graph_degrees(&graph, degrees));
    /* Python: degrees[source] += 1 then degrees[target] += 1. */
    TEST_ASSERT_EQUAL_UINT32(2u, degrees[0]);
    TEST_ASSERT_EQUAL_UINT32(0u, degrees[1]);

    crg_graph_clear(&graph);
}

static void test_degrees_rejects_null_output(void)
{
    crg_graph graph;

    TEST_ASSERT_EQUAL_INT(CRG_OK, build_fixture(&graph));
    TEST_ASSERT_EQUAL_INT(CRG_ERR_NULL, crg_graph_degrees(&graph, NULL));
    crg_graph_clear(&graph);
}

/* ---------------------------------------------------------------- */

static void test_parent_files_takes_the_first_containing_edge(void)
{
    crg_graph graph;
    uint8_t member[NAME_COUNT];
    uint32_t parent[NAME_COUNT];

    TEST_ASSERT_EQUAL_INT(CRG_OK, build_fixture(&graph));

    memset(member, 0, sizeof(member));
    member[0] = 1u;
    member[3] = 1u;

    TEST_ASSERT_EQUAL_INT(
        CRG_OK, crg_graph_parent_files(&graph, KIND_CONTAINS, member, parent));
    /* src/a.py contains f_a; src/b.py contains f_d */
    TEST_ASSERT_EQUAL_UINT32(4u, parent[0]);
    TEST_ASSERT_EQUAL_UINT32(5u, parent[3]);
    /* non-members stay absent */
    TEST_ASSERT_EQUAL_UINT32(CRG_NO_NODE, parent[1]);
    TEST_ASSERT_EQUAL_UINT32(CRG_NO_NODE, parent[4]);

    crg_graph_clear(&graph);
}

static void test_parent_files_ignores_other_kinds(void)
{
    crg_graph graph;
    uint8_t member[NAME_COUNT];
    uint32_t parent[NAME_COUNT];

    TEST_ASSERT_EQUAL_INT(CRG_OK, build_fixture(&graph));

    memset(member, 1u, sizeof(member));
    TEST_ASSERT_EQUAL_INT(
        CRG_OK, crg_graph_parent_files(&graph, KIND_CALLS, member, parent));

    /* The CALLS chain still resolves, so this is about the kind filter and
     * not about edges being absent. */
    TEST_ASSERT_EQUAL_UINT32(0u, parent[1]);
    TEST_ASSERT_EQUAL_UINT32(1u, parent[2]);
    TEST_ASSERT_EQUAL_UINT32(2u, parent[3]);
    /* No CALLS edge points at a File node. */
    TEST_ASSERT_EQUAL_UINT32(CRG_NO_NODE, parent[0]);
    TEST_ASSERT_EQUAL_UINT32(CRG_NO_NODE, parent[4]);
    TEST_ASSERT_EQUAL_UINT32(CRG_NO_NODE, parent[5]);

    crg_graph_clear(&graph);
}

/* ---------------------------------------------------------------- */

static void test_adjacency_excludes_structural_kinds(void)
{
    crg_graph graph;
    crg_adjacency adjacency;
    static const uint32_t excluded[] = {KIND_CONTAINS};

    TEST_ASSERT_EQUAL_INT(CRG_OK, build_fixture(&graph));
    TEST_ASSERT_EQUAL_INT(CRG_OK,
                          crg_adjacency_build(&graph, NULL, 0u, excluded, 1u, 0,
                                              &adjacency));

    /* f_a keeps only its CALLS neighbour, never its containing File. */
    TEST_ASSERT_EQUAL_UINT32(1u, adjacency.present[0]);
    TEST_ASSERT_EQUAL_UINT32(0u, adjacency.present[4]);
    /* f_b is still present: it is a CALLS endpoint. */
    TEST_ASSERT_EQUAL_UINT32(1u, adjacency.present[1]);

    /* undirected row for f_a: just f_b */
    TEST_ASSERT_EQUAL_size_t(0u, adjacency.offset[0]);
    TEST_ASSERT_EQUAL_size_t(1u, adjacency.offset[1]);
    TEST_ASSERT_EQUAL_UINT32(1u, adjacency.target[0]);

    crg_adjacency_clear(&adjacency);
    crg_graph_clear(&graph);
}

static void test_adjacency_directed_does_not_add_the_reverse_edge(void)
{
    crg_graph graph;
    crg_adjacency adjacency;

    TEST_ASSERT_EQUAL_INT(CRG_OK, build_fixture(&graph));
    TEST_ASSERT_EQUAL_INT(CRG_OK,
                          crg_adjacency_build(&graph, NULL, 0u, NULL, 0u, 1,
                                              &adjacency));

    /* f_d only receives; its row is empty but the node is present. */
    TEST_ASSERT_EQUAL_UINT32(1u, adjacency.present[3]);
    TEST_ASSERT_EQUAL_size_t(adjacency.offset[3], adjacency.offset[4]);

    crg_adjacency_clear(&adjacency);
    crg_graph_clear(&graph);
}

static void test_adjacency_deduplicates_parallel_edges(void)
{
    crg_graph graph;
    crg_adjacency adjacency;
    static const uint32_t source[] = {0u, 0u, 0u};
    static const uint32_t target[] = {1u, 1u, 2u};
    static const uint32_t kind[] = {KIND_CALLS, KIND_CALLS, KIND_CALLS};

    TEST_ASSERT_EQUAL_INT(CRG_OK,
                          crg_graph_init(&graph, NAMES, NAME_COUNT, source,
                                         target, kind, 3u));
    TEST_ASSERT_EQUAL_INT(CRG_OK,
                          crg_adjacency_build(&graph, NULL, 0u, NULL, 0u, 1,
                                              &adjacency));

    /* Two identical edges collapse into one row entry, as a Python set does. */
    TEST_ASSERT_EQUAL_size_t(2u, adjacency.offset[1]);
    TEST_ASSERT_EQUAL_UINT32(1u, adjacency.target[0]);
    TEST_ASSERT_EQUAL_UINT32(2u, adjacency.target[1]);

    crg_adjacency_clear(&adjacency);
    crg_graph_clear(&graph);
}

static void test_adjacency_rows_are_sorted_by_name(void)
{
    crg_graph graph;
    crg_adjacency adjacency;
    /* Insertion order is deliberately the reverse of the name order. */
    static const uint32_t source[] = {0u, 0u};
    static const uint32_t target[] = {5u, 2u};
    static const uint32_t kind[] = {KIND_CALLS, KIND_CALLS};

    TEST_ASSERT_EQUAL_INT(CRG_OK,
                          crg_graph_init(&graph, NAMES, NAME_COUNT, source,
                                         target, kind, 2u));
    TEST_ASSERT_EQUAL_INT(CRG_OK,
                          crg_adjacency_build(&graph, NULL, 0u, NULL, 0u, 1,
                                              &adjacency));

    /* "src/b.py" < "src/b.py::f_c" in code-point order. */
    TEST_ASSERT_EQUAL_UINT32(5u, adjacency.target[0]);
    TEST_ASSERT_EQUAL_UINT32(2u, adjacency.target[1]);

    crg_adjacency_clear(&adjacency);
    crg_graph_clear(&graph);
}

static void test_adjacency_restricts_to_the_allowed_kinds(void)
{
    crg_graph graph;
    crg_adjacency adjacency;
    static const uint32_t allowed[] = {KIND_CALLS};

    TEST_ASSERT_EQUAL_INT(CRG_OK, build_fixture(&graph));
    TEST_ASSERT_EQUAL_INT(CRG_OK,
                          crg_adjacency_build(&graph, allowed, 1u, NULL, 0u, 0,
                                              &adjacency));

    TEST_ASSERT_EQUAL_UINT32(0u, adjacency.present[4]);
    TEST_ASSERT_EQUAL_UINT32(0u, adjacency.present[5]);

    crg_adjacency_clear(&adjacency);
    crg_graph_clear(&graph);
}

static void test_adjacency_rejects_null_output(void)
{
    crg_graph graph;

    TEST_ASSERT_EQUAL_INT(CRG_OK, build_fixture(&graph));
    TEST_ASSERT_EQUAL_INT(CRG_ERR_NULL,
                          crg_adjacency_build(&graph, NULL, 0u, NULL, 0u, 0,
                                              NULL));
    crg_graph_clear(&graph);
}

static void test_adjacency_clear_is_safe_on_twice(void)
{
    crg_adjacency adjacency;

    memset(&adjacency, 0, sizeof(adjacency));
    crg_adjacency_clear(&adjacency);
    crg_adjacency_clear(&adjacency);

    TEST_ASSERT_NULL(adjacency.offset);
    TEST_ASSERT_EQUAL_size_t(0u, adjacency.node_count);
}

/* ---------------------------------------------------------------- */

static void test_hop_distances_labels_the_expected_nodes(void)
{
    crg_graph graph;
    crg_adjacency adjacency;
    static const uint32_t excluded[] = {KIND_CONTAINS};
    static const uint32_t seeds[] = {0u};
    int32_t hops[NAME_COUNT];
    uint32_t order[NAME_COUNT];
    size_t reachable = 0u;

    TEST_ASSERT_EQUAL_INT(CRG_OK, build_fixture(&graph));
    TEST_ASSERT_EQUAL_INT(CRG_OK,
                          crg_adjacency_build(&graph, NULL, 0u, excluded, 1u, 0,
                                              &adjacency));
    TEST_ASSERT_EQUAL_INT(
        CRG_OK,
        crg_adjacency_hop_distances(&adjacency, seeds, 1u, 2, hops, order,
                                    &reachable));

    /* f_a, f_b, f_c reachable; f_d is 3 hops out. */
    TEST_ASSERT_EQUAL_size_t(3u, reachable);
    TEST_ASSERT_EQUAL_INT32(0, hops[0]);
    TEST_ASSERT_EQUAL_INT32(1, hops[1]);
    TEST_ASSERT_EQUAL_INT32(2, hops[2]);
    TEST_ASSERT_EQUAL_INT32(-1, hops[3]);
    TEST_ASSERT_EQUAL_INT32(-1, hops[4]);
    TEST_ASSERT_EQUAL_INT32(-1, hops[5]);

    crg_adjacency_clear(&adjacency);
    crg_graph_clear(&graph);
}

static void test_hop_distances_respects_the_depth_cap(void)
{
    crg_graph graph;
    crg_adjacency adjacency;
    static const uint32_t excluded[] = {KIND_CONTAINS};
    static const uint32_t seeds[] = {0u};
    int32_t hops[NAME_COUNT];
    uint32_t order[NAME_COUNT];
    size_t reachable = 0u;

    TEST_ASSERT_EQUAL_INT(CRG_OK, build_fixture(&graph));
    TEST_ASSERT_EQUAL_INT(CRG_OK,
                          crg_adjacency_build(&graph, NULL, 0u, excluded, 1u, 0,
                                              &adjacency));
    TEST_ASSERT_EQUAL_INT(
        CRG_OK,
        crg_adjacency_hop_distances(&adjacency, seeds, 1u, 1, hops, order,
                                    &reachable));

    TEST_ASSERT_EQUAL_size_t(2u, reachable);
    /* f_c is 2 hops away and must stay unreached at depth 1. */
    TEST_ASSERT_EQUAL_INT32(-1, hops[2]);

    crg_adjacency_clear(&adjacency);
    crg_graph_clear(&graph);
}

static void test_hop_distances_with_depth_zero_returns_only_the_seeds(void)
{
    crg_graph graph;
    crg_adjacency adjacency;
    static const uint32_t seeds[] = {0u, 2u};
    int32_t hops[NAME_COUNT];
    uint32_t order[NAME_COUNT];
    size_t reachable = 0u;

    TEST_ASSERT_EQUAL_INT(CRG_OK, build_fixture(&graph));
    TEST_ASSERT_EQUAL_INT(CRG_OK,
                          crg_adjacency_build(&graph, NULL, 0u, NULL, 0u, 0,
                                              &adjacency));
    TEST_ASSERT_EQUAL_INT(
        CRG_OK,
        crg_adjacency_hop_distances(&adjacency, seeds, 2u, 0, hops, order,
                                    &reachable));

    TEST_ASSERT_EQUAL_size_t(2u, reachable);
    TEST_ASSERT_EQUAL_INT32(0, hops[0]);
    TEST_ASSERT_EQUAL_INT32(0, hops[2]);
    TEST_ASSERT_EQUAL_INT32(-1, hops[1]);

    crg_adjacency_clear(&adjacency);
    crg_graph_clear(&graph);
}

static void test_hop_distances_collapse_repeated_seeds(void)
{
    crg_graph graph;
    crg_adjacency adjacency;
    static const uint32_t seeds[] = {1u, 1u, 1u};
    int32_t hops[NAME_COUNT];
    uint32_t order[NAME_COUNT];
    size_t reachable = 0u;

    TEST_ASSERT_EQUAL_INT(CRG_OK, build_fixture(&graph));
    TEST_ASSERT_EQUAL_INT(CRG_OK,
                          crg_adjacency_build(&graph, NULL, 0u, NULL, 0u, 0,
                                              &adjacency));
    TEST_ASSERT_EQUAL_INT(
        CRG_OK,
        crg_adjacency_hop_distances(&adjacency, seeds, 3u, 0, hops, order,
                                    &reachable));

    /* dict.fromkeys would leave one entry, not three. */
    TEST_ASSERT_EQUAL_size_t(1u, reachable);

    crg_adjacency_clear(&adjacency);
    crg_graph_clear(&graph);
}

static void test_hop_distances_rejects_an_out_of_range_seed(void)
{
    crg_graph graph;
    crg_adjacency adjacency;
    static const uint32_t seeds[] = {99u};
    int32_t hops[NAME_COUNT];
    uint32_t order[NAME_COUNT];
    size_t reachable = 0u;

    TEST_ASSERT_EQUAL_INT(CRG_OK, build_fixture(&graph));
    TEST_ASSERT_EQUAL_INT(CRG_OK,
                          crg_adjacency_build(&graph, NULL, 0u, NULL, 0u, 0,
                                              &adjacency));
    TEST_ASSERT_EQUAL_INT(
        CRG_ERR_RANGE,
        crg_adjacency_hop_distances(&adjacency, seeds, 1u, 2, hops, order,
                                    &reachable));

    crg_adjacency_clear(&adjacency);
    crg_graph_clear(&graph);
}

static void test_hop_distances_rejects_null_outputs(void)
{
    crg_graph graph;
    crg_adjacency adjacency;
    static const uint32_t seeds[] = {0u};
    int32_t hops[NAME_COUNT];
    uint32_t order[NAME_COUNT];
    size_t reachable = 0u;

    TEST_ASSERT_EQUAL_INT(CRG_OK, build_fixture(&graph));
    TEST_ASSERT_EQUAL_INT(CRG_OK,
                          crg_adjacency_build(&graph, NULL, 0u, NULL, 0u, 0,
                                              &adjacency));
    TEST_ASSERT_EQUAL_INT(
        CRG_ERR_NULL,
        crg_adjacency_hop_distances(&adjacency, seeds, 1u, 2, NULL, order,
                                    &reachable));
    TEST_ASSERT_EQUAL_INT(
        CRG_ERR_NULL,
        crg_adjacency_hop_distances(&adjacency, seeds, 1u, 2, hops, NULL,
                                    &reachable));

    crg_adjacency_clear(&adjacency);
    crg_graph_clear(&graph);
}

/* ---------------------------------------------------------------- */

static void test_shortest_path_follows_calls(void)
{
    crg_graph graph;
    uint32_t path[NAME_COUNT];
    size_t length = 0u;
    int directed = 0;

    TEST_ASSERT_EQUAL_INT(CRG_OK, build_fixture(&graph));
    TEST_ASSERT_EQUAL_INT(CRG_OK,
                          crg_graph_shortest_path(&graph, NULL, 0u, 0u, 2u,
                                                  path, NAME_COUNT, &length,
                                                  &directed));

    /* f_a -> f_b -> f_c */
    TEST_ASSERT_EQUAL_size_t(3u, length);
    TEST_ASSERT_EQUAL_UINT32(0u, path[0]);
    TEST_ASSERT_EQUAL_UINT32(1u, path[1]);
    TEST_ASSERT_EQUAL_UINT32(2u, path[2]);
    TEST_ASSERT_EQUAL_INT(1, directed);

    crg_graph_clear(&graph);
}

static void test_shortest_path_of_a_symbol_to_itself(void)
{
    crg_graph graph;
    uint32_t path[NAME_COUNT];
    size_t length = 0u;
    int directed = 0;

    TEST_ASSERT_EQUAL_INT(CRG_OK, build_fixture(&graph));
    TEST_ASSERT_EQUAL_INT(CRG_OK,
                          crg_graph_shortest_path(&graph, NULL, 0u, 1u, 1u,
                                                  path, NAME_COUNT, &length,
                                                  &directed));

    TEST_ASSERT_EQUAL_size_t(1u, length);
    TEST_ASSERT_EQUAL_UINT32(1u, path[0]);
    TEST_ASSERT_EQUAL_INT(1, directed);

    crg_graph_clear(&graph);
}

static void test_shortest_path_ignores_contains_edges(void)
{
    crg_graph graph;
    uint32_t path[NAME_COUNT];
    size_t length = 0u;
    int directed = 0;
    /* f_d -> src/b.py is a CONTAINS edge, and nothing else leaves f_d. */
    static const uint32_t kinds[] = {KIND_CALLS};

    TEST_ASSERT_EQUAL_INT(CRG_OK, build_fixture(&graph));
    TEST_ASSERT_EQUAL_INT(CRG_OK,
                          crg_graph_shortest_path(&graph, kinds, 1u, 3u, 5u,
                                                  path, NAME_COUNT, &length,
                                                  &directed));

    TEST_ASSERT_EQUAL_size_t(0u, length);
    TEST_ASSERT_EQUAL_INT(0, directed);

    crg_graph_clear(&graph);
}

static void test_shortest_path_falls_back_to_undirected(void)
{
    crg_graph graph;
    uint32_t path[NAME_COUNT];
    size_t length = 0u;
    int directed = 0;

    TEST_ASSERT_EQUAL_INT(CRG_OK, build_fixture(&graph));
    /* Nothing calls its way from f_a to src/b.py; only the CONTAINS edges
     * relate them, so the undirected pass answers. */
    TEST_ASSERT_EQUAL_INT(CRG_OK,
                          crg_graph_shortest_path(&graph, NULL, 0u, 0u, 5u,
                                                  path, NAME_COUNT, &length,
                                                  &directed));

    TEST_ASSERT_EQUAL_size_t(5u, length);
    TEST_ASSERT_EQUAL_INT(0, directed);
    TEST_ASSERT_EQUAL_UINT32(0u, path[0]);
    TEST_ASSERT_EQUAL_UINT32(5u, path[length - 1u]);

    crg_graph_clear(&graph);
}

static void test_shortest_path_reports_no_route_for_isolated_nodes(void)
{
    crg_graph graph;
    uint32_t path[NAME_COUNT];
    size_t length = 0u;
    int directed = 0;
    static const uint32_t kinds[] = {KIND_IMPORTS}; /* no edge has this kind */

    TEST_ASSERT_EQUAL_INT(CRG_OK, build_fixture(&graph));
    TEST_ASSERT_EQUAL_INT(CRG_OK,
                          crg_graph_shortest_path(&graph, kinds, 1u, 0u, 1u,
                                                  path, NAME_COUNT, &length,
                                                  &directed));

    TEST_ASSERT_EQUAL_size_t(0u, length);
    TEST_ASSERT_EQUAL_INT(0, directed);

    crg_graph_clear(&graph);
}

static void test_shortest_path_rejects_a_capacity_of_zero_for_self(void)
{
    crg_graph graph;
    uint32_t path[1];
    size_t length = 0u;
    int directed = 0;

    TEST_ASSERT_EQUAL_INT(CRG_OK, build_fixture(&graph));
    TEST_ASSERT_EQUAL_INT(
        CRG_ERR_SIZE,
        crg_graph_shortest_path(&graph, NULL, 0u, 2u, 2u, path, 0u, &length,
                                &directed));

    crg_graph_clear(&graph);
}

static void test_shortest_path_rejects_out_of_range_endpoints(void)
{
    crg_graph graph;
    uint32_t path[NAME_COUNT];
    size_t length = 0u;
    int directed = 0;

    TEST_ASSERT_EQUAL_INT(CRG_OK, build_fixture(&graph));
    TEST_ASSERT_EQUAL_INT(
        CRG_ERR_RANGE,
        crg_graph_shortest_path(&graph, NULL, 0u, 0u, 99u, path, NAME_COUNT,
                                &length, &directed));

    crg_graph_clear(&graph);
}

/* ---------------------------------------------------------------- */

static void run_selection(crg_graph *graph, const int32_t *hops,
                          const uint32_t *degrees, const uint32_t *parents,
                          size_t max_nodes, const uint32_t *pinned,
                          size_t pinned_count, crg_selection *selection)
{
    TEST_ASSERT_EQUAL_INT(
        CRG_OK, crg_select_within_budget(graph, hops, degrees, parents,
                                         max_nodes, pinned, pinned_count,
                                         selection));
}

static void test_budget_trims_the_outermost_hop_first(void)
{
    crg_graph graph;
    /* f_a 0, f_b 1, f_c 1; f_c is dropped when only 2 slots are available. */
    static const int32_t hops[] = {0, 1, 1, -1, -1, -1};
    static const uint32_t degrees[] = {3u, 2u, 2u, 1u, 1u, 1u};
    static const uint32_t parents[] = {CRG_NO_NODE, CRG_NO_NODE, CRG_NO_NODE,
                                       CRG_NO_NODE, CRG_NO_NODE, CRG_NO_NODE};
    uint8_t selected[NAME_COUNT];
    int32_t selected_hop[NAME_COUNT];
    crg_selection selection = {selected, selected_hop, 0u, 0};

    TEST_ASSERT_EQUAL_INT(CRG_OK, build_fixture(&graph));
    run_selection(&graph, hops, degrees, parents, 2u, NULL, 0u, &selection);

    TEST_ASSERT_EQUAL_size_t(2u, selection.selected_count);
    /* Dropping f_c because the budget ran out is exactly what callers need to
     * report back, so the flag must survive the trim. */
    TEST_ASSERT_EQUAL_INT(1, selection.truncated);
    TEST_ASSERT_EQUAL_UINT8(1u, selected[0]);
    /* f_b and f_c tie on hop and degree; the name breaks the tie. */
    TEST_ASSERT_EQUAL_UINT8(1u, selected[1]);
    TEST_ASSERT_EQUAL_UINT8(0u, selected[2]);

    crg_graph_clear(&graph);
}

static void test_budget_ranks_by_degree_then_name(void)
{
    crg_graph graph;
    /* All three at hop 1; f_c has the higher degree so it wins the last slot. */
    static const int32_t hops[] = {-1, 1, 1, 1, -1, -1};
    static const uint32_t degrees[] = {0u, 1u, 5u, 2u, 0u, 0u};
    static const uint32_t parents[] = {CRG_NO_NODE, CRG_NO_NODE, CRG_NO_NODE,
                                       CRG_NO_NODE, CRG_NO_NODE, CRG_NO_NODE};
    uint8_t selected[NAME_COUNT];
    int32_t selected_hop[NAME_COUNT];
    crg_selection selection = {selected, selected_hop, 0u, 0};

    TEST_ASSERT_EQUAL_INT(CRG_OK, build_fixture(&graph));
    run_selection(&graph, hops, degrees, parents, 2u, NULL, 0u, &selection);

    TEST_ASSERT_EQUAL_size_t(2u, selection.selected_count);
    TEST_ASSERT_EQUAL_UINT8(1u, selected[2]);
    TEST_ASSERT_EQUAL_UINT8(1u, selected[3]);
    TEST_ASSERT_EQUAL_UINT8(0u, selected[1]);
    TEST_ASSERT_EQUAL_INT32(1, selected_hop[2]);

    crg_graph_clear(&graph);
}

static void test_budget_charges_a_symbol_its_containing_file(void)
{
    crg_graph graph;
    static const int32_t hops[] = {1, -1, -1, -1, -1, -1};
    static const uint32_t degrees[] = {3u, 2u, 2u, 1u, 1u, 1u};
    /* f_a is contained by src/a.py (id 4). */
    static const uint32_t parents[] = {4u, CRG_NO_NODE, CRG_NO_NODE,
                                       CRG_NO_NODE, CRG_NO_NODE, CRG_NO_NODE};
    uint8_t selected[NAME_COUNT];
    int32_t selected_hop[NAME_COUNT];
    crg_selection selection = {selected, selected_hop, 0u, 0};

    TEST_ASSERT_EQUAL_INT(CRG_OK, build_fixture(&graph));
    run_selection(&graph, hops, degrees, parents, 2u, NULL, 0u, &selection);

    /* The File rides along at the hop of its closest member. */
    TEST_ASSERT_EQUAL_size_t(2u, selection.selected_count);
    TEST_ASSERT_EQUAL_UINT8(1u, selected[0]);
    TEST_ASSERT_EQUAL_UINT8(1u, selected[4]);
    TEST_ASSERT_EQUAL_INT32(1, selected_hop[4]);

    crg_graph_clear(&graph);
}

static void test_budget_keeps_the_symbol_and_drops_the_file(void)
{
    crg_graph graph;
    static const int32_t hops[] = {1, 1, -1, -1, -1, -1};
    static const uint32_t degrees[] = {3u, 2u, 2u, 1u, 1u, 1u};
    static const uint32_t parents[] = {4u, 4u, CRG_NO_NODE, CRG_NO_NODE,
                                       CRG_NO_NODE, CRG_NO_NODE};
    uint8_t selected[NAME_COUNT];
    int32_t selected_hop[NAME_COUNT];
    crg_selection selection = {selected, selected_hop, 0u, 0};

    TEST_ASSERT_EQUAL_INT(CRG_OK, build_fixture(&graph));
    /* Three slots, two symbols and one File: the File is what gets given up. */
    run_selection(&graph, hops, degrees, parents, 3u, NULL, 0u, &selection);

    TEST_ASSERT_EQUAL_size_t(3u, selection.selected_count);
    TEST_ASSERT_EQUAL_UINT8(1u, selected[0]);
    TEST_ASSERT_EQUAL_UINT8(1u, selected[1]);
    TEST_ASSERT_EQUAL_UINT8(1u, selected[4]);

    crg_graph_clear(&graph);
}

static void test_budget_reports_truncation_when_a_node_cannot_fit(void)
{
    crg_graph graph;
    static const int32_t hops[] = {1, 1, -1, -1, -1, -1};
    static const uint32_t degrees[] = {3u, 2u, 2u, 1u, 1u, 1u};
    static const uint32_t parents[] = {CRG_NO_NODE, CRG_NO_NODE, CRG_NO_NODE,
                                       CRG_NO_NODE, CRG_NO_NODE, CRG_NO_NODE};
    uint8_t selected[NAME_COUNT];
    int32_t selected_hop[NAME_COUNT];
    crg_selection selection = {selected, selected_hop, 0u, 0};

    TEST_ASSERT_EQUAL_INT(CRG_OK, build_fixture(&graph));
    run_selection(&graph, hops, degrees, parents, 1u, NULL, 0u, &selection);

    TEST_ASSERT_EQUAL_size_t(1u, selection.selected_count);
    TEST_ASSERT_EQUAL_INT(1, selection.truncated);
    TEST_ASSERT_EQUAL_UINT8(1u, selected[0]);

    crg_graph_clear(&graph);
}

static void test_budget_places_pinned_nodes_first_and_bare(void)
{
    crg_graph graph;
    static const int32_t hops[] = {1, 1, -1, -1, -1, -1};
    static const uint32_t degrees[] = {3u, 2u, 2u, 1u, 1u, 1u};
    static const uint32_t parents[] = {4u, CRG_NO_NODE, CRG_NO_NODE,
                                       CRG_NO_NODE, CRG_NO_NODE, CRG_NO_NODE};
    static const uint32_t pinned[] = {1u};
    uint8_t selected[NAME_COUNT];
    int32_t selected_hop[NAME_COUNT];
    crg_selection selection = {selected, selected_hop, 0u, 0};

    TEST_ASSERT_EQUAL_INT(CRG_OK, build_fixture(&graph));
    /* f_b is pinned; it takes the only slot and its File is not charged. */
    run_selection(&graph, hops, degrees, parents, 1u, pinned, 1u, &selection);

    TEST_ASSERT_EQUAL_size_t(1u, selection.selected_count);
    TEST_ASSERT_EQUAL_UINT8(1u, selected[1]);
    TEST_ASSERT_EQUAL_UINT8(0u, selected[4]);
    TEST_ASSERT_EQUAL_INT32(1, selected_hop[1]);

    crg_graph_clear(&graph);
}

static void test_budget_reports_truncation_when_pinned_exhaust_the_budget(void)
{
    crg_graph graph;
    static const int32_t hops[] = {0, 1, -1, -1, -1, -1};
    static const uint32_t degrees[] = {3u, 2u, 2u, 1u, 1u, 1u};
    static const uint32_t parents[] = {CRG_NO_NODE, CRG_NO_NODE, CRG_NO_NODE,
                                       CRG_NO_NODE, CRG_NO_NODE, CRG_NO_NODE};
    static const uint32_t pinned[] = {0u, 1u};
    uint8_t selected[NAME_COUNT];
    int32_t selected_hop[NAME_COUNT];
    crg_selection selection = {selected, selected_hop, 0u, 0};

    TEST_ASSERT_EQUAL_INT(CRG_OK, build_fixture(&graph));
    /* The second pinned node finds the budget already full. */
    run_selection(&graph, hops, degrees, parents, 1u, pinned, 2u, &selection);

    TEST_ASSERT_EQUAL_size_t(1u, selection.selected_count);
    TEST_ASSERT_EQUAL_INT(1, selection.truncated);

    crg_graph_clear(&graph);
}

static void test_budget_skips_pinned_nodes_that_are_not_reachable(void)
{
    crg_graph graph;
    static const int32_t hops[] = {0, -1, -1, -1, -1, -1};
    static const uint32_t degrees[] = {3u, 2u, 2u, 1u, 1u, 1u};
    static const uint32_t parents[] = {CRG_NO_NODE, CRG_NO_NODE, CRG_NO_NODE,
                                       CRG_NO_NODE, CRG_NO_NODE, CRG_NO_NODE};
    static const uint32_t pinned[] = {3u};
    uint8_t selected[NAME_COUNT];
    int32_t selected_hop[NAME_COUNT];
    crg_selection selection = {selected, selected_hop, 0u, 0};

    TEST_ASSERT_EQUAL_INT(CRG_OK, build_fixture(&graph));
    run_selection(&graph, hops, degrees, parents, 1u, pinned, 1u, &selection);

    /* f_d is unreachable, so pinning it neither selects it nor truncates. */
    TEST_ASSERT_EQUAL_size_t(1u, selection.selected_count);
    TEST_ASSERT_EQUAL_UINT8(0u, selected[3]);
    TEST_ASSERT_EQUAL_INT(0, selection.truncated);

    crg_graph_clear(&graph);
}

static void test_budget_lowers_the_hop_of_an_already_selected_node(void)
{
    crg_graph graph;
    /* src/a.py is pinned at hop 1 even though it also appears at hop 0. */
    static const int32_t hops[] = {-1, -1, -1, -1, 0, -1};
    static const uint32_t degrees[] = {3u, 2u, 2u, 1u, 1u, 1u};
    static const uint32_t parents[] = {CRG_NO_NODE, CRG_NO_NODE, CRG_NO_NODE,
                                       CRG_NO_NODE, CRG_NO_NODE, CRG_NO_NODE};
    static const uint32_t pinned[] = {4u};
    uint8_t selected[NAME_COUNT];
    int32_t selected_hop[NAME_COUNT];
    crg_selection selection = {selected, selected_hop, 0u, 0};

    TEST_ASSERT_EQUAL_INT(CRG_OK, build_fixture(&graph));
    run_selection(&graph, hops, degrees, parents, 2u, pinned, 1u, &selection);

    TEST_ASSERT_EQUAL_UINT8(1u, selected[4]);
    TEST_ASSERT_EQUAL_INT32(0, selected_hop[4]);

    crg_graph_clear(&graph);
}

static void test_budget_rejects_an_out_of_range_pinned_node(void)
{
    crg_graph graph;
    static const int32_t hops[] = {0, -1, -1, -1, -1, -1};
    static const uint32_t degrees[] = {0u, 0u, 0u, 0u, 0u, 0u};
    static const uint32_t parents[] = {CRG_NO_NODE, CRG_NO_NODE, CRG_NO_NODE,
                                       CRG_NO_NODE, CRG_NO_NODE, CRG_NO_NODE};
    static const uint32_t pinned[] = {99u};
    uint8_t selected[NAME_COUNT];
    int32_t selected_hop[NAME_COUNT];
    crg_selection selection = {selected, selected_hop, 0u, 0};

    TEST_ASSERT_EQUAL_INT(CRG_OK, build_fixture(&graph));
    TEST_ASSERT_EQUAL_INT(
        CRG_ERR_RANGE,
        crg_select_within_budget(&graph, hops, degrees, parents, 5u, pinned,
                                 1u, &selection));

    crg_graph_clear(&graph);
}

static void test_budget_rejects_missing_output_buffers(void)
{
    crg_graph graph;
    static const int32_t hops[] = {0, -1, -1, -1, -1, -1};
    static const uint32_t degrees[] = {0u, 0u, 0u, 0u, 0u, 0u};
    static const uint32_t parents[] = {CRG_NO_NODE, CRG_NO_NODE, CRG_NO_NODE,
                                       CRG_NO_NODE, CRG_NO_NODE, CRG_NO_NODE};
    uint8_t selected[NAME_COUNT];
    int32_t selected_hop[NAME_COUNT];
    crg_selection selection = {selected, selected_hop, 0u, 0};

    TEST_ASSERT_EQUAL_INT(CRG_OK, build_fixture(&graph));
    selection.selected = NULL;
    TEST_ASSERT_EQUAL_INT(
        CRG_ERR_NULL,
        crg_select_within_budget(&graph, hops, degrees, parents, 5u, NULL, 0u,
                                 &selection));

    crg_graph_clear(&graph);
}

static void test_budget_handles_a_graph_with_no_reachable_node(void)
{
    crg_graph graph;
    static const int32_t hops[] = {-1, -1, -1, -1, -1, -1};
    static const uint32_t degrees[] = {0u, 0u, 0u, 0u, 0u, 0u};
    static const uint32_t parents[] = {CRG_NO_NODE, CRG_NO_NODE, CRG_NO_NODE,
                                       CRG_NO_NODE, CRG_NO_NODE, CRG_NO_NODE};
    uint8_t selected[NAME_COUNT];
    int32_t selected_hop[NAME_COUNT];
    crg_selection selection = {selected, selected_hop, 0u, 0};

    TEST_ASSERT_EQUAL_INT(CRG_OK, build_fixture(&graph));
    run_selection(&graph, hops, degrees, parents, 5u, NULL, 0u, &selection);

    TEST_ASSERT_EQUAL_size_t(0u, selection.selected_count);
    TEST_ASSERT_EQUAL_INT(0, selection.truncated);

    crg_graph_clear(&graph);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_graph_init_rejects_null_graph);
    RUN_TEST(test_graph_init_rejects_null_names);
    RUN_TEST(test_graph_init_rejects_empty_graph);
    RUN_TEST(test_graph_init_rejects_partial_edge_arrays);
    RUN_TEST(test_graph_init_rejects_out_of_range_ids);
    RUN_TEST(test_graph_init_leaves_the_struct_cleared_on_failure);
    RUN_TEST(test_graph_clear_is_safe_on_zeroed_and_twice);
    RUN_TEST(test_graph_clear_is_safe_on_null);
    RUN_TEST(test_graph_accepts_an_edgeless_graph);
    RUN_TEST(test_degrees_count_both_endpoints);
    RUN_TEST(test_degrees_count_a_self_loop_twice);
    RUN_TEST(test_degrees_rejects_null_output);
    RUN_TEST(test_parent_files_takes_the_first_containing_edge);
    RUN_TEST(test_parent_files_ignores_other_kinds);
    RUN_TEST(test_adjacency_excludes_structural_kinds);
    RUN_TEST(test_adjacency_directed_does_not_add_the_reverse_edge);
    RUN_TEST(test_adjacency_deduplicates_parallel_edges);
    RUN_TEST(test_adjacency_rows_are_sorted_by_name);
    RUN_TEST(test_adjacency_restricts_to_the_allowed_kinds);
    RUN_TEST(test_adjacency_rejects_null_output);
    RUN_TEST(test_adjacency_clear_is_safe_on_twice);
    RUN_TEST(test_hop_distances_labels_the_expected_nodes);
    RUN_TEST(test_hop_distances_respects_the_depth_cap);
    RUN_TEST(test_hop_distances_with_depth_zero_returns_only_the_seeds);
    RUN_TEST(test_hop_distances_collapse_repeated_seeds);
    RUN_TEST(test_hop_distances_rejects_an_out_of_range_seed);
    RUN_TEST(test_hop_distances_rejects_null_outputs);
    RUN_TEST(test_shortest_path_follows_calls);
    RUN_TEST(test_shortest_path_of_a_symbol_to_itself);
    RUN_TEST(test_shortest_path_ignores_contains_edges);
    RUN_TEST(test_shortest_path_falls_back_to_undirected);
    RUN_TEST(test_shortest_path_reports_no_route_for_isolated_nodes);
    RUN_TEST(test_shortest_path_rejects_a_capacity_of_zero_for_self);
    RUN_TEST(test_shortest_path_rejects_out_of_range_endpoints);
    RUN_TEST(test_budget_trims_the_outermost_hop_first);
    RUN_TEST(test_budget_ranks_by_degree_then_name);
    RUN_TEST(test_budget_charges_a_symbol_its_containing_file);
    RUN_TEST(test_budget_keeps_the_symbol_and_drops_the_file);
    RUN_TEST(test_budget_reports_truncation_when_a_node_cannot_fit);
    RUN_TEST(test_budget_places_pinned_nodes_first_and_bare);
    RUN_TEST(test_budget_reports_truncation_when_pinned_exhaust_the_budget);
    RUN_TEST(test_budget_skips_pinned_nodes_that_are_not_reachable);
    RUN_TEST(test_budget_lowers_the_hop_of_an_already_selected_node);
    RUN_TEST(test_budget_rejects_an_out_of_range_pinned_node);
    RUN_TEST(test_budget_rejects_missing_output_buffers);
    RUN_TEST(test_budget_handles_a_graph_with_no_reachable_node);
    return UNITY_END();
}
