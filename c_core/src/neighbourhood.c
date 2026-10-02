#include "code_review_graph/neighbourhood.h"

#include <stdlib.h>
#include <string.h>

/* Ported from code_review_graph/neighbourhood.py.
 *
 * Ordering contract: Python sorts strings by Unicode code point, strcmp sorts
 * UTF-8 bytes. Those agree for every well-formed UTF-8 string, so each sort
 * below reproduces the Python order exactly. Callers must pass valid UTF-8.
 *
 * Node identity: ids are assigned by the caller over *unique* names, so two
 * Python dicts keyed by qualified_name collapse to one C node here. The Python
 * side de-duplicates names before building the graph.
 */

/* Python neighbourhood._MAX_PATH_VISITS: refuse pathological walks. */
#define CRG_MAX_PATH_VISITS 2000000u

typedef struct {
    const char *name;
    uint32_t id;
} crg_named_id;

static int crg_compare_named_id(const void *lhs, const void *rhs)
{
    const crg_named_id *left = (const crg_named_id *)lhs;
    const crg_named_id *right = (const crg_named_id *)rhs;
    int order = strcmp(left->name, right->name);

    if (order != 0) {
        return order < 0 ? -1 : 1;
    }
    if (left->id < right->id) {
        return -1;
    }
    if (left->id > right->id) {
        return 1;
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* graph                                                               */
/* ------------------------------------------------------------------ */

crg_status crg_graph_init(crg_graph *graph,
                          const char *const *names, size_t node_count,
                          const uint32_t *edge_source,
                          const uint32_t *edge_target,
                          const uint32_t *edge_kind,
                          size_t edge_count)
{
    if (graph == NULL) {
        return CRG_ERR_NULL;
    }
    memset(graph, 0, sizeof(*graph));

    if (names == NULL || node_count == 0u) {
        return CRG_ERR_NULL;
    }
    if (edge_count > 0u && (edge_source == NULL || edge_target == NULL ||
                            edge_kind == NULL)) {
        return CRG_ERR_NULL;
    }
    if (edge_count > UINT32_MAX || node_count > UINT32_MAX) {
        return CRG_ERR_SIZE;
    }
    if (node_count + 1u > SIZE_MAX / sizeof(size_t)) {
        return CRG_ERR_OVERFLOW;
    }
    if (edge_count > SIZE_MAX / sizeof(uint32_t)) {
        return CRG_ERR_OVERFLOW;
    }

    for (size_t index = 0u; index < edge_count; ++index) {
        if (edge_source[index] >= node_count || edge_target[index] >= node_count) {
            return CRG_ERR_RANGE;
        }
    }

    /* Edges stay in caller order: parent_files and shortest-path tie-breaks
     * both depend on it. */
    graph->edge_source = (uint32_t *)malloc(edge_count * sizeof(uint32_t));
    graph->edge_target = (uint32_t *)malloc(edge_count * sizeof(uint32_t));
    graph->edge_kind = (uint32_t *)malloc(edge_count * sizeof(uint32_t));
    if (graph->edge_source == NULL || graph->edge_target == NULL ||
        graph->edge_kind == NULL) {
        crg_graph_clear(graph);
        return CRG_ERR_NOMEM;
    }
    if (edge_count > 0u) {
        memcpy(graph->edge_source, edge_source, edge_count * sizeof(uint32_t));
        memcpy(graph->edge_target, edge_target, edge_count * sizeof(uint32_t));
        memcpy(graph->edge_kind, edge_kind, edge_count * sizeof(uint32_t));
    }

    graph->names = names;
    graph->node_count = node_count;
    graph->edge_count = edge_count;
    return CRG_OK;
}

void crg_graph_clear(crg_graph *graph)
{
    if (graph == NULL) {
        return;
    }
    free(graph->edge_source);
    free(graph->edge_target);
    free(graph->edge_kind);
    memset(graph, 0, sizeof(*graph));
}

crg_status crg_graph_degrees(const crg_graph *graph, uint32_t *out_degrees)
{
    if (graph == NULL || out_degrees == NULL) {
        return CRG_ERR_NULL;
    }

    memset(out_degrees, 0, graph->node_count * sizeof(uint32_t));

    /* Python: degrees[source] += 1; degrees[target] += 1 -- a self loop
     * therefore counts twice, and it must stay that way for parity. */
    for (size_t index = 0u; index < graph->edge_count; ++index) {
        uint32_t source = graph->edge_source[index];
        uint32_t target = graph->edge_target[index];

        if (out_degrees[source] == UINT32_MAX || out_degrees[target] == UINT32_MAX) {
            return CRG_ERR_OVERFLOW;
        }
        out_degrees[source] += 1u;
        out_degrees[target] += 1u;
    }
    return CRG_OK;
}

crg_status crg_graph_parent_files(const crg_graph *graph, uint32_t kind,
                                  const uint8_t *member, uint32_t *out_parent)
{
    if (graph == NULL || member == NULL || out_parent == NULL) {
        return CRG_ERR_NULL;
    }

    for (size_t index = 0u; index < graph->node_count; ++index) {
        out_parent[index] = CRG_NO_NODE;
    }

    /* dict.setdefault over edges in their original order: first match wins. */
    for (size_t index = 0u; index < graph->edge_count; ++index) {
        if (graph->edge_kind[index] != kind) {
            continue;
        }
        uint32_t target = graph->edge_target[index];

        if (member[target] == 0u || out_parent[target] != CRG_NO_NODE) {
            continue;
        }
        out_parent[target] = graph->edge_source[index];
    }
    return CRG_OK;
}

/* ------------------------------------------------------------------ */
/* adjacency                                                           */
/* ------------------------------------------------------------------ */

static int crg_kind_allowed(const uint32_t *allowed, size_t allowed_count,
                            const uint32_t *excluded, size_t excluded_count,
                            uint32_t kind)
{
    for (size_t index = 0u; index < excluded_count; ++index) {
        if (excluded[index] == kind) {
            return 0;
        }
    }
    if (allowed_count == 0u) {
        return 1;
    }
    for (size_t index = 0u; index < allowed_count; ++index) {
        if (allowed[index] == kind) {
            return 1;
        }
    }
    return 0;
}

crg_status crg_adjacency_build(const crg_graph *graph,
                               const uint32_t *allowed, size_t allowed_count,
                               const uint32_t *excluded, size_t excluded_count,
                               int directed, crg_adjacency *out_adjacency)
{
    if (graph == NULL || out_adjacency == NULL) {
        return CRG_ERR_NULL;
    }
    memset(out_adjacency, 0, sizeof(*out_adjacency));

    size_t node_count = graph->node_count;
    size_t edge_count = graph->edge_count;

    if (node_count + 1u > SIZE_MAX / sizeof(size_t) ||
        node_count > SIZE_MAX / sizeof(size_t) ||
        edge_count > (SIZE_MAX / sizeof(uint32_t)) / 2u) {
        crg_adjacency_clear(out_adjacency);
        return CRG_ERR_OVERFLOW;
    }

    out_adjacency->offset = (size_t *)calloc(node_count + 1u, sizeof(size_t));
    out_adjacency->present = (uint8_t *)calloc(node_count, sizeof(uint8_t));
    out_adjacency->row_length = (size_t *)calloc(node_count, sizeof(size_t));
    out_adjacency->row_length_final =
        (size_t *)calloc(node_count + 1u, sizeof(size_t));
    if (out_adjacency->offset == NULL || out_adjacency->present == NULL ||
        out_adjacency->row_length == NULL ||
        out_adjacency->row_length_final == NULL) {
        crg_adjacency_clear(out_adjacency);
        return CRG_ERR_NOMEM;
    }

    /* Pass 1: per-row width. A directed edge adds one entry to the source's
     * row; an undirected edge also adds the source to the target's row. */
    size_t total = 0u;

    for (size_t index = 0u; index < edge_count; ++index) {
        if (!crg_kind_allowed(allowed, allowed_count, excluded, excluded_count,
                              graph->edge_kind[index])) {
            continue;
        }
        uint32_t source = graph->edge_source[index];
        uint32_t target = graph->edge_target[index];

        out_adjacency->present[source] = 1u;
        out_adjacency->present[target] = 1u;
        out_adjacency->row_length[source] += 1u;
        total += 1u;
        if (!directed) {
            out_adjacency->row_length[target] += 1u;
            total += 1u;
        }
    }

    /* calloc(0) is implementation-defined, and an edgeless graph still needs
     * a non-NULL array so _clear stays symmetric. */
    out_adjacency->target = (uint32_t *)calloc(total + 1u, sizeof(uint32_t));
    if (out_adjacency->target == NULL) {
        crg_adjacency_clear(out_adjacency);
        return CRG_ERR_NOMEM;
    }

    /* Pass 2: prefix-sum the offsets. row_length is then redundant with
     * offset[node + 1] - offset[node], so it is reused as the write cursor --
     * a node's row receives both its outgoing and (undirected) its incoming
     * edges, which is exactly why the cursor cannot be offset itself. */
    total = 0u;

    for (size_t node = 0u; node < node_count; ++node) {
        out_adjacency->offset[node] = total;
        total += out_adjacency->row_length[node];
    }
    out_adjacency->offset[node_count] = total;

    for (size_t node = 0u; node < node_count; ++node) {
        out_adjacency->row_length[node] = out_adjacency->offset[node];
    }

    for (size_t index = 0u; index < edge_count; ++index) {
        if (!crg_kind_allowed(allowed, allowed_count, excluded, excluded_count,
                              graph->edge_kind[index])) {
            continue;
        }
        uint32_t source = graph->edge_source[index];
        uint32_t target = graph->edge_target[index];

        out_adjacency->target[out_adjacency->row_length[source]++] = target;
        if (!directed) {
            out_adjacency->target[out_adjacency->row_length[target]++] = source;
        }
    }

    /* Pass 3: sort each row by name and drop duplicates so the traversal
     * order matches Python's sorted(adjacency[current]) over a set. Final
     * lengths are recorded separately because a shortened row shifts every
     * later row. */
    for (size_t node = 0u; node < node_count; ++node) {
        size_t row_start = out_adjacency->offset[node];
        size_t row_length = out_adjacency->offset[node + 1u] - row_start;
        size_t unique = 0u;

        if (row_length > 1u) {
            crg_named_id *scratch =
                (crg_named_id *)malloc(row_length * sizeof(crg_named_id));
            if (scratch == NULL) {
                crg_adjacency_clear(out_adjacency);
                return CRG_ERR_NOMEM;
            }
            for (size_t index = 0u; index < row_length; ++index) {
                uint32_t id = out_adjacency->target[row_start + index];

                scratch[index].name = graph->names[id];
                scratch[index].id = id;
            }
            qsort(scratch, row_length, sizeof(crg_named_id),
                  crg_compare_named_id);

            for (size_t index = 0u; index < row_length; ++index) {
                if (index > 0u && scratch[index].id == scratch[index - 1u].id) {
                    continue;
                }
                out_adjacency->target[row_start + unique] = scratch[index].id;
                unique += 1u;
            }
            free(scratch);
        } else {
            unique = row_length;
        }
        out_adjacency->row_length_final[node] = unique;
    }

    /* Pass 4: compact the deduplicated rows into a contiguous array. */
    size_t write = 0u;

    for (size_t node = 0u; node < node_count; ++node) {
        size_t row_start = out_adjacency->offset[node];
        size_t unique = out_adjacency->row_length_final[node];

        if (write != row_start && unique > 0u) {
            memmove(&out_adjacency->target[write],
                    &out_adjacency->target[row_start],
                    unique * sizeof(uint32_t));
        }
        out_adjacency->offset[node] = write;
        write += unique;
    }
    out_adjacency->offset[node_count] = write;

    free(out_adjacency->row_length);
    out_adjacency->row_length = NULL;

    out_adjacency->node_count = node_count;
    return CRG_OK;
}

void crg_adjacency_clear(crg_adjacency *adjacency)
{
    if (adjacency == NULL) {
        return;
    }
    free(adjacency->offset);
    free(adjacency->target);
    free(adjacency->present);
    free(adjacency->row_length);
    free(adjacency->row_length_final);
    memset(adjacency, 0, sizeof(*adjacency));
}

/* ------------------------------------------------------------------ */
/* traversal                                                           */
/* ------------------------------------------------------------------ */

crg_status crg_adjacency_hop_distances(const crg_adjacency *adjacency,
                                        const uint32_t *seeds, size_t seed_count,
                                        int32_t depth,
                                        int32_t *out_hops, uint32_t *out_order,
                                        size_t *out_reachable)
{
    if (adjacency == NULL || out_hops == NULL || out_order == NULL ||
        out_reachable == NULL) {
        return CRG_ERR_NULL;
    }
    if (seed_count > 0u && seeds == NULL) {
        return CRG_ERR_NULL;
    }

    size_t node_count = adjacency->node_count;

    for (size_t index = 0u; index < node_count; ++index) {
        out_hops[index] = -1;
    }

    size_t reachable = 0u;
    size_t head = 0u;

    for (size_t index = 0u; index < seed_count; ++index) {
        uint32_t seed = seeds[index];

        if (seed >= node_count) {
            return CRG_ERR_RANGE;
        }
        /* dict.fromkeys semantics: a repeated seed keeps its first slot. */
        if (out_hops[seed] >= 0) {
            continue;
        }
        out_hops[seed] = 0;
        out_order[reachable] = seed;
        reachable += 1u;
    }

    while (head < reachable) {
        uint32_t current = out_order[head];
        head += 1u;

        int32_t distance = out_hops[current];
        if (distance >= depth) {
            continue;
        }
        if (distance == INT32_MAX) {
            return CRG_ERR_OVERFLOW;
        }

        size_t start = adjacency->offset[current];
        size_t stop = adjacency->offset[current + 1u];

        for (size_t slot = start; slot < stop; ++slot) {
            uint32_t neighbour = adjacency->target[slot];

            if (out_hops[neighbour] >= 0) {
                continue;
            }
            out_hops[neighbour] = distance + 1;
            out_order[reachable] = neighbour;
            reachable += 1u;
        }
    }

    *out_reachable = reachable;
    return CRG_OK;
}

/* Python _bfs_path: returns the path ids, or an empty list when the pair is
 * unconnected. Rows are name-sorted, so the tie-break matches Python's
 * sorted(adjacency[current]). */
static crg_status crg_bfs_path(const crg_adjacency *adjacency, size_t node_count,
                               uint32_t source, uint32_t target,
                               uint32_t *out_path, size_t path_capacity,
                               size_t *out_length)
{
    *out_length = 0u;

    /* Python returns [] unless both endpoints are keys of the adjacency map,
     * i.e. touched by at least one surviving edge. */
    if (adjacency->present[source] == 0u || adjacency->present[target] == 0u) {
        return CRG_OK;
    }

    uint32_t *previous = (uint32_t *)malloc(node_count * sizeof(uint32_t));
    uint8_t *seen = (uint8_t *)calloc(node_count, sizeof(uint8_t));
    if (previous == NULL || seen == NULL) {
        free(previous);
        free(seen);
        return CRG_ERR_NOMEM;
    }

    uint32_t *queue = (uint32_t *)malloc(node_count * sizeof(uint32_t));
    if (queue == NULL) {
        free(previous);
        free(seen);
        return CRG_ERR_NOMEM;
    }

    seen[source] = 1u;
    queue[0] = source;
    size_t head = 0u;
    size_t tail = 1u;
    size_t visits = 0u;
    int found = 0;

    while (head < tail) {
        uint32_t current = queue[head];
        head += 1u;
        visits += 1u;

        if (visits > CRG_MAX_PATH_VISITS) {
            free(previous);
            free(seen);
            free(queue);
            *out_length = 0u;
            return CRG_OK;
        }
        if (current == target) {
            found = 1;
            break;
        }

        size_t start = adjacency->offset[current];
        size_t stop = adjacency->offset[current + 1u];

        for (size_t slot = start; slot < stop; ++slot) {
            uint32_t neighbour = adjacency->target[slot];

            if (seen[neighbour] != 0u) {
                continue;
            }
            seen[neighbour] = 1u;
            previous[neighbour] = current;
            queue[tail] = neighbour;
            tail += 1u;
        }
    }

    free(queue);

    if (!found) {
        free(previous);
        free(seen);
        return CRG_OK;
    }

    /* Walk the predecessor chain back to the seed, then reverse in place. */
    size_t length = 0u;

    for (uint32_t node = target;;) {
        if (length >= path_capacity) {
            free(previous);
            free(seen);
            *out_length = 0u;
            return CRG_ERR_SIZE;
        }
        out_path[length] = node;
        length += 1u;

        if (node == source) {
            break;
        }
        node = previous[node];
    }
    for (size_t left = 0u, right = length - 1u; left < right; ++left, --right) {
        uint32_t swap = out_path[left];

        out_path[left] = out_path[right];
        out_path[right] = swap;
    }

    free(previous);
    free(seen);
    *out_length = length;
    return CRG_OK;
}

crg_status crg_graph_shortest_path(const crg_graph *graph,
                                   const uint32_t *kinds, size_t kind_count,
                                   uint32_t source, uint32_t target,
                                   uint32_t *out_path, size_t path_capacity,
                                   size_t *out_length, int *out_directed)
{
    if (graph == NULL || out_path == NULL || out_length == NULL ||
        out_directed == NULL) {
        return CRG_ERR_NULL;
    }
    if (kind_count > 0u && kinds == NULL) {
        return CRG_ERR_NULL;
    }
    if (source >= graph->node_count || target >= graph->node_count) {
        return CRG_ERR_RANGE;
    }
    *out_length = 0u;

    /* Python short-circuits source == target before building any adjacency. */
    if (source == target) {
        if (path_capacity < 1u) {
            return CRG_ERR_SIZE;
        }
        out_path[0] = source;
        *out_length = 1u;
        *out_directed = 1;
        return CRG_OK;
    }

    crg_adjacency adjacency;
    size_t length = 0u;
    crg_status status;

    status = crg_adjacency_build(graph, kinds, kind_count, NULL, 0u, 1,
                                 &adjacency);
    if (status != CRG_OK) {
        return status;
    }
    status = crg_bfs_path(&adjacency, graph->node_count, source, target,
                          out_path, path_capacity, &length);
    crg_adjacency_clear(&adjacency);
    if (status != CRG_OK) {
        return status;
    }
    if (length > 0u) {
        *out_length = length;
        *out_directed = 1;
        return CRG_OK;
    }

    /* Fall back to the undirected graph so a merely *related* pair still
     * shows a route; the answer is still labelled undirected. */
    status = crg_adjacency_build(graph, kinds, kind_count, NULL, 0u, 0,
                                 &adjacency);
    if (status != CRG_OK) {
        return status;
    }
    status = crg_bfs_path(&adjacency, graph->node_count, source, target,
                          out_path, path_capacity, &length);
    crg_adjacency_clear(&adjacency);
    if (status != CRG_OK) {
        return status;
    }

    *out_length = length;
    *out_directed = 0;
    return CRG_OK;
}

/* ------------------------------------------------------------------ */
/* payload budget                                                      */
/* ------------------------------------------------------------------ */

typedef struct {
    const char *name;
    int32_t hop;
    int64_t degree; /* negated, so ascending sorts best-connected first */
    uint32_t id;
} crg_ranked;

static int crg_compare_ranked(const void *lhs, const void *rhs)
{
    const crg_ranked *left = (const crg_ranked *)lhs;
    const crg_ranked *right = (const crg_ranked *)rhs;

    if (left->hop != right->hop) {
        return left->hop < right->hop ? -1 : 1;
    }
    if (left->degree != right->degree) {
        return left->degree < right->degree ? -1 : 1;
    }
    return crg_compare_named_id(&(const crg_named_id){left->name, left->id},
                                &(const crg_named_id){right->name, right->id});
}

crg_status crg_select_within_budget(const crg_graph *graph,
                                    const int32_t *hops,
                                    const uint32_t *degrees,
                                    const uint32_t *parents,
                                    size_t max_nodes,
                                    const uint32_t *pinned, size_t pinned_count,
                                    crg_selection *out_selection)
{
    if (graph == NULL || hops == NULL || degrees == NULL || parents == NULL ||
        out_selection == NULL || out_selection->selected == NULL ||
        out_selection->out_hops == NULL) {
        return CRG_ERR_NULL;
    }
    if (pinned_count > 0u && pinned == NULL) {
        return CRG_ERR_NULL;
    }

    size_t node_count = graph->node_count;
    uint8_t *selected = out_selection->selected;
    int32_t *selected_hop = out_selection->out_hops;

    memset(selected, 0, node_count * sizeof(uint8_t));
    size_t count = 0u;
    int truncated = 0;

    /* Pinned nodes go in bare and first: a path answer must never be crowded
     * out by another node's containing File. */
    for (size_t index = 0u; index < pinned_count; ++index) {
        if (count >= max_nodes) {
            truncated = 1;
            break;
        }
        uint32_t id = pinned[index];

        if (id >= node_count) {
            return CRG_ERR_RANGE;
        }
        if (hops[id] >= 0 && selected[id] == 0u) {
            selected[id] = 1u;
            selected_hop[id] = hops[id];
            count += 1u;
        }
    }

    crg_ranked *ordered = (crg_ranked *)malloc(node_count * sizeof(crg_ranked));
    if (ordered == NULL) {
        return CRG_ERR_NOMEM;
    }

    size_t reachable = 0u;

    for (size_t index = 0u; index < node_count; ++index) {
        if (hops[index] < 0) {
            continue;
        }
        ordered[reachable].name = graph->names[index];
        ordered[reachable].hop = hops[index];
        ordered[reachable].degree = -(int64_t)degrees[index];
        ordered[reachable].id = (uint32_t)index;
        reachable += 1u;
    }
    qsort(ordered, reachable, sizeof(crg_ranked), crg_compare_ranked);

    for (size_t index = 0u; index < reachable; ++index) {
        uint32_t id = ordered[index].id;
        int32_t distance = ordered[index].hop;
        int already = selected[id] != 0u;

        if (already && selected_hop[id] > distance) {
            selected_hop[id] = distance;
        }

        /* A symbol costs its own slot plus, the first time it appears, a slot
         * for the File that contains it. */
        uint32_t addition[2];
        size_t addition_length = 0u;

        if (!already) {
            addition[addition_length] = id;
            addition_length += 1u;
        }
        uint32_t parent = parents[id];
        if (parent != CRG_NO_NODE && selected[parent] == 0u) {
            addition[addition_length] = parent;
            addition_length += 1u;
        }
        if (addition_length == 0u) {
            continue;
        }

        if (count + addition_length > max_nodes) {
            if (addition_length > 1u && count + 1u <= max_nodes) {
                /* Keep the symbol, give up its File: the File is a rendering
                 * nicety, the symbol is what the reviewer asked for. */
                addition_length = 1u;
            } else {
                truncated = 1;
                continue;
            }
        }

        for (size_t slot = 0u; slot < addition_length; ++slot) {
            uint32_t member = addition[slot];

            if (selected[member] == 0u) {
                selected[member] = 1u;
                selected_hop[member] = distance;
                count += 1u;
            } else if (selected_hop[member] > distance) {
                selected_hop[member] = distance;
            }
        }
    }
    free(ordered);

    out_selection->selected_count = count;
    out_selection->truncated = truncated;
    return CRG_OK;
}
