#include "uno.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

/* ------------------------------------------------------------- */
/* small local helpers                                            */
/* ------------------------------------------------------------- */

static char *uno_strdup(const char *s)
{
    size_t n;
    char *copy;

    if (s == NULL)
        return NULL;

    n = strlen(s);
    copy = malloc(n + 1);
    if (copy == NULL)
        return NULL;

    memcpy(copy, s, n + 1);
    return copy;
}

static char *uno_strndup(const char *s, size_t n)
{
    char *copy = malloc(n + 1);
    if (copy == NULL)
        return NULL;
    memcpy(copy, s, n);
    copy[n] = '\0';
    return copy;
}

/* Growable output buffer, same shape as pp.c's pp_append pattern. */
typedef struct
{
    char *data;
    size_t length;
    size_t capacity;
} uno_buf_t;

static int uno_buf_init(uno_buf_t *b)
{
    b->capacity = 128;
    b->length = 0;
    b->data = malloc(b->capacity);
    if (b->data == NULL)
        return -1;
    b->data[0] = '\0';
    return 0;
}

static int uno_buf_append(uno_buf_t *b, const char *text)
{
    size_t len = strlen(text);
    size_t required = b->length + len + 1;

    if (required > b->capacity)
    {
        size_t new_capacity = b->capacity ? b->capacity : 128;
        char *new_data;

        while (new_capacity < required)
            new_capacity *= 2;

        new_data = realloc(b->data, new_capacity);
        if (new_data == NULL)
            return -1;

        b->data = new_data;
        b->capacity = new_capacity;
    }

    memcpy(b->data + b->length, text, len + 1);
    b->length += len;
    return 0;
}

/* Owning copy of a value for storage in a struct field / vector slot.
 * Scalars are duplicated; vectors/instances are reference-counted, so
 * storing one just retains it. */
static int uno_store_value(ussr_value_t *dst, ussr_value_t src)
{
    switch (src.type)
    {
    case USSR_STRING:
        dst->type = USSR_STRING;
        dst->data.string = uno_strdup(src.data.string);
        return dst->data.string != NULL ? 0 : -1;

    case USSR_VECTOR:
        ussr_vector_retain(src.data.vector);
        *dst = src;
        return 0;

    case USSR_STRUCT:
        ussr_struct_instance_retain(src.data.instance);
        *dst = src;
        return 0;

    default:
        *dst = src;
        return 0;
    }
}

static void uno_release_value(ussr_value_t *v)
{
    switch (v->type)
    {
    case USSR_STRING:
        free(v->data.string);
        break;
    case USSR_VECTOR:
        ussr_vector_release(v->data.vector);
        break;
    case USSR_STRUCT:
        ussr_struct_instance_release(v->data.instance);
        break;
    default:
        break;
    }
}

/* ------------------------------------------------------------- */
/* struct type registry                                           */
/* ------------------------------------------------------------- */

static ussr_struct_type_t *g_struct_types = NULL;

ussr_struct_type_t *ussr_struct_type_find(const char *name)
{
    ussr_struct_type_t *t;
    for (t = g_struct_types; t != NULL; t = t->next)
        if (strcmp(t->name, name) == 0)
            return t;
    return NULL;
}

size_t ussr_struct_total_field_count(const ussr_struct_type_t *type)
{
    size_t count = type->field_count;
    if (type->parent != NULL)
        count += ussr_struct_total_field_count(type->parent);
    return count;
}

int ussr_struct_type_is(const ussr_struct_type_t *type, const char *of_name)
{
    for (; type != NULL; type = type->parent)
        if (strcmp(type->name, of_name) == 0)
            return 1;
    return 0;
}

