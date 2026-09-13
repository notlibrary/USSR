#ifndef USSR_UNO_H
#define USSR_UNO_H

#include <stddef.h>
#include "ussr.h"

/*
 * ---------------------------------------------------------------
 * Struct types
 * ---------------------------------------------------------------
 *
 * A field's declared type is one of the strings:
 *   "number" | "string" | "boolean" | "null" | "vector" | "any"
 * or the name of another registered struct (checked at
 * construct/decode time, walking the target's parent chain so a
 * subtype satisfies a parent-typed field/vector).
 */

typedef struct ussr_struct_field_def_t
{
    char *name;
    char *type_name;
    struct ussr_struct_field_def_t *next;
} ussr_struct_field_def_t;

typedef struct ussr_struct_type_t
{
    char *name;
    char *parent_name;                 /* NULL if none */
    struct ussr_struct_type_t *parent; /* resolved lazily, may be NULL until parent is registered */

    ussr_struct_field_def_t *fields_head;
    ussr_struct_field_def_t *fields_tail;
    size_t field_count;                /* own fields only; ussr_struct_total_field_count() includes parents */

    struct ussr_struct_type_t *next;
} ussr_struct_type_t;

/*
 * Registers a new struct type. field_specs[i] is "name:type" as
 * described above. Returns 0 on success, -1 on error (duplicate
 * name, malformed field spec, or unknown parent_name) with a message
 * on stderr.
 */
int ussr_struct_register(
    const char *name,
    const char *parent_name, /* may be NULL */
    char **field_specs,
    size_t field_count
);

ussr_struct_type_t *ussr_struct_type_find(const char *name);

/* Total field count including inherited fields, parent-first order. */
size_t ussr_struct_total_field_count(const ussr_struct_type_t *type);

/* True if `type` is `of_name` or descends from it. */
int ussr_struct_type_is(const ussr_struct_type_t *type, const char *of_name);

/*
 * ---------------------------------------------------------------
 * Struct instances (reference-counted)
 * ---------------------------------------------------------------
 */

struct ussr_struct_instance_t
{
    ussr_struct_type_t *type;
    ussr_value_t *fields; /* parallel to ussr_struct_total_field_count(type), parent-first order */
    size_t field_count;
    size_t refcount;
};

/* Positional construction, args in declared (parent-first) order.
 * Used for `TypeName(x): args...` constructor calls. */
ussr_struct_instance_t *ussr_struct_instantiate(
    const char *type_name,
    const ussr_value_t *args,
    size_t argument_count
);

/* Named-field construction: field_names[i]/field_values[i] pairs, any
 * order, every declared field (own + inherited) must be present
 * exactly once. Used by ussr_uno_decode(), since UNO objects are
 * name=value pairs rather than positional. */
ussr_struct_instance_t *ussr_struct_instantiate_named(
    const char *type_name,
    const char * const *field_names,
    const ussr_value_t *field_values,
    size_t count
);

void ussr_struct_instance_retain(ussr_struct_instance_t *instance);
void ussr_struct_instance_release(ussr_struct_instance_t *instance);

/* Returns 1 and *out set on success, 0 if the field doesn't exist. */
int ussr_struct_get_field(
    const ussr_struct_instance_t *instance,
    const char *field_name,
    ussr_value_t *out
);

/*
 * Sets a field, type-checking the new value against the field's
 * declared type. Returns 0 on success, -1 on unknown field or type
 * mismatch. Releases/retains as needed for reference-typed fields.
 */
int ussr_struct_set_field(
    ussr_struct_instance_t *instance,
    const char *field_name,
    ussr_value_t value
);

/*
 * ---------------------------------------------------------------
 * Methods (single dispatch)
 * ---------------------------------------------------------------
 */

int ussr_method_register(
    const char *type_name,
    const char *method_name,
    ussr_command_list_t *body
);

/* Walks the type's parent chain; NULL if no method found anywhere in it. */
ussr_command_list_t *ussr_method_lookup(
    const ussr_struct_type_t *type,
    const char *method_name
);

/*
 * ---------------------------------------------------------------
 * Vectors (reference-counted, optionally element-typed)
 * ---------------------------------------------------------------
 */

struct ussr_vector_t
{
    char *element_type; /* NULL means "any"; otherwise a scalar-type name or struct type name */
    ussr_value_t *items;
    size_t count;
    size_t capacity;
    size_t refcount;
};

ussr_vector_t *ussr_vector_create(const char *element_type /* may be NULL or "" for any */);
void ussr_vector_retain(ussr_vector_t *vector);
void ussr_vector_release(ussr_vector_t *vector);

/* Returns 0 on success, -1 on element-type mismatch. */
int ussr_vector_push(ussr_vector_t *vector, ussr_value_t item);

/* Returns 1 and *out set on success, 0 if index is out of range. */
int ussr_vector_get(const ussr_vector_t *vector, size_t index, ussr_value_t *out);

size_t ussr_vector_length(const ussr_vector_t *vector);

/*
 * ---------------------------------------------------------------
 * UNO — Unified Object Notation
 * ---------------------------------------------------------------
 *
 * Grammar (see design doc section 5.2):
 *
 *   uno-value  ::= uno-object | uno-vector | uno-string
 *                | number | boolean | null
 *   uno-object ::= identifier "{" (field ("," field)*)? "}"
 *   field      ::= identifier "=" uno-value
 *   uno-vector ::= "[" (uno-value ("," uno-value)*)? "]"
 *   uno-string ::= "`" char-except-backtick-or-newline* "`"
 *
 * ussr_uno_encode() emits BARE text (no wrapping "'" or "\""): the
 * same string is valid written to a file/pipe, passed through
 * decode(), or written directly as a `'...'` source literal.
 *
 * ussr_uno_decode() parses that same bare grammar (no outer quote
 * expected). It's called from two places, both passing bare text:
 *
 *   - the decode() builtin, given a runtime USSR_STRING value
 *   - the argument evaluator, for a `'...'` literal token: the
 *     lexer captures the span between the quotes as raw text (see
 *     parser.y's UNO_LITERAL token) and defers decoding to when the
 *     argument is actually evaluated. This has to be deferred rather
 *     than resolved during parsing, because a struct's `struct(...)`
 *     registration is itself just an AST node until the interpreter
 *     runs it — the type registry isn't populated yet at parse time,
 *     including for structs defined earlier in the same file.
 */

/* Caller owns the returned string (free with free()). NULL on error. */
char *ussr_uno_encode(const ussr_value_t *value);

/*
 * Parses `text` fully (trailing whitespace only) into *out.
 * Type-checks struct fields exactly like ussr_struct_instantiate.
 * Returns 0 on success, -1 on parse error or unknown/mismatched
 * struct type, with a message on stderr.
 */
int ussr_uno_decode(const char *text, ussr_value_t *out);

/* Release all registered struct types/methods. Call at interpreter shutdown. */
void ussr_uno_cleanup(void);

#endif
