#include "ussr_oop_builtins.h"
#include "uno.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * ussr_argument_evaluate is declared `static` in ussr.c. Remove that
 * `static` (both on the forward declaration near the top of the file
 * and on the function definition itself) so this file can link
 * against it — see INTEGRATION.md.
 */
extern int ussr_argument_evaluate(
    const ussr_argument_t *argument,
    ussr_value_t *result
);

/* ------------------------------------------------------------- */
/* small local helpers                                             */
/* ------------------------------------------------------------- */

static int oop_eval(const ussr_argument_t *arg, ussr_value_t *out)
{
    return ussr_argument_evaluate(arg, out);
}

static int oop_expect_string(const ussr_value_t *v, const char **out)
{
    if (v->type != USSR_STRING)
        return 0;
    *out = v->data.string;
    return 1;
}

static int oop_expect_integer(const ussr_value_t *v, long *out)
{
    if (v->type == USSR_INTEGER)
    {
        *out = v->data.integer;
        return 1;
    }
    if (v->type == USSR_REAL)
    {
        *out = (long)v->data.real;
        return 1;
    }
    return 0;
}

static int oop_expect_vector(const ussr_value_t *v, ussr_vector_t **out)
{
    if (v->type != USSR_VECTOR)
        return 0;
    *out = v->data.vector;
    return 1;
}

static int oop_expect_struct(const ussr_value_t *v, ussr_struct_instance_t **out)
{
    if (v->type != USSR_STRUCT)
        return 0;
    *out = v->data.instance;
    return 1;
}

/* ------------------------------------------------------------- */
/* struct(TypeName): "field:type" ...                              */
/* Uses the command's return-variable slot as the type-name slot,   */
/* exactly like a constructor call uses it to bind the instance —   */
/* so struct(Point): ... also (harmlessly) sets a variable "Point"  */
/* to null, since that slot is normally a data variable. If you'd   */
/* rather it not touch the variable table at all, have the caller   */
/* use a throwaway name, e.g. struct(_Point): ...                   */
/* ------------------------------------------------------------- */

static int oop_do_struct(
    const char *return_name,
    ussr_argument_t *arguments,
    size_t argument_count,
    ussr_value_t *out_result
)
{
    char **field_specs;
    ussr_value_t *values;
    size_t field_count = 0;
    size_t i;
    int status;

    values = malloc((argument_count > 0 ? argument_count : 1) * sizeof(*values));
    field_specs = malloc((argument_count > 0 ? argument_count : 1) * sizeof(*field_specs));
    if (values == NULL || field_specs == NULL)
    {
        free(values);
        free(field_specs);
        return -1;
    }

    for (i = 0; i < argument_count; ++i)
    {
        const char *text;

        if (oop_eval(&arguments[i], &values[i]) != 0)
        {
            size_t j;
            for (j = 0; j < i; ++j)
                ussr_value_free(&values[j]);
            free(values);
            free(field_specs);
            return -1;
        }

        if (!oop_expect_string(&values[i], &text))
        {
            fprintf(stderr, "USSR: struct(...) field specs must be strings (\"name:type\")\n");
            for (i = i + 1; i-- > 0;)
                ussr_value_free(&values[i]);
            free(values);
            free(field_specs);
            return -1;
        }

        field_specs[field_count++] = (char *)text;
    }

    status = ussr_struct_register(return_name, NULL, field_specs, field_count);

    for (i = 0; i < argument_count; ++i)
        ussr_value_free(&values[i]);
    free(values);
    free(field_specs);

    if (status != 0)
        return -1;

    *out_result = ussr_null();
    return 0;
}

/* ------------------------------------------------------------- */
/* new(x): "TypeName" args...   (positional constructor)           */
/* ------------------------------------------------------------- */

static int oop_do_new(
    ussr_argument_t *arguments,
    size_t argument_count,
    ussr_value_t *out_result
)
{
    ussr_value_t type_v;
    const char *type_name;
    ussr_value_t *args;
    ussr_struct_instance_t *instance;
    size_t i, n;

    if (argument_count < 1)
    {
        fprintf(stderr, "USSR: new(x): \"TypeName\" args...\n");
        return -1;
    }

    if (oop_eval(&arguments[0], &type_v) != 0)
        return -1;

    if (!oop_expect_string(&type_v, &type_name))
    {
        fprintf(stderr, "USSR: new() expects a type name string as its first argument\n");
        ussr_value_free(&type_v);
        return -1;
    }

    n = argument_count - 1;
    args = malloc((n > 0 ? n : 1) * sizeof(*args));
    if (args == NULL)
    {
        ussr_value_free(&type_v);
        return -1;
    }

    for (i = 0; i < n; ++i)
    {
        if (oop_eval(&arguments[1 + i], &args[i]) != 0)
        {
            size_t j;
            for (j = 0; j < i; ++j)
                ussr_value_free(&args[j]);
            free(args);
            ussr_value_free(&type_v);
            return -1;
        }
    }

    instance = ussr_struct_instantiate(type_name, args, n);

    for (i = 0; i < n; ++i)
        ussr_value_free(&args[i]);
    free(args);
    ussr_value_free(&type_v);

    if (instance == NULL)
        return -1;

    *out_result = ussr_struct_value(instance); /* takes the constructor's one reference */
    return 0;
}