/* "name:type" -> out_name/out_type (both owned copies). Returns 0/-1. */
static int uno_parse_field_spec(
    const char *spec,
    char **out_name,
    char **out_type
)
{
    const char *colon = strchr(spec, ':');

    if (colon == NULL || colon == spec || colon[1] == '\0')
    {
        fprintf(stderr, "USSR: malformed struct field spec '%s' (want \"name:type\")\n", spec);
        return -1;
    }

    *out_name = uno_strndup(spec, (size_t)(colon - spec));
    *out_type = uno_strdup(colon + 1);

    if (*out_name == NULL || *out_type == NULL)
    {
        free(*out_name);
        free(*out_type);
        return -1;
    }

    return 0;
}

int ussr_struct_register(
    const char *name,
    const char *parent_name,
    char **field_specs,
    size_t field_count
)
{
    ussr_struct_type_t *type;
    ussr_struct_type_t *parent = NULL;
    size_t i;

    if (ussr_struct_type_find(name) != NULL)
    {
        fprintf(stderr, "USSR: struct '%s' already defined\n", name);
        return -1;
    }

    if (parent_name != NULL && parent_name[0] != '\0')
    {
        parent = ussr_struct_type_find(parent_name);
        if (parent == NULL)
        {
            fprintf(stderr, "USSR: struct '%s' extends unknown struct '%s' "
                            "(parent must be declared first)\n", name, parent_name);
            return -1;
        }
    }

    type = calloc(1, sizeof(*type));
    if (type == NULL)
        return -1;

    type->name = uno_strdup(name);
    type->parent_name = parent_name != NULL ? uno_strdup(parent_name) : NULL;
    type->parent = parent;

    if (type->name == NULL)
    {
        free(type);
        return -1;
    }

    for (i = 0; i < field_count; ++i)
    {
        ussr_struct_field_def_t *def = calloc(1, sizeof(*def));

        if (def == NULL)
            return -1;

        if (uno_parse_field_spec(field_specs[i], &def->name, &def->type_name) != 0)
        {
            free(def);
            return -1;
        }

        if (type->fields_tail == NULL)
            type->fields_head = def;
        else
            type->fields_tail->next = def;

        type->fields_tail = def;
        type->field_count++;
    }

    type->next = g_struct_types;
    g_struct_types = type;

    return 0;
}

/* Locates a field anywhere in the parent chain. Returns 1 and sets
 * *out_index (absolute, parent-first) and *out_type_name on success. */
static int uno_field_locate(
    const ussr_struct_type_t *type,
    const char *name,
    size_t *out_index,
    const char **out_type_name
)
{
    size_t base = 0;
    const ussr_struct_field_def_t *def;
    size_t i;

    if (type->parent != NULL)
    {
        if (uno_field_locate(type->parent, name, out_index, out_type_name))
            return 1;
        base = ussr_struct_total_field_count(type->parent);
    }

    i = 0;
    for (def = type->fields_head; def != NULL; def = def->next, ++i)
    {
        if (strcmp(def->name, name) == 0)
        {
            *out_index = base + i;
            *out_type_name = def->type_name;
            return 1;
        }
    }

    return 0;
}

/* Writes ordered (parent-first) field defs into out[], which must have
 * room for ussr_struct_total_field_count(type) entries. */
static void uno_collect_field_defs(
    const ussr_struct_type_t *type,
    const ussr_struct_field_def_t **out,
    size_t *cursor
)
{
    const ussr_struct_field_def_t *def;

    if (type->parent != NULL)
        uno_collect_field_defs(type->parent, out, cursor);

    for (def = type->fields_head; def != NULL; def = def->next)
        out[(*cursor)++] = def;
}

static int uno_type_matches(const char *type_name, const ussr_value_t *value)
{
    if (value->type == USSR_NULL)
        return 1; /* v1: every field is implicitly nullable */

    if (strcmp(type_name, "any") == 0)
        return 1;
    if (strcmp(type_name, "number") == 0)
        return value->type == USSR_INTEGER || value->type == USSR_REAL;
    if (strcmp(type_name, "string") == 0)
        return value->type == USSR_STRING;
    if (strcmp(type_name, "boolean") == 0)
        return value->type == USSR_BOOLEAN;
    if (strcmp(type_name, "null") == 0)
        return value->type == USSR_NULL;
    if (strcmp(type_name, "vector") == 0)
        return value->type == USSR_VECTOR;

    /* Otherwise type_name names a struct; accept it or any subtype. */
    return value->type == USSR_STRUCT &&
           ussr_struct_type_is(value->data.instance->type, type_name);
}

