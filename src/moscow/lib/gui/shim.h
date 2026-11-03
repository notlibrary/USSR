/* shim.h - what petersburg.c takes from the hosted ussr (src/ussr.h,
 * src/scheduler.h, src/eventq.h), for Moscow: values as the C side of a
 * native sees them. Its own headers are kept out by their guards. */
#ifndef MOSCOW_GUI_SHIM_H
#define MOSCOW_GUI_SHIM_H

#define USSR_PETERSBURG_H
#define USSR_EVENTQ_H

#include <stddef.h>
#include <stdlib.h>
#include <string.h>

typedef long long ussr_int_t;

typedef enum
{
    USSR_NULL,
    USSR_INTEGER,
    USSR_REAL,
    USSR_STRING,
    USSR_BOOLEAN,
    USSR_VECTOR,
    USSR_STRUCT,
    USSR_BTREE
} ussr_value_type_t;

typedef struct ussr_vector_t ussr_vector_t;

typedef struct
{
    ussr_value_type_t type;
    union
    {
        ussr_int_t integer;
        double real;
        char *string;
        int boolean;
        ussr_vector_t *vector;
    } data;
} ussr_value_t;

struct ussr_vector_t
{
    ussr_value_t *items;
    size_t count;
    size_t capacity;
};

typedef struct { const char *name; } ussr_bc_function_t;
typedef struct
{
    struct
    {
        size_t function_count;
        ussr_bc_function_t *functions;
    } bc;
} ussr_sched_program_t;
typedef struct ussr_proc ussr_proc_t;

static inline ussr_value_t ussr_integer(ussr_int_t v)
{
    ussr_value_t x;
    x.type = USSR_INTEGER;
    x.data.integer = v;
    return x;
}

static inline ussr_value_t ussr_real(double v)
{
    ussr_value_t x;
    x.type = USSR_REAL;
    x.data.real = v;
    return x;
}

static inline ussr_value_t ussr_boolean(int v)
{
    ussr_value_t x;
    x.type = USSR_BOOLEAN;
    x.data.boolean = v != 0;
    return x;
}

static inline ussr_value_t ussr_string(const char *s)
{
    ussr_value_t x;
    size_t n = strlen(s != NULL ? s : "");
    x.type = USSR_STRING;
    x.data.string = malloc(n + 1);
    if (x.data.string != NULL)
        memcpy(x.data.string, s != NULL ? s : "", n + 1);
    return x;
}

static void ussr_vector_release(ussr_vector_t *v);

static inline void ussr_value_free(ussr_value_t *x)
{
    if (x->type == USSR_STRING)
        free(x->data.string);
    else if (x->type == USSR_VECTOR)
        ussr_vector_release(x->data.vector);
    x->type = USSR_NULL;
}

static inline ussr_vector_t *ussr_vector_create(void *unused)
{
    (void)unused;
    return calloc(1, sizeof(ussr_vector_t));
}

static inline int ussr_vector_push(ussr_vector_t *v, ussr_value_t x)
{
    if (v->count == v->capacity)
    {
        size_t cap = v->capacity ? v->capacity * 2 : 8;
        ussr_value_t *items = realloc(v->items, cap * sizeof(*items));
        if (items == NULL)
            return -1;
        v->items = items;
        v->capacity = cap;
    }
    /* strings are copied: the caller frees its own */
    if (x.type == USSR_STRING)
        x = ussr_string(x.data.string);
    v->items[v->count++] = x;
    return 0;
}

static void ussr_vector_release(ussr_vector_t *v)
{
    size_t i;
    if (v == NULL)
        return;
    for (i = 0; i < v->count; ++i)
        ussr_value_free(&v->items[i]);
    free(v->items);
    free(v);
}

static inline ussr_value_t ussr_vector_value(ussr_vector_t *v)
{
    ussr_value_t x;
    x.type = USSR_VECTOR;
    x.data.vector = v;
    return x;
}

static inline int ussr_sched_spawn_func(ussr_proc_t *parent, ussr_sched_program_t *program,
                                        size_t function, ussr_value_t *arguments,
                                        size_t count, const char *name, int fork)
{
    (void)parent; (void)program; (void)function; (void)arguments;
    (void)count; (void)name; (void)fork;
    return -1;
}

static inline long ussr_eq_next_timeout_ms(void) { return 0; }

int ussr_petersburg_dispatch(ussr_sched_program_t *program, const char *name,
                             const ussr_value_t *values, size_t value_count,
                             ussr_value_t *out);
int ussr_petersburg_pump(ussr_sched_program_t *program, ussr_proc_t *parent,
                         int wait_for_events);
int ussr_petersburg_running(void);
void ussr_petersburg_cleanup(void);

#endif