/* ------------------------------------------------------------- */
/* getf(v): obj "field"   /   setf(_): obj "field" value            */
/* ------------------------------------------------------------- */

static int oop_do_getf(
    ussr_argument_t *arguments,
    size_t argument_count,
    ussr_value_t *out_result
)
{
    ussr_value_t obj_v, field_v;
    ussr_struct_instance_t *instance;
    const char *field_name;

    if (argument_count != 2)
    {
        fprintf(stderr, "USSR: getf(result): obj \"field\"\n");
        return -1;
    }

    if (oop_eval(&arguments[0], &obj_v) != 0)
        return -1;
    if (oop_eval(&arguments[1], &field_v) != 0)
    {
        ussr_value_free(&obj_v);
        return -1;
    }

    if (!oop_expect_struct(&obj_v, &instance) || !oop_expect_string(&field_v, &field_name))
    {
        fprintf(stderr, "USSR: getf() expects (struct instance, field name string)\n");
        ussr_value_free(&obj_v);
        ussr_value_free(&field_v);
        return -1;
    }

    if (!ussr_struct_get_field(instance, field_name, out_result))
    {
        fprintf(stderr, "USSR: %s has no field '%s'\n", instance->type->name, field_name);
        ussr_value_free(&obj_v);
        ussr_value_free(&field_v);
        return -1;
    }

    ussr_value_free(&obj_v);
    ussr_value_free(&field_v);
    return 0;
}

static int oop_do_setf(
    ussr_argument_t *arguments,
    size_t argument_count,
    ussr_value_t *out_result
)
{
    ussr_value_t obj_v, field_v, new_v;
    ussr_struct_instance_t *instance;
    const char *field_name;
    int status;

    if (argument_count != 3)
    {
        fprintf(stderr, "USSR: setf(_): obj \"field\" value\n");
        return -1;
    }

    if (oop_eval(&arguments[0], &obj_v) != 0)
        return -1;
    if (oop_eval(&arguments[1], &field_v) != 0)
    {
        ussr_value_free(&obj_v);
        return -1;
    }
    if (oop_eval(&arguments[2], &new_v) != 0)
    {
        ussr_value_free(&obj_v);
        ussr_value_free(&field_v);
        return -1;
    }

    if (!oop_expect_struct(&obj_v, &instance) || !oop_expect_string(&field_v, &field_name))
    {
        fprintf(stderr, "USSR: setf() expects (struct instance, field name string, value)\n");
        ussr_value_free(&obj_v);
        ussr_value_free(&field_v);
        ussr_value_free(&new_v);
        return -1;
    }

    status = ussr_struct_set_field(instance, field_name, new_v);
    if (status == 0)
        new_v.type = USSR_NULL; /* ownership moved into the struct; don't double-free below */

    ussr_value_free(&obj_v);
    ussr_value_free(&field_v);
    ussr_value_free(&new_v);

    if (status != 0)
        return -1;

    *out_result = ussr_null();
    return 0;
}

/* ------------------------------------------------------------- */
/* vec(v): ["ElementType"] / push(_): v item / at(x): v i / len(n): v */
/* ------------------------------------------------------------- */

static int oop_do_vec(
    ussr_argument_t *arguments,
    size_t argument_count,
    ussr_value_t *out_result
)
{
    ussr_value_t type_v;
    const char *element_type = NULL;
    ussr_vector_t *vector;

    if (argument_count > 1)
    {
        fprintf(stderr, "USSR: vec(v): [\"ElementType\"]\n");
        return -1;
    }

    if (argument_count == 1)
    {
        if (oop_eval(&arguments[0], &type_v) != 0)
            return -1;

        if (!oop_expect_string(&type_v, &element_type))
        {
            fprintf(stderr, "USSR: vec() element type must be a string\n");
            ussr_value_free(&type_v);
            return -1;
        }
    }

    vector = ussr_vector_create(element_type);

    if (argument_count == 1)
        ussr_value_free(&type_v);

    if (vector == NULL)
        return -1;

    *out_result = ussr_vector_value(vector);
    return 0;
}

static int oop_do_push(
    ussr_argument_t *arguments,
    size_t argument_count,
    ussr_value_t *out_result
)
{
    ussr_value_t vec_v, item_v;
    ussr_vector_t *vector;
    int status;

    if (argument_count != 2)
    {
        fprintf(stderr, "USSR: push(_): v item\n");
        return -1;
    }

    if (oop_eval(&arguments[0], &vec_v) != 0)
        return -1;
    if (oop_eval(&arguments[1], &item_v) != 0)
    {
        ussr_value_free(&vec_v);
        return -1;
    }

    if (!oop_expect_vector(&vec_v, &vector))
    {
        fprintf(stderr, "USSR: push() expects a vector as its first argument\n");
        ussr_value_free(&vec_v);
        ussr_value_free(&item_v);
        return -1;
    }

    status = ussr_vector_push(vector, item_v);

    ussr_value_free(&item_v);
    ussr_value_free(&vec_v);

    if (status != 0)
        return -1;

    *out_result = ussr_null();
    return 0;
}