/* ------------------------------------------------------------- */
/* struct instances                                                */
/* ------------------------------------------------------------- */

static ussr_struct_instance_t *uno_alloc_instance(
    ussr_struct_type_t *type
)
{
    ussr_struct_instance_t *instance = calloc(1, sizeof(*instance));
    size_t total;

    if (instance == NULL)
        return NULL;

    total = ussr_struct_total_field_count(type);

    instance->type = type;
    instance->field_count = total;
    instance->refcount = 1;

    if (total > 0)
    {
        instance->fields = calloc(total, sizeof(*instance->fields));
        if (instance->fields == NULL)
        {
            free(instance);
            return NULL;
        }
    }

    return instance;
}

ussr_struct_instance_t *ussr_struct_instantiate(
    const char *type_name,
    const ussr_value_t *args,
    size_t argument_count
)
{
    ussr_struct_type_t *type = ussr_struct_type_find(type_name);
    const ussr_struct_field_def_t **defs;
    ussr_struct_instance_t *instance;
    size_t total, i, cursor = 0;

    if (type == NULL)
    {
        fprintf(stderr, "USSR: unknown struct type '%s'\n", type_name);
        return NULL;
    }

    total = ussr_struct_total_field_count(type);

    if (total != argument_count)
    {
        fprintf(stderr, "USSR: %s(...) expects %zu argument(s), got %zu\n",
                type_name, total, argument_count);
        return NULL;
    }

    instance = uno_alloc_instance(type);
    if (instance == NULL)
        return NULL;

    if (total == 0)
        return instance;

    defs = malloc(total * sizeof(*defs));
    if (defs == NULL)
    {
        ussr_struct_instance_release(instance);
        return NULL;
    }
    uno_collect_field_defs(type, defs, &cursor);

    for (i = 0; i < total; ++i)
    {
        if (!uno_type_matches(defs[i]->type_name, &args[i]))
        {
            fprintf(stderr, "USSR: %s.%s expects type '%s'\n",
                    type_name, defs[i]->name, defs[i]->type_name);
            free(defs);
            ussr_struct_instance_release(instance);
            return NULL;
        }

        if (uno_store_value(&instance->fields[i], args[i]) != 0)
        {
            free(defs);
            ussr_struct_instance_release(instance);
            return NULL;
        }
    }

    free(defs);
    return instance;
}

ussr_struct_instance_t *ussr_struct_instantiate_named(
    const char *type_name,
    const char * const *field_names,
    const ussr_value_t *field_values,
    size_t count
)
{
    ussr_struct_type_t *type = ussr_struct_type_find(type_name);
    ussr_struct_instance_t *instance;
    size_t total, i;
    char *satisfied;

    if (type == NULL)
    {
        fprintf(stderr, "USSR: unknown struct type '%s'\n", type_name);
        return NULL;
    }

    total = ussr_struct_total_field_count(type);
    instance = uno_alloc_instance(type);
    if (instance == NULL)
        return NULL;

    satisfied = calloc(total > 0 ? total : 1, 1);
    if (satisfied == NULL)
    {
        ussr_struct_instance_release(instance);
        return NULL;
    }

    for (i = 0; i < count; ++i)
    {
        size_t index;
        const char *field_type;

        if (!uno_field_locate(type, field_names[i], &index, &field_type))
        {
            fprintf(stderr, "USSR: %s has no field '%s'\n", type_name, field_names[i]);
            free(satisfied);
            ussr_struct_instance_release(instance);
            return NULL;
        }

        if (!uno_type_matches(field_type, &field_values[i]))
        {
            fprintf(stderr, "USSR: %s.%s expects type '%s'\n",
                    type_name, field_names[i], field_type);
            free(satisfied);
            ussr_struct_instance_release(instance);
            return NULL;
        }

        if (uno_store_value(&instance->fields[index], field_values[i]) != 0)
        {
            free(satisfied);
            ussr_struct_instance_release(instance);
            return NULL;
        }

        satisfied[index] = 1;
    }

    for (i = 0; i < total; ++i)
    {
        if (!satisfied[i])
        {
            fprintf(stderr, "USSR: %s literal is missing field(s)\n", type_name);
            free(satisfied);
            ussr_struct_instance_release(instance);
            return NULL;
        }
    }

    free(satisfied);
    return instance;
}

