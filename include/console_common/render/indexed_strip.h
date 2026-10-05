#ifndef CONSOLE_COMMON_RENDER_INDEXED_STRIP_H
#define CONSOLE_COMMON_RENDER_INDEXED_STRIP_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Derived, once-only transport of an explicitly identified U32 triangle strip
 * with fixed UINT32_MAX restart. Source borrows the supplied index owner
 * and its original BE32 backing; the owner must outlive this call and keep
 * that complete view unchanged across validation and conversion.
 * Triangles is the host U16 list consumed by the shared indexed backend.
 * Capacity and written are U16 index counts. Every triangle, including
 * duplicate-index degenerates, is retained in order.
 * Restarts discard incomplete primitives and reset strip parity. The current
 * vertex stays last, preserving the strip's provoking-vertex ordering.
 * Source, complete output capacity and count are bounded and disjoint. Failure
 * preserves both writable outputs. No allocation, callback or frame work occurs.
 * The caller supplies proven topology/restart state and valid stable storage;
 * this does not identify a native topology or establish physical GPU fidelity. */
bool cc_indexed_strip_be32_to_triangles(const uint8_t *source, size_t source_size,
                                        size_t index_count, size_t vertex_count,
                                        uint16_t *triangles, size_t triangle_capacity,
                                        size_t *written);

#endif