static int oop_do_at(
    ussr_argument_t *arguments,
    size_t argument_count,
    ussr_value_t *out_result
)
{
    ussr_value_t vec_v, index_v;
    ussr_vector_t *vector;
    long index;

    if (argument_count != 2)
    {
        fprintf(stderr, "USSR: at(item): v index\n");
        return -1;
    }

    if (oop_eval(&arguments[0], &vec_v) != 0)
        return -1;
    if (oop_eval(&arguments[1], &index_v) != 0)
    {
        ussr_value_free(&vec_v);
        return -1;
    }

    if (!oop_expect_vector(&vec_v, &vector) || !oop_expect_integer(&index_v, &index) || index < 0)
    {
        fprintf(stderr, "USSR: at() expects (vector, non-negative integer index)\n");
        ussr_value_free(&vec_v);
        ussr_value_free(&index_v);
        return -1;
    }

    if (!ussr_vector_get(vector, (size_t)index, out_result))
    {
        fprintf(stderr, "USSR: vector index %ld out of range\n", index);
        ussr_value_free(&vec_v);
        ussr_value_free(&index_v);
        return -1;
    }

    ussr_value_free(&vec_v);
    ussr_value_free(&index_v);
    return 0;
}

static int oop_do_len(
    ussr_argument_t *arguments,
    size_t argument_count,
    ussr_value_t *out_result
)
{
    ussr_value_t vec_v;
    ussr_vector_t *vector;

    if (argument_count != 1)
    {
        fprintf(stderr, "USSR: len(n): v\n");
        return -1;
    }

    if (oop_eval(&arguments[0], &vec_v) != 0)
        return -1;

    if (!oop_expect_vector(&vec_v, &vector))
    {
        fprintf(stderr, "USSR: len() expects a vector\n");
        ussr_value_free(&vec_v);
        return -1;
    }

    *out_result = ussr_integer((long)ussr_vector_length(vector));
    ussr_value_free(&vec_v);
    return 0;
}

/* ------------------------------------------------------------- */
/* encode(s): obj   /   decode(obj): s                              */
/* ------------------------------------------------------------- */

static int oop_do_encode(
    ussr_argument_t *arguments,
    size_t argument_count,
    ussr_value_t *out_result
)
{
    ussr_value_t obj_v;
    char *text;

    if (argument_count != 1)
    {
        fprintf(stderr, "USSR: encode(s): obj\n");
        return -1;
    }

    if (oop_eval(&arguments[0], &obj_v) != 0)
        return -1;

    text = ussr_uno_encode(&obj_v);
    ussr_value_free(&obj_v);

    if (text == NULL)
        return -1;

    *out_result = ussr_string(text);
    free(text);
    return 0;
}

static int oop_do_decode(
    ussr_argument_t *arguments,
    size_t argument_count,
    ussr_value_t *out_result
)
{
    ussr_value_t text_v;
    const char *text;
    int status;

    if (argument_count != 1)
    {
        fprintf(stderr, "USSR: decode(obj): s\n");
        return -1;
    }

    if (oop_eval(&arguments[0], &text_v) != 0)
        return -1;

    if (!oop_expect_string(&text_v, &text))
    {
        fprintf(stderr, "USSR: decode() expects a string\n");
        ussr_value_free(&text_v);
        return -1;
    }

    status = ussr_uno_decode(text, out_result);
    ussr_value_free(&text_v);

    return status;
}

/* ------------------------------------------------------------- */
/* dispatch                                                         */
/* ------------------------------------------------------------- */

int ussr_oop_dispatch(
    const char *command,
    const char *return_name,
    ussr_argument_t *arguments,
    size_t argument_count,
    ussr_value_t *out_result
)
{
    int status;

    if (strcmp(command, "struct") == 0)
        status = oop_do_struct(return_name, arguments, argument_count, out_result);
    else if (strcmp(command, "new") == 0)
        status = oop_do_new(arguments, argument_count, out_result);
    else if (strcmp(command, "getf") == 0)
        status = oop_do_getf(arguments, argument_count, out_result);
    else if (strcmp(command, "setf") == 0)
        status = oop_do_setf(arguments, argument_count, out_result);
    else if (strcmp(command, "vec") == 0)
        status = oop_do_vec(arguments, argument_count, out_result);
    else if (strcmp(command, "push") == 0)
        status = oop_do_push(arguments, argument_count, out_result);
    else if (strcmp(command, "at") == 0)
        status = oop_do_at(arguments, argument_count, out_result);
    else if (strcmp(command, "len") == 0)
        status = oop_do_len(arguments, argument_count, out_result);
    else if (strcmp(command, "encode") == 0)
        status = oop_do_encode(arguments, argument_count, out_result);
    else if (strcmp(command, "decode") == 0)
        status = oop_do_decode(arguments, argument_count, out_result);
    else
        return 0; /* not ours */

    return status == 0 ? 1 : -1;
}