void ussr_struct_instance_retain(ussr_struct_instance_t *instance)
{
    if (instance != NULL)
        instance->refcount++;
}

void ussr_struct_instance_release(ussr_struct_instance_t *instance)
{
    size_t i;

    if (instance == NULL)
        return;

    if (--instance->refcount > 0)
        return;

    for (i = 0; i < instance->field_count; ++i)
        uno_release_value(&instance->fields[i]);

    free(instance->fields);
    free(instance);
}

int ussr_struct_get_field(
    const ussr_struct_instance_t *instance,
    const char *field_name,
    ussr_value_t *out
)
{
    size_t index;
    const char *field_type;

    if (!uno_field_locate(instance->type, field_name, &index, &field_type))
        return 0;

    return uno_store_value(out, instance->fields[index]) == 0;
}

int ussr_struct_set_field(
    ussr_struct_instance_t *instance,
    const char *field_name,
    ussr_value_t value
)
{
    size_t index;
    const char *field_type;
    ussr_value_t stored;

    if (!uno_field_locate(instance->type, field_name, &index, &field_type))
    {
        fprintf(stderr, "USSR: %s has no field '%s'\n", instance->type->name, field_name);
        return -1;
    }

    if (!uno_type_matches(field_type, &value))
    {
        fprintf(stderr, "USSR: %s.%s expects type '%s'\n",
                instance->type->name, field_name, field_type);
        return -1;
    }

    if (uno_store_value(&stored, value) != 0)
        return -1;

    uno_release_value(&instance->fields[index]);
    instance->fields[index] = stored;
    return 0;
}

/* ------------------------------------------------------------- */
/* methods                                                         */
/* ------------------------------------------------------------- */

typedef struct ussr_method_t
{
    char *type_name;
    char *method_name;
    ussr_command_list_t *body;
    struct ussr_method_t *next;
} ussr_method_t;

static ussr_method_t *g_methods = NULL;

int ussr_method_register(
    const char *type_name,
    const char *method_name,
    ussr_command_list_t *body
)
{
    ussr_method_t *m;

    if (ussr_struct_type_find(type_name) == NULL)
    {
        fprintf(stderr, "USSR: method registered for unknown struct '%s'\n", type_name);
        return -1;
    }

    m = calloc(1, sizeof(*m));
    if (m == NULL)
        return -1;

    m->type_name = uno_strdup(type_name);
    m->method_name = uno_strdup(method_name);
    m->body = body;

    if (m->type_name == NULL || m->method_name == NULL)
    {
        free(m->type_name);
        free(m->method_name);
        free(m);
        return -1;
    }

    m->next = g_methods;
    g_methods = m;
    return 0;
}

static ussr_command_list_t *uno_method_lookup_by_name(
    const char *type_name,
    const char *method_name
)
{
    ussr_method_t *m;
    for (m = g_methods; m != NULL; m = m->next)
        if (strcmp(m->type_name, type_name) == 0 &&
            strcmp(m->method_name, method_name) == 0)
            return m->body;
    return NULL;
}

