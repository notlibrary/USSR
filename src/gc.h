#ifndef USSR_GC_H
#define USSR_GC_H

#include <stddef.h>
#include "ussr.h"

/*
 * Mark-sweep garbage collector for USSR heap objects (vectors and
 * struct instances, both defined in uno.h).
 *
 * The interpreter already reference-counts those objects; the
 * collector exists to reclaim reference CYCLES and anything the
 * refcount graph can no longer reach. Every live object sits in an
 * intrusive registry owned by uno.c.
 *
 * A collection cycle is split in three:
 *
 *   ussr_gc_begin()          - clear all mark bits
 *   ussr_gc_mark_value(...)  - called by the root provider (the
 *                              scheduler / runtime) for every root
 *                              value: all process variable scopes, VM
 *                              registers, frame stacks, hash entries
 *   ussr_gc_sweep()          - destroy everything still unmarked
 *
 * Collection must only run at scheduler slice boundaries (between
 * process time slices, never mid-instruction), because C locals
 * inside the VM dispatch loop may hold the only reference to a fresh
 * object. gc() command and allocation-threshold triggers therefore
 * just set a pending flag; the scheduler performs the actual cycle.
 *
 * This collector is fresh code: no external GC library is used.
 */

void ussr_gc_init(void);
void ussr_gc_cleanup(void);

/* Allocation accounting: uno.c creation paths call note_alloc via the
 * registry hooks; when the threshold is hit a collection is deferred
 * to the next scheduler boundary. */
void ussr_gc_note_alloc(void);

/* Request a collection at the next safe point (gc() command). */
void ussr_gc_request(void);

/* Non-zero when a collection was requested or the allocation
 * threshold tripped. */
int ussr_gc_pending(void);

/* Cycle phases. mark_value is recursive over vector items and struct
 * fields; the mark bit makes cycles safe. */
void ussr_gc_begin(void);
void ussr_gc_mark_value(const ussr_value_t *value);

/* Sweep: returns the number of objects reclaimed. */
size_t ussr_gc_sweep(void);

/* Statistics (gc_stats() command / diagnostics). */
size_t ussr_gc_object_count(void);
size_t ussr_gc_collected_total(void);

#endif
