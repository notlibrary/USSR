#include "gc.h"
#include "uno.h"

#include <stdlib.h>

/*
 * Mark-sweep garbage collector.
 *
 * Registry lives in uno.c (intrusive gc_next/gc_mark fields on
 * ussr_vector_t and ussr_struct_instance_t); this file implements the
 * classic three-phase cycle: clear marks, mark from roots, sweep.
 *
 * Sweep details: an unreachable cycle keeps its members' refcounts
 * above zero, so the sweep cannot rely on ussr_*_release(). Instead
 * it first walks the garbage set and manually decrements the refcount
 * of every reference-typed child held by a garbage parent (no cascade
 * — the children are in the same set and will be freed directly),
 * then frees every garbage object with the raw destroyers, which
 * release owned scalar strings but never touch child references.
 */

/* Collect roughly after this many fresh allocations. */
#define USSR_GC_ALLOC_THRESHOLD 4096UL

static unsigned long gc_alloc_count = 0;
static unsigned long gc_collected_total = 0;
static int gc_pending = 0;

void ussr_gc_init(void)
{
    gc_alloc_count = 0;
    gc_collected_total = 0;
    gc_pending = 0;
}

void ussr_gc_cleanup(void)
{
    gc_alloc_count = 0;
    gc_pending = 0;
}

void ussr_gc_note_alloc(void)
{
    if (++gc_alloc_count >= USSR_GC_ALLOC_THRESHOLD)
    {
        gc_alloc_count = 0;
        gc_pending = 1;
    }
}

void ussr_gc_request(void)
{
    gc_pending = 1;
}

int ussr_gc_pending(void)
{
    return gc_pending;
}

size_t ussr_gc_object_count(void)
{
    size_t count = 0;
    ussr_vector_t *v = ussr_uno_gc_vectors();
    ussr_struct_instance_t *s = ussr_uno_gc_instances();

    while (v != NULL) { ++count; v = v->gc_next; }
    while (s != NULL) { ++count; s = s->gc_next; }

    return count;
}

size_t ussr_gc_collected_total(void)
{
    return (size_t)gc_collected_total;
}

void ussr_gc_begin(void)
{
    ussr_vector_t *v = ussr_uno_gc_vectors();
    ussr_struct_instance_t *s = ussr_uno_gc_instances();

    while (v != NULL) { v->gc_mark = 0; v = v->gc_next; }
    while (s != NULL) { s->gc_mark = 0; s = s->gc_next; }
}

static void gc_mark_vector(ussr_vector_t *vector)
{
    size_t i;

    if (vector == NULL || vector->gc_mark)
        return;

    vector->gc_mark = 1;

    for (i = 0; i < vector->count; ++i)
        ussr_gc_mark_value(&vector->items[i]);
}

static void gc_mark_instance(ussr_struct_instance_t *instance)
{
    size_t i;

    if (instance == NULL || instance->gc_mark)
        return;

    instance->gc_mark = 1;

    for (i = 0; i < instance->field_count; ++i)
        ussr_gc_mark_value(&instance->fields[i]);
}

void ussr_gc_mark_value(const ussr_value_t *value)
{
    if (value == NULL)
        return;

    if (value->type == USSR_VECTOR)
        gc_mark_vector(value->data.vector);
    else if (value->type == USSR_STRUCT)
        gc_mark_instance(value->data.instance);
}

/*
 * Drop one reference held by a garbage parent to a garbage child,
 * without cascading (the child is reclaimed directly by the sweep).
 */
static void gc_unref_garbage_child(const ussr_value_t *value)
{
    if (value->type == USSR_VECTOR &&
        value->data.vector != NULL &&
        !value->data.vector->gc_mark &&
        value->data.vector->refcount > 0)
    {
        --value->data.vector->refcount;
    }
    else if (value->type == USSR_STRUCT &&
             value->data.instance != NULL &&
             !value->data.instance->gc_mark &&
             value->data.instance->refcount > 0)
    {
        --value->data.instance->refcount;
    }
}

size_t ussr_gc_sweep(void)
{
    size_t collected = 0;
    ussr_vector_t *v;
    ussr_struct_instance_t *s;
    size_t i;

    /* Pass 1: detach garbage-to-garbage child references. */
    for (v = ussr_uno_gc_vectors(); v != NULL; v = v->gc_next)
    {
        if (v->gc_mark)
            continue;
        for (i = 0; i < v->count; ++i)
            gc_unref_garbage_child(&v->items[i]);
    }
    for (s = ussr_uno_gc_instances(); s != NULL; s = s->gc_next)
    {
        if (s->gc_mark)
            continue;
        for (i = 0; i < s->field_count; ++i)
            gc_unref_garbage_child(&s->fields[i]);
    }

    /* Pass 2: reclaim. Unlink from the registry while iterating. */
    v = ussr_uno_gc_vectors();
    while (v != NULL)
    {
        ussr_vector_t *next = v->gc_next;
        if (!v->gc_mark)
        {
            ussr_uno_gc_unregister_vector(v);
            ussr_uno_vector_destroy_raw(v);
            ++collected;
        }
        v = next;
    }

    s = ussr_uno_gc_instances();
    while (s != NULL)
    {
        ussr_struct_instance_t *next = s->gc_next;
        if (!s->gc_mark)
        {
            ussr_uno_gc_unregister_instance(s);
            ussr_uno_instance_destroy_raw(s);
            ++collected;
        }
        s = next;
    }

    gc_collected_total += collected;
    gc_pending = 0;
    gc_alloc_count = 0;

    return collected;
}