ussr_command_list_t *ussr_method_lookup(
    const ussr_struct_type_t *type,
    const char *method_name
)
{
    for (; type != NULL; type = type->parent)
    {
        ussr_command_list_t *body = uno_method_lookup_by_name(type->name, method_name);
        if (body != NULL)
            return body;
    }
    return NULL;
}

/* ------------------------------------------------------------- */
/* vectors                                                         */
/* ------------------------------------------------------------- */

ussr_vector_t *ussr_vector_create(const char *element_type)
{
    ussr_vector_t *v = calloc(1, sizeof(*v));
    if (v == NULL)
        return NULL;

    if (element_type != NULL && element_type[0] != '\0')
    {
        v->element_type = uno_strdup(element_type);
        if (v->element_type == NULL)
        {
            free(v);
            return NULL;
        }
    }

    v->refcount = 1;
    return v;
}

void ussr_vector_retain(ussr_vector_t *vector)
{
    if (vector != NULL)
        vector->refcount++;
}

void ussr_vector_release(ussr_vector_t *vector)
{
    size_t i;

    if (vector == NULL)
        return;

    if (--vector->refcount > 0)
        return;

    for (i = 0; i < vector->count; ++i)
        uno_release_value(&vector->items[i]);

    free(vector->items);
    free(vector->element_type);
    free(vector);
}

int ussr_vector_push(ussr_vector_t *vector, ussr_value_t item)
{
    ussr_value_t stored;

    if (vector->element_type != NULL &&
        !uno_type_matches(vector->element_type, &item))
    {
        fprintf(stderr, "USSR: vector expects element type '%s'\n", vector->element_type);
        return -1;
    }

    if (vector->count == vector->capacity)
    {
        size_t new_capacity = vector->capacity ? vector->capacity * 2 : 8;
        ussr_value_t *new_items = realloc(vector->items, new_capacity * sizeof(*new_items));

        if (new_items == NULL)
            return -1;

        vector->items = new_items;
        vector->capacity = new_capacity;
    }

    if (uno_store_value(&stored, item) != 0)
        return -1;

    vector->items[vector->count++] = stored;
    return 0;
}

int ussr_vector_get(const ussr_vector_t *vector, size_t index, ussr_value_t *out)
{
    if (index >= vector->count)
        return 0;
    return uno_store_value(out, vector->items[index]) == 0;
}

size_t ussr_vector_length(const ussr_vector_t *vector)
{
    return vector->count;
}

/* ------------------------------------------------------------- */
/* UNO encode                                                       */
/* ------------------------------------------------------------- */

static int uno_encode_value(uno_buf_t *buf, const ussr_value_t *value);

static int uno_encode_number(uno_buf_t *buf, double real, int is_integer, long integer)
{
    char tmp[64];

    if (is_integer)
    {
        snprintf(tmp, sizeof(tmp), "%ld", integer);
        return uno_buf_append(buf, tmp);
    }

    snprintf(tmp, sizeof(tmp), "%g", real);
    if (strchr(tmp, '.') == NULL && strchr(tmp, 'e') == NULL && strchr(tmp, 'n') == NULL)
        strcat(tmp, ".0");

    return uno_buf_append(buf, tmp);
}

static int uno_encode_struct(uno_buf_t *buf, const ussr_struct_instance_t *instance)
{
    const ussr_struct_field_def_t **defs;
    size_t total = instance->field_count;
    size_t cursor = 0, i;

    if (uno_buf_append(buf, instance->type->name) != 0)
        return -1;
    if (uno_buf_append(buf, "{") != 0)
        return -1;

    if (total > 0)
    {
        defs = malloc(total * sizeof(*defs));
        if (defs == NULL)
            return -1;
        uno_collect_field_defs(instance->type, defs, &cursor);

        for (i = 0; i < total; ++i)
        {
            if (i > 0 && uno_buf_append(buf, ",") != 0)
            {
                free(defs);
                return -1;
            }
            if (uno_buf_append(buf, defs[i]->name) != 0 ||
                uno_buf_append(buf, "=") != 0 ||
                uno_encode_value(buf, &instance->fields[i]) != 0)
            {
                free(defs);
                return -1;
            }
        }

        free(defs);
    }

    return uno_buf_append(buf, "}");
}

