#ifndef CRG_NEIGHBOURHOOD_H
#define CRG_NEIGHBOURHOOD_H

#include <stddef.h>
#include <stdint.h>

#include "code_review_graph/c_core_export.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Seeded k-hop neighbourhood, ported from ``code_review_graph/neighbourhood.py``.
 *
 * Ownership: every ``crg_*_init``/``_build`` writes into a caller-owned struct.
 * Caller-provided arrays are borrowed for the duration of the call only; the
 * library keeps its own copies of names but never takes ownership of caller
 * memory. Each struct has exactly one matching ``_clear``, and ``_clear`` is
 * safe on a zeroed struct and safe to call twice.
 */

/* Sentinel for "no parent" in the parent map. */
#define CRG_NO_NODE UINT32_MAX

typedef enum {
    CRG_OK = 0,
    CRG_ERR_NULL = 1,     /* a required pointer argument was NULL */
    CRG_ERR_SIZE = 2,     /* a count or capacity is inconsistent */
    CRG_ERR_RANGE = 3,    /* an id falls outside [0, node_count) */
    CRG_ERR_OVERFLOW = 4, /* a size computation would wrap */
    CRG_ERR_NOMEM = 5     /* an allocation failed; the struct is left cleared */
} crg_status;

/* Immutable view of the graph. ``names`` is borrowed by the library and must
 * outlive every call that receives this graph; each entry is a NUL-terminated
 * UTF-8 string, and ids are indexes into it. Edges stay in caller order:
 * parent-file resolution and degree parity depend on it. The edge arrays are
 * copied on init and freed by ``crg_graph_clear``. */
typedef struct {
    const char *const *names; /* [node_count] */
    size_t node_count;
    uint32_t *edge_source;    /* [edge_count] */
    uint32_t *edge_target;    /* [edge_count] */
    uint32_t *edge_kind;      /* [edge_count] */
    size_t edge_count;
} crg_graph;

/* Build the CSR view. ``edge_source``/``edge_target``/``edge_kind`` are borrowed
 * and read during the call only. Ids must be < node_count. On failure the
 * struct is left zeroed. */
CRG_API crg_status crg_graph_init(crg_graph *graph,
                          const char *const *names, size_t node_count,
                          const uint32_t *edge_source,
                          const uint32_t *edge_target,
                          const uint32_t *edge_kind,
                          size_t edge_count);

CRG_API void crg_graph_clear(crg_graph *graph);

/* Whole-graph degree per node: every edge increments both endpoints, so a self
 * loop counts twice. Sums are checked for uint32_t overflow. */
CRG_API crg_status crg_graph_degrees(const crg_graph *graph, uint32_t *out_degrees);

/* First containing File for each member node, by edge kind. ``member`` is a
 * [node_count] flag array. ``out_parent`` receives CRG_NO_NODE where absent.
 * The first matching edge in input order wins. */
CRG_API crg_status crg_graph_parent_files(const crg_graph *graph, uint32_t kind,
                                  const uint8_t *member, uint32_t *out_parent);

/* Adjacency restricted to *allowed* minus *excluded* edge kinds. Pass NULL/0 for
 * a kind set to mean "no restriction". Each row is deduplicated and sorted by
 * name so traversal order matches Python's ``sorted(adjacency[current])``.
 * ``present`` marks nodes touched by a surviving edge, mirroring the difference
 * between a missing key and an empty set in the Python map. Rows are stored in
 * a CSR array; ``offset`` is ``[node_count + 1]``. */
typedef struct {
    size_t *offset;             /* [node_count + 1] CSR offsets */
    uint32_t *target;           /* [total] sorted by name within each row */
    uint8_t *present;           /* [node_count] */
    size_t node_count;
    /* Internal scratch, NULL outside crg_adjacency_build. */
    size_t *row_length;
    size_t *row_length_final;
} crg_adjacency;

CRG_API crg_status crg_adjacency_build(const crg_graph *graph,
                               const uint32_t *allowed, size_t allowed_count,
                               const uint32_t *excluded, size_t excluded_count,
                               int directed, crg_adjacency *out_adjacency);

CRG_API void crg_adjacency_clear(crg_adjacency *adjacency);

/* Breadth-first hop distance from the seeds, capped at *depth*. ``out_hops`` is
 * [node_count] and receives -1 for unreachable nodes; ``out_order`` is
 * [node_count] and receives the reachable ids in discovery order (seeds first,
 * in the order given, repeats collapsed). Returns the number written to
 * ``out_order`` through ``out_reachable``. */
CRG_API crg_status crg_adjacency_hop_distances(const crg_adjacency *adjacency,
                                        const uint32_t *seeds, size_t seed_count,
                                        int32_t depth,
                                        int32_t *out_hops, uint32_t *out_order,
                                        size_t *out_reachable);

/* Shortest path through *kinds*, directed first then undirected. Returns the
 * path length; an unconnected pair yields 0. ``*out_directed`` reports which
 * pass produced the path (1 = directed), including the source == target case. */
CRG_API crg_status crg_graph_shortest_path(const crg_graph *graph,
                                   const uint32_t *kinds, size_t kind_count,
                                   uint32_t source, uint32_t target,
                                   uint32_t *out_path, size_t path_capacity,
                                   size_t *out_length, int *out_directed);

/* Apply the payload node budget. Returns the selected count; ``selected`` is a
 * [node_count] flag array and ``out_hops`` receives each selected node's hop. */
typedef struct {
    uint8_t *selected; /* caller-provided, [node_count] */
    int32_t *out_hops; /* caller-provided, [node_count] */
    size_t selected_count;
    int truncated;
} crg_selection;

CRG_API crg_status crg_select_within_budget(const crg_graph *graph,
                                    const int32_t *hops,
                                    const uint32_t *degrees,
                                    const uint32_t *parents,
                                    size_t max_nodes,
                                    const uint32_t *pinned, size_t pinned_count,
                                    crg_selection *out_selection);

#ifdef __cplusplus
}
#endif

#endif /* CRG_NEIGHBOURHOOD_H */