static int uno_encode_vector(uno_buf_t *buf, const ussr_vector_t *vector)
{
    size_t i;

    if (uno_buf_append(buf, "[") != 0)
        return -1;

    for (i = 0; i < vector->count; ++i)
    {
        if (i > 0 && uno_buf_append(buf, ",") != 0)
            return -1;
        if (uno_encode_value(buf, &vector->items[i]) != 0)
            return -1;
    }

    return uno_buf_append(buf, "]");
}

static int uno_encode_value(uno_buf_t *buf, const ussr_value_t *value)
{
    switch (value->type)
    {
    case USSR_NULL:
        return uno_buf_append(buf, "null");

    case USSR_INTEGER:
        return uno_encode_number(buf, 0.0, 1, value->data.integer);

    case USSR_REAL:
        return uno_encode_number(buf, value->data.real, 0, 0);

    case USSR_BOOLEAN:
        return uno_buf_append(buf, value->data.boolean ? "true" : "false");

    case USSR_STRING:
        if (strchr(value->data.string, '`') != NULL ||
            strchr(value->data.string, '\n') != NULL)
        {
            fprintf(stderr, "USSR: cannot encode string containing '`' or a newline in UNO\n");
            return -1;
        }
        return uno_buf_append(buf, "`") == 0 &&
               uno_buf_append(buf, value->data.string) == 0 &&
               uno_buf_append(buf, "`") == 0 ? 0 : -1;

    case USSR_VECTOR:
        return uno_encode_vector(buf, value->data.vector);

    case USSR_STRUCT:
        return uno_encode_struct(buf, value->data.instance);
    }

    return -1;
}

char *ussr_uno_encode(const ussr_value_t *value)
{
    uno_buf_t buf;

    if (uno_buf_init(&buf) != 0)
        return NULL;

    if (uno_encode_value(&buf, value) != 0)
    {
        free(buf.data);
        return NULL;
    }

    return buf.data;
}

/* ------------------------------------------------------------- */
/* UNO decode (hand-rolled recursive descent)                      */
/* ------------------------------------------------------------- */

typedef struct
{
    const char *cursor;
} uno_reader_t;

static void uno_skip_ws(uno_reader_t *r)
{
    while (isspace((unsigned char)*r->cursor))
        r->cursor++;
}

static int uno_read_identifier(uno_reader_t *r, char **out)
{
    const char *start = r->cursor;

    if (!isalpha((unsigned char)*r->cursor) && *r->cursor != '_')
        return -1;

    while (isalnum((unsigned char)*r->cursor) || *r->cursor == '_')
        r->cursor++;

    *out = uno_strndup(start, (size_t)(r->cursor - start));
    return *out != NULL ? 0 : -1;
}

static int uno_read_number(uno_reader_t *r, ussr_value_t *out)
{
    const char *start = r->cursor;
    int is_real = 0;

    if (*r->cursor == '-')
        r->cursor++;

    if (!isdigit((unsigned char)*r->cursor))
    {
        r->cursor = start;
        return -1;
    }

    while (isdigit((unsigned char)*r->cursor))
        r->cursor++;

    if (*r->cursor == '.' && isdigit((unsigned char)r->cursor[1]))
    {
        is_real = 1;
        r->cursor++;
        while (isdigit((unsigned char)*r->cursor))
            r->cursor++;
    }

    if (is_real)
    {
        out->type = USSR_REAL;
        out->data.real = strtod(start, NULL);
    }
    else
    {
        out->type = USSR_INTEGER;
        out->data.integer = strtol(start, NULL, 10);
    }

    return 0;
}

static int uno_read_string(uno_reader_t *r, ussr_value_t *out)
{
    const char *start;

    if (*r->cursor != '`')
        return -1;

    r->cursor++;
    start = r->cursor;

    while (*r->cursor != '`')
    {
        if (*r->cursor == '\0' || *r->cursor == '\n')
        {
            fprintf(stderr, "USSR: unterminated UNO string\n");
            return -1;
        }
        r->cursor++;
    }

    out->type = USSR_STRING;
    out->data.string = uno_strndup(start, (size_t)(r->cursor - start));
    r->cursor++; /* closing backtick */

    return out->data.string != NULL ? 0 : -1;
}

static int uno_read_value(uno_reader_t *r, ussr_value_t *out);

static int uno_read_vector(uno_reader_t *r, ussr_value_t *out)
{
    ussr_vector_t *vector;

    if (*r->cursor != '[')
        return -1;
    r->cursor++;

    vector = ussr_vector_create(NULL);
    if (vector == NULL)
        return -1;

    uno_skip_ws(r);
    if (*r->cursor == ']')
    {
        r->cursor++;
        out->type = USSR_VECTOR;
        out->data.vector = vector;
        return 0;
    }

    for (;;)
    {
        ussr_value_t item;

        uno_skip_ws(r);
        if (uno_read_value(r, &item) != 0)
        {
            ussr_vector_release(vector);
            return -1;
        }

        if (ussr_vector_push(vector, item) != 0)
        {
            uno_release_value(&item);
            ussr_vector_release(vector);
            return -1;
        }
        uno_release_value(&item); /* push retained/copied its own storage */

        uno_skip_ws(r);
        if (*r->cursor == ',')
        {
            r->cursor++;
            continue;
        }
        if (*r->cursor == ']')
        {
            r->cursor++;
            break;
        }

        fprintf(stderr, "USSR: expected ',' or ']' in UNO vector\n");
        ussr_vector_release(vector);
        return -1;
    }

    out->type = USSR_VECTOR;
    out->data.vector = vector;
    return 0;
}

#define UNO_MAX_FIELDS 64

static int uno_read_object(uno_reader_t *r, char *type_name, ussr_value_t *out)
{
    char *names[UNO_MAX_FIELDS];
    ussr_value_t values[UNO_MAX_FIELDS];
    size_t count = 0;
    ussr_struct_instance_t *instance;
    size_t i;

    if (*r->cursor != '{')
    {
        free(type_name);
        return -1;
    }
    r->cursor++;

    uno_skip_ws(r);
    if (*r->cursor != '}')
    {
        for (;;)
        {
            if (count >= UNO_MAX_FIELDS)
            {
                fprintf(stderr, "USSR: UNO object exceeds %d fields\n", UNO_MAX_FIELDS);
                goto fail;
            }

            uno_skip_ws(r);
            if (uno_read_identifier(r, &names[count]) != 0)
            {
                fprintf(stderr, "USSR: expected field name in UNO object\n");
                goto fail;
            }

            uno_skip_ws(r);
            if (*r->cursor != '=')
            {
                fprintf(stderr, "USSR: expected '=' after field name '%s'\n", names[count]);
                free(names[count]);
                goto fail;
            }
            r->cursor++;

            uno_skip_ws(r);
            if (uno_read_value(r, &values[count]) != 0)
            {
                free(names[count]);
                goto fail;
            }

            count++;

            uno_skip_ws(r);
            if (*r->cursor == ',')
            {
                r->cursor++;
                continue;
            }
            if (*r->cursor == '}')
                break;

            fprintf(stderr, "USSR: expected ',' or '}' in UNO object\n");
            goto fail;
        }
    }
    r->cursor++; /* closing brace */

    {
        const char *name_ptrs[UNO_MAX_FIELDS];
        for (i = 0; i < count; ++i)
            name_ptrs[i] = names[i];

        instance = ussr_struct_instantiate_named(type_name, name_ptrs, values, count);
    }

    for (i = 0; i < count; ++i)
    {
        free(names[i]);
        uno_release_value(&values[i]);
    }
    free(type_name);

    if (instance == NULL)
        return -1;

    out->type = USSR_STRUCT;
    out->data.instance = instance;
    return 0;

fail:
    for (i = 0; i < count; ++i)
    {
        free(names[i]);
        uno_release_value(&values[i]);
    }
    free(type_name);
    return -1;
}

static int uno_read_value(uno_reader_t *r, ussr_value_t *out)
{
    uno_skip_ws(r);

    if (*r->cursor == '[')
        return uno_read_vector(r, out);

    if (*r->cursor == '`')
        return uno_read_string(r, out);

    if (*r->cursor == '-' || isdigit((unsigned char)*r->cursor))
        return uno_read_number(r, out);

    if (isalpha((unsigned char)*r->cursor) || *r->cursor == '_')
    {
        char *ident;

        if (uno_read_identifier(r, &ident) != 0)
            return -1;

        if (strcmp(ident, "true") == 0)
        {
            free(ident);
            out->type = USSR_BOOLEAN;
            out->data.boolean = 1;
            return 0;
        }
        if (strcmp(ident, "false") == 0)
        {
            free(ident);
            out->type = USSR_BOOLEAN;
            out->data.boolean = 0;
            return 0;
        }
        if (strcmp(ident, "null") == 0)
        {
            free(ident);
            out->type = USSR_NULL;
            return 0;
        }

        /* Otherwise it must be TypeName{...} */
        uno_skip_ws(r);
        if (*r->cursor != '{')
        {
            fprintf(stderr, "USSR: expected '{' after '%s' in UNO text\n", ident);
            free(ident);
            return -1;
        }
        return uno_read_object(r, ident, out); /* takes ownership of ident */
    }

    fprintf(stderr, "USSR: unexpected character '%c' in UNO text\n", *r->cursor);
    return -1;
}

int ussr_uno_decode(const char *text, ussr_value_t *out)
{
    uno_reader_t r;
    r.cursor = text;

    if (uno_read_value(&r, out) != 0)
        return -1;

    uno_skip_ws(&r);
    if (*r.cursor != '\0')
    {
        fprintf(stderr, "USSR: trailing characters after UNO value: '%s'\n", r.cursor);
        uno_release_value(out);
        return -1;
    }

    return 0;
}

/* ------------------------------------------------------------- */
/* value wrappers + cleanup                                        */
/* ------------------------------------------------------------- */

ussr_value_t ussr_vector_value(ussr_vector_t *vector)
{
    ussr_value_t v;
    v.type = USSR_VECTOR;
    v.data.vector = vector; /* caller already owns one reference */
    return v;
}

ussr_value_t ussr_struct_value(ussr_struct_instance_t *instance)
{
    ussr_value_t v;
    v.type = USSR_STRUCT;
    v.data.instance = instance; /* caller already owns one reference */
    return v;
}

void ussr_uno_cleanup(void)
{
    ussr_struct_type_t *t = g_struct_types;
    ussr_method_t *m = g_methods;

    while (t != NULL)
    {
        ussr_struct_type_t *next_t = t->next;
        ussr_struct_field_def_t *def = t->fields_head;

        while (def != NULL)
        {
            ussr_struct_field_def_t *next_def = def->next;
            free(def->name);
            free(def->type_name);
            free(def);
            def = next_def;
        }

        free(t->name);
        free(t->parent_name);
        free(t);
        t = next_t;
    }
    g_struct_types = NULL;

    while (m != NULL)
    {
        ussr_method_t *next_m = m->next;
        free(m->type_name);
        free(m->method_name);
        ussr_command_list_free(m->body);
        free(m);
        m = next_m;
    }
    g_methods = NULL;
}
