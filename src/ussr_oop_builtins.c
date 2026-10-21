/* POSIX and BSD interfaces (setenv, lstat, mkdir, ...) under -std=c11. */
#if !defined(_WIN32) && !defined(_DEFAULT_SOURCE)
#define _DEFAULT_SOURCE
#endif

#include "ussr_oop_builtins.h"
#include "uno.h"

#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <conio.h>
#include <fcntl.h>
#include <io.h>
#include <windows.h>
#ifndef ENABLE_VIRTUAL_TERMINAL_PROCESSING
#define ENABLE_VIRTUAL_TERMINAL_PROCESSING 0x0004
#endif
#else
#include <dirent.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <termios.h>
#include <unistd.h>
#endif

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

static int oop_expect_integer(const ussr_value_t *v, ussr_int_t *out)
{
    if (v->type == USSR_INTEGER)
    {
        *out = v->data.integer;
        return 1;
    }
    /* A real is truncated; one outside the integer range is refused
     * (converting it would be undefined behavior). */
    if (v->type == USSR_REAL &&
        v->data.real > -9223372036854775808.0 &&
        v->data.real < 9223372036854775808.0)
    {
        *out = (ussr_int_t)v->data.real;
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

/* pop(x): v -- removes v's last item and returns it. */
static int oop_do_pop(
    ussr_argument_t *arguments,
    size_t argument_count,
    ussr_value_t *out_result
)
{
    ussr_value_t vec_v;
    ussr_vector_t *vector;

    if (argument_count != 1)
    {
        fprintf(stderr, "USSR: pop(item): v\n");
        return -1;
    }

    if (oop_eval(&arguments[0], &vec_v) != 0)
        return -1;

    if (!oop_expect_vector(&vec_v, &vector))
    {
        fprintf(stderr, "USSR: pop() expects a vector\n");
        ussr_value_free(&vec_v);
        return -1;
    }

    if (!ussr_vector_pop(vector, out_result))
    {
        fprintf(stderr, "USSR: pop() from an empty vector\n");
        ussr_value_free(&vec_v);
        return -1;
    }

    ussr_value_free(&vec_v);
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
    ussr_int_t index;

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

    if (vec_v.type == USSR_STRING && vec_v.data.string != NULL)
    {
        const char *text = vec_v.data.string;
        size_t text_length = strlen(text);
        char *piece;

        if (!oop_expect_integer(&index_v, &index) || index < 0 ||
            (size_t)index >= text_length)
        {
            fprintf(stderr, "USSR: string index " USSR_INT_FMT " out of range\n", index);
            ussr_value_free(&vec_v);
            ussr_value_free(&index_v);
            return -1;
        }

        piece = (char *)malloc(2);
        if (piece == NULL)
        {
            ussr_value_free(&vec_v);
            ussr_value_free(&index_v);
            return -1;
        }
        piece[0] = text[index];
        piece[1] = '\0';
        *out_result = ussr_string(piece);
        free(piece);
        ussr_value_free(&vec_v);
        ussr_value_free(&index_v);
        return 0;
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
        fprintf(stderr, "USSR: vector index " USSR_INT_FMT " out of range\n", index);
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

    if (vec_v.type == USSR_STRING && vec_v.data.string != NULL)
    {
        *out_result = ussr_integer((ussr_int_t)strlen(vec_v.data.string));
        ussr_value_free(&vec_v);
        return 0;
    }

    if (!oop_expect_vector(&vec_v, &vector))
    {
        fprintf(stderr, "USSR: len() expects a vector or a string\n");
        ussr_value_free(&vec_v);
        return -1;
    }

    *out_result = ussr_integer((ussr_int_t)ussr_vector_length(vector));
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
/* byte(v): s i   /   slice(v): s i n  (string primitives)          */
/* ------------------------------------------------------------- */

static int oop_do_byte(
    ussr_argument_t *arguments,
    size_t argument_count,
    ussr_value_t *out_result
)
{
    ussr_value_t str_v, index_v;
    const char *text;
    ussr_int_t index;

    if (argument_count != 2)
    {
        fprintf(stderr, "USSR: byte(v): s i\n");
        return -1;
    }

    if (oop_eval(&arguments[0], &str_v) != 0)
        return -1;
    if (oop_eval(&arguments[1], &index_v) != 0)
    {
        ussr_value_free(&str_v);
        return -1;
    }

    if (!oop_expect_string(&str_v, &text))
    {
        fprintf(stderr, "USSR: byte() expects (string, integer index)\n");
        ussr_value_free(&str_v);
        ussr_value_free(&index_v);
        return -1;
    }

    if (!oop_expect_integer(&index_v, &index) || index < 0 ||
        (size_t)index >= strlen(text))
    {
        fprintf(stderr, "USSR: byte() index out of range\n");
        ussr_value_free(&str_v);
        ussr_value_free(&index_v);
        return -1;
    }

    *out_result = ussr_integer((ussr_int_t)(unsigned char)text[index]);
    ussr_value_free(&str_v);
    ussr_value_free(&index_v);
    return 0;
}

static int oop_do_slice(
    ussr_argument_t *arguments,
    size_t argument_count,
    ussr_value_t *out_result
)
{
    ussr_value_t str_v, index_v, count_v;
    const char *text;
    ussr_int_t index, count, text_length;

    if (argument_count != 3)
    {
        fprintf(stderr, "USSR: slice(v): s i n\n");
        return -1;
    }

    if (oop_eval(&arguments[0], &str_v) != 0)
        return -1;
    if (oop_eval(&arguments[1], &index_v) != 0)
    {
        ussr_value_free(&str_v);
        return -1;
    }
    if (oop_eval(&arguments[2], &count_v) != 0)
    {
        ussr_value_free(&str_v);
        ussr_value_free(&index_v);
        return -1;
    }

    if (!oop_expect_string(&str_v, &text) ||
        !oop_expect_integer(&index_v, &index) ||
        !oop_expect_integer(&count_v, &count) ||
        index < 0 || count < 0)
    {
        fprintf(stderr, "USSR: slice() expects (string, non-negative integer index, non-negative integer count)\n");
        ussr_value_free(&str_v);
        ussr_value_free(&index_v);
        ussr_value_free(&count_v);
        return -1;
    }

    text_length = (ussr_int_t)strlen(text);
    if (index > text_length || index + count > text_length)
    {
        fprintf(stderr, "USSR: slice() range out of bounds\n");
        ussr_value_free(&str_v);
        ussr_value_free(&index_v);
        ussr_value_free(&count_v);
        return -1;
    }

    {
        char *piece = (char *)malloc((size_t)count + 1);
        if (piece == NULL)
        {
            ussr_value_free(&str_v);
            ussr_value_free(&index_v);
            ussr_value_free(&count_v);
            return -1;
        }
        memcpy(piece, text + index, (size_t)count);
        piece[count] = '\0';
        *out_result = ussr_string(piece);
        free(piece);
    }

    ussr_value_free(&str_v);
    ussr_value_free(&index_v);
    ussr_value_free(&count_v);
    return 0;
}


/* ------------------------------------------------------------- */
/* readfile(v): path -- host file-read service                      */
/* ------------------------------------------------------------- */

static int oop_do_readfile(
    ussr_argument_t *arguments,
    size_t argument_count,
    ussr_value_t *out_result
)
{
    ussr_value_t path_v;
    const char *path;
    FILE *fp;
    long size;
    size_t got;
    char *buffer;

    if (argument_count != 1)
    {
        fprintf(stderr, "USSR: readfile(v): path\n");
        return -1;
    }

    if (oop_eval(&arguments[0], &path_v) != 0)
        return -1;

    if (!oop_expect_string(&path_v, &path))
    {
        fprintf(stderr, "USSR: readfile() expects a string path\n");
        ussr_value_free(&path_v);
        return -1;
    }

    fp = fopen(path, "rb");
    if (fp == NULL)
    {
        fprintf(stderr, "USSR: readfile cannot open '%s'\n", path);
        ussr_value_free(&path_v);
        return -1;
    }

    if (fseek(fp, 0, SEEK_END) != 0 || (size = ftell(fp)) < 0)
    {
        /* pipe or non-seekable: read incrementally */
        size_t cap = 4096;
        size_t got = 0;
        clearerr(fp);
        buffer = (char *)malloc(cap);
        if (buffer == NULL)
        {
            fclose(fp);
            ussr_value_free(&path_v);
            return -1;
        }
        for (;;)
        {
            size_t n;
            if (got + 2048 > cap)
            {
                char *nb;
                cap *= 2;
                nb = (char *)realloc(buffer, cap);
                if (nb == NULL)
                {
                    free(buffer);
                    fclose(fp);
                    ussr_value_free(&path_v);
                    return -1;
                }
                buffer = nb;
            }
            n = fread(buffer + got, 1, 2048, fp);
            got += n;
            if (n < 2048)
                break;
        }
        fclose(fp);
        buffer[got] = '\0';
        *out_result = ussr_string(buffer);
        free(buffer);
        ussr_value_free(&path_v);
        return 0;
    }

    rewind(fp);

    buffer = (char *)malloc((size_t)size + 1);
    if (buffer == NULL)
    {
        fclose(fp);
        ussr_value_free(&path_v);
        return -1;
    }

    got = fread(buffer, 1, (size_t)size, fp);
    fclose(fp);
    buffer[got] = '\0';

    *out_result = ussr_string(buffer);
    free(buffer);
    ussr_value_free(&path_v);
    return 0;
}

/* ------------------------------------------------------------- */
/* Portable host primitives for interactive programs (Mbl.su).   */
/* Same command set on POSIX and Windows:                        */
/*   writefile(v): path data    -> 0 ok / 1 error (non-fatal)    */
/*   file_exists(v): path       -> 1 yes / 0 no                  */
/*   terminal_escape(v):        -> the ESC character             */
/*   terminal_raw(v):           raw input mode, 0 ok / 1 non-tty */
/*   terminal_sane(v):          restore input mode               */
/*   terminal_getch(v):         one blocking byte, -1 at EOF.    */
/*                              Arrow keys always arrive as the  */
/*                              3-byte sequence ESC [ A/B/C/D    */
/*                              (synthesized on Windows).        */
/*   terminal_size(v):          "rows cols" or "" if unknown     */
/* ------------------------------------------------------------- */

static int oop_do_writefile(
    ussr_argument_t *arguments,
    size_t argument_count,
    ussr_value_t *out_result
)
{
    ussr_value_t path_v, data_v;
    const char *path;
    const char *data;
    FILE *fp;
    size_t length;

    if (argument_count != 2)
    {
        fprintf(stderr, "USSR: writefile(v): path data\n");
        return -1;
    }

    if (oop_eval(&arguments[0], &path_v) != 0)
        return -1;
    if (oop_eval(&arguments[1], &data_v) != 0)
    {
        ussr_value_free(&path_v);
        return -1;
    }

    if (!oop_expect_string(&path_v, &path) ||
        !oop_expect_string(&data_v, &data))
    {
        fprintf(stderr, "USSR: writefile() expects (string path, string data)\n");
        ussr_value_free(&path_v);
        ussr_value_free(&data_v);
        return -1;
    }

    fp = fopen(path, "wb");
    if (fp == NULL)
    {
        fprintf(stderr, "USSR: writefile cannot open '%s'\n", path);
        *out_result = ussr_integer(1);
        ussr_value_free(&path_v);
        ussr_value_free(&data_v);
        return 0; /* non-fatal: status value reports the failure */
    }

    length = strlen(data);
    if (length > 0 && fwrite(data, 1, length, fp) != length)
    {
        fprintf(stderr, "USSR: writefile failed writing '%s'\n", path);
        fclose(fp);
        *out_result = ussr_integer(1);
        ussr_value_free(&path_v);
        ussr_value_free(&data_v);
        return 0;
    }

    fclose(fp);
    *out_result = ussr_integer(0);
    ussr_value_free(&path_v);
    ussr_value_free(&data_v);
    return 0;
}

static int oop_do_file_exists(
    ussr_argument_t *arguments,
    size_t argument_count,
    ussr_value_t *out_result
)
{
    ussr_value_t path_v;
    const char *path;
    FILE *fp;

    if (argument_count != 1)
    {
        fprintf(stderr, "USSR: file_exists(v): path\n");
        return -1;
    }

    if (oop_eval(&arguments[0], &path_v) != 0)
        return -1;

    if (!oop_expect_string(&path_v, &path))
    {
        fprintf(stderr, "USSR: file_exists() expects a string path\n");
        ussr_value_free(&path_v);
        return -1;
    }

    fp = fopen(path, "rb");
    *out_result = ussr_integer(fp != NULL ? 1 : 0);
    if (fp != NULL)
        fclose(fp);

    ussr_value_free(&path_v);
    return 0;
}

static int oop_do_terminal_escape(
    ussr_argument_t *arguments,
    size_t argument_count,
    ussr_value_t *out_result
)
{
    (void)arguments;
    (void)argument_count;
    *out_result = ussr_string("\x1b");
    return 0;
}

#ifdef _WIN32

static DWORD term_saved_imode;
static int term_have_imode = 0;
static int term_saved_in_bin = -1;
static int term_saved_out_bin = -1;
static unsigned char term_pushback[8];
static int term_pushback_count = 0;
static int term_atexit_registered = 0;

static UINT term_saved_output_cp;

static void term_restore(void)
{
    if (term_saved_output_cp != 0)
    {
        SetConsoleOutputCP(term_saved_output_cp);
        term_saved_output_cp = 0;
    }
    if (term_have_imode)
    {
        SetConsoleMode(GetStdHandle(STD_INPUT_HANDLE), term_saved_imode);
        term_have_imode = 0;
    }
    if (term_saved_in_bin != -1)
    {
        _setmode(_fileno(stdin), term_saved_in_bin);
        term_saved_in_bin = -1;
    }
    if (term_saved_out_bin != -1)
    {
        _setmode(_fileno(stdout), term_saved_out_bin);
        term_saved_out_bin = -1;
    }
}

static int term_stdin_is_console(void)
{
    DWORD mode;
    return GetConsoleMode(GetStdHandle(STD_INPUT_HANDLE), &mode) != 0;
}

static int oop_do_terminal_raw(
    ussr_argument_t *arguments,
    size_t argument_count,
    ussr_value_t *out_result
)
{
    HANDLE hin;
    DWORD mode;
    int ok = 0;

    (void)arguments;
    (void)argument_count;

    hin = GetStdHandle(STD_INPUT_HANDLE);
    if (GetConsoleMode(hin, &mode))
    {
        term_saved_imode = mode;
        term_have_imode = 1;
        SetConsoleMode(
            hin,
            mode & ~(DWORD)(ENABLE_LINE_INPUT |
                            ENABLE_ECHO_INPUT |
                            ENABLE_PROCESSED_INPUT)
        );

        /* best-effort: ANSI escape processing on the console */
        {
            HANDLE hout = GetStdHandle(STD_OUTPUT_HANDLE);
            DWORD omode;

            if (GetConsoleMode(hout, &omode))
                SetConsoleMode(hout, omode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
            /* Programs write UTF-8; show it as UTF-8 while raw. */
            if (term_saved_output_cp == 0)
            {
                term_saved_output_cp = GetConsoleOutputCP();
                SetConsoleOutputCP(CP_UTF8);
            }
        }

        ok = 1;
    }

    if (!term_atexit_registered)
    {
        atexit(term_restore);
        term_atexit_registered = 1;
    }

    /* binary stdio keeps pipes byte-exact (\r\n is not translated) */
    if (term_saved_in_bin == -1)
        term_saved_in_bin = _setmode(_fileno(stdin), _O_BINARY);
    if (term_saved_out_bin == -1)
        term_saved_out_bin = _setmode(_fileno(stdout), _O_BINARY);

    term_pushback_count = 0;
    *out_result = ussr_integer(ok ? 0 : 1);
    return 0;
}

static int oop_do_terminal_sane(
    ussr_argument_t *arguments,
    size_t argument_count,
    ussr_value_t *out_result
)
{
    (void)arguments;
    (void)argument_count;
    term_restore();
    *out_result = ussr_integer(0);
    return 0;
}

static int oop_do_terminal_getch(
    ussr_argument_t *arguments,
    size_t argument_count,
    ussr_value_t *out_result
)
{
    (void)arguments;
    (void)argument_count;

    if (term_pushback_count > 0)
    {
        int c = term_pushback[0];

        --term_pushback_count;
        memmove(
            term_pushback,
            term_pushback + 1,
            (size_t)term_pushback_count
        );
        *out_result = ussr_integer(c);
        return 0;
    }

    if (term_stdin_is_console())
    {
        for (;;)
        {
            int c = (int)_getwch();

            if (c == 0 || c == 0xE0)
            {
                /* special key: translate arrows to ESC [ letter */
                int c2 = (int)_getwch();
                char letter = '\0';

                if (c2 == 72) letter = 'A';      /* up    */
                else if (c2 == 80) letter = 'B'; /* down  */
                else if (c2 == 77) letter = 'C'; /* right */
                else if (c2 == 75) letter = 'D'; /* left  */

                if (letter != '\0')
                {
                    term_pushback[0] = (unsigned char)'[';
                    term_pushback[1] = (unsigned char)letter;
                    term_pushback_count = 2;
                    *out_result = ussr_integer(27);
                    return 0;
                }

                continue; /* ignore other special keys */
            }

            /* Console characters arrive as UTF-16; hand them out as UTF-8
             * bytes like a POSIX terminal: first byte now, rest queued. */
            if (c >= 0xD800 && c <= 0xDBFF)
            {
                int low = (int)_getwch();
                if (low >= 0xDC00 && low <= 0xDFFF)
                    c = 0x10000 + ((c - 0xD800) << 10) + (low - 0xDC00);
                else
                    c = 0xFFFD;
            }
            else if (c >= 0xDC00 && c <= 0xDFFF)
                c = 0xFFFD;
            if (c >= 0x80)
            {
                unsigned char bytes[4];
                int count;
                int i;

                if (c < 0x800)
                {
                    bytes[0] = (unsigned char)(0xC0 | (c >> 6));
                    count = 2;
                }
                else if (c < 0x10000)
                {
                    bytes[0] = (unsigned char)(0xE0 | (c >> 12));
                    bytes[1] = (unsigned char)(0x80 | ((c >> 6) & 0x3F));
                    count = 3;
                }
                else
                {
                    bytes[0] = (unsigned char)(0xF0 | (c >> 18));
                    bytes[1] = (unsigned char)(0x80 | ((c >> 12) & 0x3F));
                    bytes[2] = (unsigned char)(0x80 | ((c >> 6) & 0x3F));
                    count = 4;
                }
                if (count == 2)
                    bytes[1] = (unsigned char)(0x80 | (c & 0x3F));
                else
                    bytes[count - 1] = (unsigned char)(0x80 | (c & 0x3F));
                for (i = 1; i < count; ++i)
                    term_pushback[term_pushback_count++] = bytes[i];
                c = bytes[0];
            }

            *out_result = ussr_integer(c);
            return 0;
        }
    }

    /* redirected stdin (pipe/file): plain byte read */
    {
        unsigned char c;
        int n = _read(0, &c, 1);

        *out_result = ussr_integer(n == 1 ? (ussr_int_t)c : -1);
        return 0;
    }
}

static int oop_do_terminal_size(
    ussr_argument_t *arguments,
    size_t argument_count,
    ussr_value_t *out_result
)
{
    CONSOLE_SCREEN_BUFFER_INFO info;
    char buffer[32];

    (void)arguments;
    (void)argument_count;

    if (GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &info))
    {
        int rows = info.srWindow.Bottom - info.srWindow.Top + 1;
        int cols = info.srWindow.Right - info.srWindow.Left + 1;

        if (rows > 0 && cols > 0)
        {
            snprintf(buffer, sizeof(buffer), "%d %d", rows, cols);
            *out_result = ussr_string(buffer);
            return 0;
        }
    }

    *out_result = ussr_string("");
    return 0;
}

#else /* POSIX */

static struct termios term_saved;
static int term_have_saved = 0;
static int term_atexit_registered = 0;

static void term_restore(void)
{
    if (term_have_saved)
    {
        tcsetattr(0, TCSANOW, &term_saved);
        term_have_saved = 0;
    }
}

static int oop_do_terminal_raw(
    ussr_argument_t *arguments,
    size_t argument_count,
    ussr_value_t *out_result
)
{
    struct termios raw;

    (void)arguments;
    (void)argument_count;

    if (!isatty(0) || tcgetattr(0, &term_saved) != 0)
    {
        *out_result = ussr_integer(1); /* pipe/file: nothing to do */
        return 0;
    }

    raw = term_saved;
    raw.c_iflag &= ~(tcflag_t)(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
    raw.c_oflag &= ~(tcflag_t)(OPOST);
    raw.c_cflag |= (tcflag_t)CS8;
    raw.c_lflag &= ~(tcflag_t)(ECHO | ICANON | IEXTEN | ISIG);
    raw.c_cc[VMIN] = 1;
    raw.c_cc[VTIME] = 0;

    if (tcsetattr(0, TCSANOW, &raw) != 0)
    {
        *out_result = ussr_integer(1);
        return 0;
    }

    term_have_saved = 1;
    if (!term_atexit_registered)
    {
        atexit(term_restore);
        term_atexit_registered = 1;
    }
    *out_result = ussr_integer(0);
    return 0;
}

static int oop_do_terminal_sane(
    ussr_argument_t *arguments,
    size_t argument_count,
    ussr_value_t *out_result
)
{
    (void)arguments;
    (void)argument_count;
    term_restore();
    *out_result = ussr_integer(0);
    return 0;
}

static int oop_do_terminal_getch(
    ussr_argument_t *arguments,
    size_t argument_count,
    ussr_value_t *out_result
)
{
    unsigned char c;
    ssize_t n;

    (void)arguments;
    (void)argument_count;

    do
    {
        n = read(0, &c, 1);
    } while (n < 0 && errno == EINTR);

    *out_result = ussr_integer(n == 1 ? (ussr_int_t)c : -1);
    return 0;
}

static int oop_do_terminal_size(
    ussr_argument_t *arguments,
    size_t argument_count,
    ussr_value_t *out_result
)
{
    struct winsize ws;
    char buffer[32];

    (void)arguments;
    (void)argument_count;

    memset(&ws, 0, sizeof(ws));
    if ((ioctl(1, TIOCGWINSZ, &ws) != 0 || ws.ws_row == 0) &&
        ioctl(0, TIOCGWINSZ, &ws) != 0)
    {
        *out_result = ussr_string("");
        return 0;
    }

    if (ws.ws_row == 0 || ws.ws_col == 0)
    {
        *out_result = ussr_string("");
        return 0;
    }

    snprintf(
        buffer,
        sizeof(buffer),
        "%u %u",
        (unsigned)ws.ws_row,
        (unsigned)ws.ws_col
    );
    *out_result = ussr_string(buffer);
    return 0;
}

#endif /* _WIN32 */

/* ------------------------------------------------------------- */
/* io.su primitives: stream file handles                           */
/*                                                                 */
/* open_file(h): "path" "mode"  -> integer handle >= 0, -1 on error */
/* close_file(ok): h            -> 0 on success                     */
/* write_file(n): h "text"      -> bytes written                    */
/* read_file(s): h [n]          -> string (rest of file, or n bytes)*/
/*                                                                 */
/* Handles index a small process-global table of FILE*. The whole-  */
/* file readfile/writefile builtins above stay untouched.           */
/* ------------------------------------------------------------- */

#define USSR_MAX_FILE_HANDLES 64

static FILE *ussr_file_table[USSR_MAX_FILE_HANDLES];

static int oop_file_handle_valid(ussr_int_t handle)
{
    return handle >= 0 &&
           handle < USSR_MAX_FILE_HANDLES &&
           ussr_file_table[handle] != NULL;
}

static int oop_do_open_file(
    ussr_argument_t *arguments,
    size_t argument_count,
    ussr_value_t *out_result
)
{
    ussr_value_t path_v;
    ussr_value_t mode_v;
    const char *path;
    const char *mode;
    FILE *fp;
    ussr_int_t handle;

    if (argument_count != 2)
    {
        fprintf(stderr, "USSR: open_file(h): \"path\" \"mode\"\n");
        return -1;
    }

    if (oop_eval(&arguments[0], &path_v) != 0 ||
        oop_eval(&arguments[1], &mode_v) != 0)
        return -1;

    if (!oop_expect_string(&path_v, &path) ||
        !oop_expect_string(&mode_v, &mode))
    {
        fprintf(stderr,
                "USSR: open_file expects string path and mode\n");
        ussr_value_free(&path_v);
        ussr_value_free(&mode_v);
        return -1;
    }

    /* Whitelist C fopen modes so garbage stays garbage. */
    {
        static const char *modes[] = {
            "r", "w", "a", "r+", "w+", "a+",
            "rb", "wb", "ab", "r+b", "w+b", "a+b",
            "rb+", "wb+", "ab+"
        };
        size_t i;
        int ok = 0;

        for (i = 0; i < sizeof(modes) / sizeof(modes[0]); ++i)
            if (strcmp(mode, modes[i]) == 0)
                ok = 1;

        if (!ok)
        {
            fprintf(stderr,
                    "USSR: open_file: unsupported mode '%s'\n", mode);
            ussr_value_free(&path_v);
            ussr_value_free(&mode_v);
            return -1;
        }
    }

    for (handle = 0; handle < USSR_MAX_FILE_HANDLES; ++handle)
        if (ussr_file_table[handle] == NULL)
            break;

    if (handle == USSR_MAX_FILE_HANDLES)
    {
        fprintf(stderr, "USSR: open_file: too many open files\n");
        ussr_value_free(&path_v);
        ussr_value_free(&mode_v);
        return -1;
    }

    fp = fopen(path, mode);
    if (fp == NULL)
    {
        *out_result = ussr_integer(-1);
        ussr_value_free(&path_v);
        ussr_value_free(&mode_v);
        return 0;
    }

    ussr_file_table[handle] = fp;

    ussr_value_free(&path_v);
    ussr_value_free(&mode_v);

    *out_result = ussr_integer(handle);
    return 0;
}

static int oop_do_close_file(
    ussr_argument_t *arguments,
    size_t argument_count,
    ussr_value_t *out_result
)
{
    ussr_value_t handle_v;
    ussr_int_t handle;

    if (argument_count != 1)
    {
        fprintf(stderr, "USSR: close_file(ok): h\n");
        return -1;
    }

    if (oop_eval(&arguments[0], &handle_v) != 0)
        return -1;

    if (!oop_expect_integer(&handle_v, &handle) ||
        !oop_file_handle_valid(handle))
    {
        fprintf(stderr, "USSR: close_file: invalid handle\n");
        ussr_value_free(&handle_v);
        return -1;
    }

    fclose(ussr_file_table[handle]);
    ussr_file_table[handle] = NULL;

    ussr_value_free(&handle_v);
    *out_result = ussr_integer(0);
    return 0;
}

static int oop_do_write_file(
    ussr_argument_t *arguments,
    size_t argument_count,
    ussr_value_t *out_result
)
{
    ussr_value_t handle_v;
    ussr_value_t text_v;
    ussr_int_t handle;
    const char *text;
    size_t written;

    if (argument_count != 2)
    {
        fprintf(stderr, "USSR: write_file(n): h \"text\"\n");
        return -1;
    }

    if (oop_eval(&arguments[0], &handle_v) != 0 ||
        oop_eval(&arguments[1], &text_v) != 0)
        return -1;

    if (!oop_expect_integer(&handle_v, &handle) ||
        !oop_file_handle_valid(handle))
    {
        fprintf(stderr, "USSR: write_file: invalid handle\n");
        ussr_value_free(&handle_v);
        ussr_value_free(&text_v);
        return -1;
    }

    if (!oop_expect_string(&text_v, &text))
    {
        fprintf(stderr, "USSR: write_file expects a string\n");
        ussr_value_free(&handle_v);
        ussr_value_free(&text_v);
        return -1;
    }

    written = fwrite(text, 1, strlen(text), ussr_file_table[handle]);
    fflush(ussr_file_table[handle]);

    ussr_value_free(&handle_v);
    ussr_value_free(&text_v);

    *out_result = ussr_integer((ussr_int_t)written);
    return 0;
}

static int oop_do_read_file(
    ussr_argument_t *arguments,
    size_t argument_count,
    ussr_value_t *out_result
)
{
    ussr_value_t handle_v;
    ussr_int_t handle;
    ussr_int_t requested = -1;
    FILE *fp;
    char *buffer;
    size_t length;
    size_t capacity;

    if (argument_count < 1 || argument_count > 2)
    {
        fprintf(stderr, "USSR: read_file(s): h [n]\n");
        return -1;
    }

    if (oop_eval(&arguments[0], &handle_v) != 0)
        return -1;

    if (!oop_expect_integer(&handle_v, &handle) ||
        !oop_file_handle_valid(handle))
    {
        fprintf(stderr, "USSR: read_file: invalid handle\n");
        ussr_value_free(&handle_v);
        return -1;
    }
    ussr_value_free(&handle_v);

    if (argument_count == 2)
    {
        ussr_value_t count_v;

        if (oop_eval(&arguments[1], &count_v) != 0)
            return -1;

        if (!oop_expect_integer(&count_v, &requested) || requested < 0)
        {
            fprintf(stderr, "USSR: read_file: bad byte count\n");
            ussr_value_free(&count_v);
            return -1;
        }
        ussr_value_free(&count_v);
    }

    fp = ussr_file_table[handle];

    capacity = requested >= 0 ? (size_t)requested + 1 : 4096;
    buffer = malloc(capacity);
    if (buffer == NULL)
        return -1;

    length = 0;
    for (;;)
    {
        size_t want;
        size_t got;

        if (requested >= 0)
        {
            if (length >= (size_t)requested)
                break;
            want = (size_t)requested - length;
        }
        else
        {
            if (length + 2048 + 1 > capacity)
            {
                char *grown;
                capacity *= 2;
                grown = realloc(buffer, capacity);
                if (grown == NULL)
                {
                    free(buffer);
                    return -1;
                }
                buffer = grown;
            }
            want = 2048;
        }

        got = fread(buffer + length, 1, want, fp);
        length += got;

        if (got < want)
            break; /* EOF or error; both end the read */
    }

    buffer[length] = '\0';
    *out_result = ussr_string(buffer);
    free(buffer);
    return 0;
}

/* read_line(s): h -> next line without its LF/CRLF, null at EOF. */
static int oop_do_read_line(
    ussr_argument_t *arguments,
    size_t argument_count,
    ussr_value_t *out_result
)
{
    ussr_value_t handle_v;
    ussr_int_t handle;
    FILE *fp;
    char *buffer;
    size_t length = 0;
    size_t capacity = 256;
    int character;

    if (argument_count != 1)
    {
        fprintf(stderr, "USSR: read_line(s): h\n");
        return -1;
    }
    if (oop_eval(&arguments[0], &handle_v) != 0)
        return -1;
    if (!oop_expect_integer(&handle_v, &handle) ||
        !oop_file_handle_valid(handle))
    {
        fprintf(stderr, "USSR: read_line: invalid handle\n");
        ussr_value_free(&handle_v);
        return -1;
    }
    ussr_value_free(&handle_v);

    fp = ussr_file_table[handle];
    character = fgetc(fp);
    if (character == EOF)
    {
        *out_result = ussr_null();
        return 0;
    }
    buffer = malloc(capacity);
    if (buffer == NULL)
        return -1;
    while (character != EOF && character != '\n')
    {
        if (length + 1 >= capacity)
        {
            char *grown = realloc(buffer, capacity * 2);
            if (grown == NULL)
            {
                free(buffer);
                return -1;
            }
            buffer = grown;
            capacity *= 2;
        }
        if (character != '\0')
            buffer[length++] = (char)character;
        character = fgetc(fp);
    }
    if (length > 0 && buffer[length - 1] == '\r')
        --length;
    buffer[length] = '\0';
    *out_result = ussr_string(buffer);
    free(buffer);
    return out_result->data.string != NULL ? 0 : -1;
}

/* ------------------------------------------------------------- */
/* math.su primitives                                              */
/*                                                                 */
/* sin(r): x -> sine of x (radians)                                 */
/* ln(r): x  -> natural logarithm, x > 0                            */
/* M_PI / M_E arrive as preprocessor macros from math.su itself.    */
/* ------------------------------------------------------------- */

static int oop_do_math_unary(
    const char *name,
    double (*function)(double),
    int positive_only,
    ussr_argument_t *arguments,
    size_t argument_count,
    ussr_value_t *out_result
)
{
    ussr_value_t x_v;
    double x;

    if (argument_count != 1)
    {
        fprintf(stderr, "USSR: %s(r): x\n", name);
        return -1;
    }

    if (oop_eval(&arguments[0], &x_v) != 0)
        return -1;

    if (x_v.type != USSR_INTEGER && x_v.type != USSR_REAL)
    {
        fprintf(stderr, "USSR: %s expects a number\n", name);
        ussr_value_free(&x_v);
        return -1;
    }

    x = x_v.type == USSR_REAL
        ? x_v.data.real
        : (double)x_v.data.integer;

    ussr_value_free(&x_v);

    if (positive_only && x <= 0.0)
    {
        fprintf(stderr, "USSR: %s: input must be positive\n", name);
        return -1;
    }

    *out_result = ussr_real(function(x));
    return 0;
}

/* ------------------------------------------------------------- */
/* bytes.su primitives (memset analogs)                            */
/*                                                                 */
/* set_bytes(r): value vec -> every item of vec set to value        */
/*                            (0..255), like memset(vec, value,     */
/*                            sizeof vec); returns vec              */
/* clear_bytes(r): vec     -> set_bytes with value 0                */
/* ------------------------------------------------------------- */

static int oop_do_set_bytes(
    ussr_argument_t *arguments,
    size_t argument_count,
    int clear,
    ussr_value_t *out_result
)
{
    ussr_value_t vec_v;
    ussr_value_t value_v;
    ussr_vector_t *vector;
    ussr_int_t fill = 0;
    size_t count;
    size_t i;

    if (argument_count != (clear ? 1u : 2u))
    {
        fprintf(stderr,
                clear
                    ? "USSR: clear_bytes(r): vec\n"
                    : "USSR: set_bytes(r): value vec\n");
        return -1;
    }

    if (!clear && oop_eval(&arguments[0], &value_v) != 0)
        return -1;

    if (oop_eval(&arguments[clear ? 0 : 1], &vec_v) != 0)
    {
        if (!clear)
            ussr_value_free(&value_v);
        return -1;
    }

    if (!clear && !oop_expect_integer(&value_v, &fill))
    {
        fprintf(stderr, "USSR: set_bytes value must be an integer\n");
        ussr_value_free(&value_v);
        ussr_value_free(&vec_v);
        return -1;
    }

    if (!oop_expect_vector(&vec_v, &vector))
    {
        fprintf(stderr, "USSR: set_bytes expects a vector\n");
        if (!clear)
            ussr_value_free(&value_v);
        ussr_value_free(&vec_v);
        return -1;
    }

    count = ussr_vector_length(vector);

    for (i = 0; i < count; ++i)
    {
        ussr_value_t byte_value = ussr_integer(fill);
        if (!ussr_vector_set(vector, i, byte_value))
        {
            fprintf(stderr, "USSR: set_bytes: incompatible element\n");
            if (!clear)
                ussr_value_free(&value_v);
            ussr_value_free(&vec_v);
            return -1;
        }
    }

    if (!clear)
        ussr_value_free(&value_v);

    *out_result = vec_v; /* ownership moves to the result */
    return 0;
}

/* ------------------------------------------------------------- */
/* Directory and path primitives (io.su):                          */
/*   list_dir(v): path        -> vector of names, sorted without   */
/*                               case; directories end in "/";     */
/*                               "." and ".." are omitted. null    */
/*                               (non-fatal) when unreadable.      */
/*   is_dir(v): path          -> 1 directory / 0 otherwise         */
/*   getcwd(v):               -> current working directory         */
/*   rename_path(v): from to  -> 0 ok / 1 error; never replaces    */
/*                               an existing destination           */
/*   remove_file(v): path     -> 0 ok / 1 error; files only        */
/*   remove_dir(v): path      -> 0 ok / 1 error; empty dirs only   */
/*   make_dir(v): path        -> 0 ok / 1 error or already exists  */
/* ------------------------------------------------------------- */

static int oop_path_argument(
    const char *name,
    ussr_argument_t *arguments,
    size_t index,
    ussr_value_t *value,
    const char **path
)
{
    if (oop_eval(&arguments[index], value) != 0)
        return 0;
    if (!oop_expect_string(value, path))
    {
        fprintf(stderr, "USSR: %s() expects a string path\n", name);
        ussr_value_free(value);
        return 0;
    }
    return 1;
}

static int oop_path_is_dir(const char *path)
{
#ifdef _WIN32
    DWORD attributes = GetFileAttributesA(path);
    return attributes != INVALID_FILE_ATTRIBUTES &&
           (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
#else
    struct stat info;
    return stat(path, &info) == 0 && S_ISDIR(info.st_mode);
#endif
}

static int oop_path_exists(const char *path)
{
#ifdef _WIN32
    return GetFileAttributesA(path) != INVALID_FILE_ATTRIBUTES;
#else
    struct stat info;
    return lstat(path, &info) == 0;
#endif
}

static int oop_name_compare(const void *left, const void *right)
{
    const unsigned char *a = *(const unsigned char *const *)left;
    const unsigned char *b = *(const unsigned char *const *)right;

    for (;; ++a, ++b)
    {
        int ca = (*a >= 'A' && *a <= 'Z') ? *a + 32 : *a;
        int cb = (*b >= 'A' && *b <= 'Z') ? *b + 32 : *b;
        if (ca != cb || ca == 0)
            return ca != cb ? ca - cb : strcmp(*(const char *const *)left,
                                               *(const char *const *)right);
    }
}

static int oop_names_add(char ***names, size_t *count, size_t *capacity,
                         const char *name, int directory)
{
    size_t length = strlen(name);
    char *copy;

    if (*count == *capacity)
    {
        size_t grown_capacity = *capacity == 0 ? 64 : *capacity * 2;
        char **grown = realloc(*names, grown_capacity * sizeof(*grown));
        if (grown == NULL)
            return 0;
        *names = grown;
        *capacity = grown_capacity;
    }
    copy = malloc(length + 2);
    if (copy == NULL)
        return 0;
    memcpy(copy, name, length);
    if (directory)
        copy[length++] = '/';
    copy[length] = '\0';
    (*names)[(*count)++] = copy;
    return 1;
}

static int oop_list_names(const char *path, char ***names, size_t *count)
{
    size_t capacity = 0;
#ifdef _WIN32
    char pattern[MAX_PATH + 4];
    WIN32_FIND_DATAA data;
    HANDLE handle;
    size_t length = strlen(path);

    if (length == 0 || length + 3 >= sizeof(pattern))
        return 0;
    memcpy(pattern, path, length);
    if (pattern[length - 1] != '\\' && pattern[length - 1] != '/')
        pattern[length++] = '\\';
    memcpy(pattern + length, "*", 2);
    handle = FindFirstFileA(pattern, &data);
    if (handle == INVALID_HANDLE_VALUE)
        return 0;
    do
    {
        if (strcmp(data.cFileName, ".") == 0 ||
            strcmp(data.cFileName, "..") == 0)
            continue;
        if (!oop_names_add(names, count, &capacity, data.cFileName,
                           (data.dwFileAttributes &
                            FILE_ATTRIBUTE_DIRECTORY) != 0))
        {
            FindClose(handle);
            return 0;
        }
    } while (FindNextFileA(handle, &data));
    FindClose(handle);
#else
    DIR *directory = opendir(path);
    struct dirent *item;
    size_t length = strlen(path);

    if (directory == NULL)
        return 0;
    while ((item = readdir(directory)) != NULL)
    {
        char *full;
        int is_directory;

        if (strcmp(item->d_name, ".") == 0 || strcmp(item->d_name, "..") == 0)
            continue;
        full = malloc(length + strlen(item->d_name) + 2);
        if (full == NULL)
        {
            closedir(directory);
            return 0;
        }
        sprintf(full, "%s%s%s", path,
                length > 0 && path[length - 1] == '/' ? "" : "/",
                item->d_name);
        is_directory = oop_path_is_dir(full);
        free(full);
        if (!oop_names_add(names, count, &capacity, item->d_name,
                           is_directory))
        {
            closedir(directory);
            return 0;
        }
    }
    closedir(directory);
#endif
    if (*count > 1)
        qsort(*names, *count, sizeof(**names), oop_name_compare);
    return 1;
}

static int oop_do_list_dir(
    ussr_argument_t *arguments,
    size_t argument_count,
    ussr_value_t *out_result
)
{
    ussr_value_t path_v;
    const char *path;
    char **names = NULL;
    size_t count = 0;
    size_t i;
    ussr_vector_t *vector;
    int ok;

    if (argument_count != 1)
    {
        fprintf(stderr, "USSR: list_dir(v): path\n");
        return -1;
    }
    if (!oop_path_argument("list_dir", arguments, 0, &path_v, &path))
        return -1;
    ok = oop_list_names(path, &names, &count);
    ussr_value_free(&path_v);
    if (!ok)
    {
        for (i = 0; i < count; ++i)
            free(names[i]);
        free(names);
        *out_result = ussr_null(); /* non-fatal: unreadable directory */
        return 0;
    }
    vector = ussr_vector_create(NULL);
    for (i = 0; i < count; ++i)
    {
        if (vector != NULL)
        {
            ussr_value_t item = ussr_string(names[i]);
            if (item.data.string == NULL ||
                ussr_vector_push(vector, item) != 0)
            {
                ussr_vector_release(vector);
                vector = NULL;
            }
            ussr_value_free(&item);
        }
        free(names[i]);
    }
    free(names);
    if (vector == NULL)
        return -1;
    *out_result = ussr_vector_value(vector);
    return 0;
}

static int oop_do_is_dir(
    ussr_argument_t *arguments,
    size_t argument_count,
    ussr_value_t *out_result
)
{
    ussr_value_t path_v;
    const char *path;

    if (argument_count != 1)
    {
        fprintf(stderr, "USSR: is_dir(v): path\n");
        return -1;
    }
    if (!oop_path_argument("is_dir", arguments, 0, &path_v, &path))
        return -1;
    *out_result = ussr_integer(oop_path_is_dir(path) ? 1 : 0);
    ussr_value_free(&path_v);
    return 0;
}

static int oop_do_getcwd(
    size_t argument_count,
    ussr_value_t *out_result
)
{
    char buffer[4096];

    if (argument_count != 0)
    {
        fprintf(stderr, "USSR: getcwd(v):\n");
        return -1;
    }
#ifdef _WIN32
    {
        DWORD length = GetCurrentDirectoryA((DWORD)sizeof(buffer), buffer);
        if (length == 0 || length >= sizeof(buffer))
        {
            fprintf(stderr, "USSR: getcwd cannot read the current directory\n");
            return -1;
        }
    }
#else
    if (getcwd(buffer, sizeof(buffer)) == NULL)
    {
        fprintf(stderr, "USSR: getcwd: %s\n", strerror(errno));
        return -1;
    }
#endif
    *out_result = ussr_string(buffer);
    return out_result->data.string != NULL ? 0 : -1;
}

/* rename_path / remove_file / remove_dir: 0 ok, 1 refused or failed. */
static int oop_do_path_change(
    const char *name,
    ussr_argument_t *arguments,
    size_t argument_count,
    ussr_value_t *out_result
)
{
    int renaming = strcmp(name, "rename_path") == 0;
    int making = strcmp(name, "make_dir") == 0;
    ussr_value_t first_v;
    ussr_value_t second_v;
    const char *first;
    const char *second = NULL;
    int failed;

    if (argument_count != (renaming ? 2u : 1u))
    {
        fprintf(stderr, "USSR: %s(v): %s\n", name,
                renaming ? "from to" : "path");
        return -1;
    }
    if (!oop_path_argument(name, arguments, 0, &first_v, &first))
        return -1;
    if (renaming &&
        !oop_path_argument(name, arguments, 1, &second_v, &second))
    {
        ussr_value_free(&first_v);
        return -1;
    }

    if (renaming)
        failed = !oop_path_exists(first) || oop_path_exists(second) ||
#ifdef _WIN32
                 !MoveFileA(first, second);
#else
                 rename(first, second) != 0;
#endif
    else if (making)
        failed = oop_path_exists(first) ||
#ifdef _WIN32
                 !CreateDirectoryA(first, NULL);
#else
                 mkdir(first, 0777) != 0;
#endif
    else if (strcmp(name, "remove_dir") == 0)
        failed = !oop_path_is_dir(first) ||
#ifdef _WIN32
                 !RemoveDirectoryA(first);
#else
                 rmdir(first) != 0;
#endif
    else
        failed = !oop_path_exists(first) || oop_path_is_dir(first) ||
                 remove(first) != 0;

    *out_result = ussr_integer(failed ? 1 : 0);
    ussr_value_free(&first_v);
    if (renaming)
        ussr_value_free(&second_v);
    return 0;
}

/* ------------------------------------------------------------- */
/* typeof(t): value -> "null" "integer" "real" "string" "boolean"   */
/*                     "vector", or a struct instance's type name   */
/* fail(_): message -> prints message to stderr and stops the      */
/*                     script like any other runtime error          */
/* ------------------------------------------------------------- */

static int oop_do_typeof(
    ussr_argument_t *arguments,
    size_t argument_count,
    ussr_value_t *out_result
)
{
    ussr_value_t value;
    const char *name = "null";

    if (argument_count != 1)
    {
        fprintf(stderr, "USSR: typeof(t): value\n");
        return -1;
    }
    if (oop_eval(&arguments[0], &value) != 0)
        return -1;
    switch (value.type)
    {
    case USSR_NULL: name = "null"; break;
    case USSR_INTEGER: name = "integer"; break;
    case USSR_REAL: name = "real"; break;
    case USSR_STRING: name = "string"; break;
    case USSR_BOOLEAN: name = "boolean"; break;
    case USSR_VECTOR: name = "vector"; break;
    case USSR_STRUCT:
        name = value.data.instance != NULL &&
               value.data.instance->type != NULL
                   ? value.data.instance->type->name : "struct";
        break;
    }
    *out_result = ussr_string(name);
    ussr_value_free(&value);
    return out_result->data.string != NULL ? 0 : -1;
}

/* ------------------------------------------------------------- */
/* Environment variables of the ussr process (inherited by the    */
/* external commands it starts):                                   */
/*   getenv(v): name          -> value, "" if set to empty, null   */
/*                               if not set                       */
/*   setenv(ok): name value   -> 0 ok / 1 invalid name or failure  */
/*   unsetenv(ok): name       -> 0 ok (also when it was not set) / */
/*                               1 invalid name or failure         */
/* A valid name is non-empty and has no "=". Non-string arguments  */
/* are errors. Windows uses the Win32 environment block, so empty  */
/* values are kept (the C runtime's _putenv would delete them).    */
/* ------------------------------------------------------------- */

static int oop_env_name_valid(const char *name)
{
    return name[0] != '\0' && strchr(name, '=') == NULL;
}

#ifdef _WIN32
/* UTF-8 <-> UTF-16 so non-ASCII names and values survive the round trip. */
static wchar_t *oop_env_wide(const char *text)
{
    int count = MultiByteToWideChar(CP_UTF8, 0, text, -1, NULL, 0);
    wchar_t *wide;

    if (count <= 0)
        return NULL;
    wide = malloc((size_t)count * sizeof(*wide));
    if (wide != NULL &&
        MultiByteToWideChar(CP_UTF8, 0, text, -1, wide, count) <= 0)
    {
        free(wide);
        wide = NULL;
    }
    return wide;
}

static char *oop_env_utf8(const wchar_t *wide)
{
    int count = WideCharToMultiByte(CP_UTF8, 0, wide, -1, NULL, 0, NULL, NULL);
    char *text;

    if (count <= 0)
        return NULL;
    text = malloc((size_t)count);
    if (text != NULL &&
        WideCharToMultiByte(CP_UTF8, 0, wide, -1, text, count, NULL, NULL) <= 0)
    {
        free(text);
        text = NULL;
    }
    return text;
}
#endif

static int oop_env_argument(
    const char *command,
    ussr_argument_t *arguments,
    size_t index,
    ussr_value_t *value,
    const char **text
)
{
    if (oop_eval(&arguments[index], value) != 0)
        return 0;
    if (!oop_expect_string(value, text))
    {
        fprintf(stderr, "USSR: %s() expects string arguments\n", command);
        ussr_value_free(value);
        return 0;
    }
    return 1;
}

/* Value of an environment variable as UTF-8 (malloc'd), or NULL when it is
 * not set. Shared with the REPL prompt (PS1/PS2) so both see setenv. */
char *ussr_env_get(const char *name)
{
    char *copy = NULL;

    if (name == NULL || !oop_env_name_valid(name))
        return NULL;
#ifdef _WIN32
    {
        wchar_t *wide_name = oop_env_wide(name);
        DWORD size;

        if (wide_name == NULL)
            return NULL;
        SetLastError(0);
        size = GetEnvironmentVariableW(wide_name, NULL, 0);
        if (size == 0)
        {
            if (GetLastError() != ERROR_ENVVAR_NOT_FOUND)
            {
                copy = malloc(1);
                if (copy != NULL)
                    copy[0] = '\0';
            }
        }
        else
        {
            wchar_t *buffer = malloc((size_t)size * sizeof(*buffer));
            if (buffer != NULL &&
                GetEnvironmentVariableW(wide_name, buffer, size) < size)
                copy = oop_env_utf8(buffer);
            free(buffer);
        }
        free(wide_name);
    }
#else
    {
        const char *found = getenv(name);
        if (found != NULL)
        {
            copy = malloc(strlen(found) + 1);
            if (copy != NULL)
                strcpy(copy, found);
        }
    }
#endif
    return copy;
}

static int oop_do_env(
    const char *command,
    ussr_argument_t *arguments,
    size_t argument_count,
    ussr_value_t *out_result
)
{
    int setting = strcmp(command, "setenv") == 0;
    int getting = strcmp(command, "getenv") == 0;
    ussr_value_t name_v;
    ussr_value_t value_v;
    const char *name;
    const char *value = NULL;
    int failed;

    if (argument_count != (setting ? 2u : 1u))
    {
        fprintf(stderr, "USSR: %s(v): %s\n", command,
                setting ? "name value" : "name");
        return -1;
    }
    if (!oop_env_argument(command, arguments, 0, &name_v, &name))
        return -1;
    if (setting && !oop_env_argument(command, arguments, 1, &value_v, &value))
    {
        ussr_value_free(&name_v);
        return -1;
    }

    if (getting)
    {
        char *found = ussr_env_get(name);
        *out_result = found != NULL ? ussr_string(found) : ussr_null();
        free(found);
        ussr_value_free(&name_v);
        return 0;
    }

    failed = !oop_env_name_valid(name);
    if (!failed)
    {
#ifdef _WIN32
        wchar_t *wide_name = oop_env_wide(name);
        wchar_t *wide_value = setting ? oop_env_wide(value) : NULL;

        if (wide_name == NULL || (setting && wide_value == NULL))
            failed = 1;
        else if (setting)
            failed = !SetEnvironmentVariableW(wide_name, wide_value);
        else
            failed = !SetEnvironmentVariableW(wide_name, NULL) &&
                     GetLastError() != ERROR_ENVVAR_NOT_FOUND;
        free(wide_name);
        free(wide_value);
#else
        failed = setting ? setenv(name, value, 1) != 0 : unsetenv(name) != 0;
#endif
    }
    *out_result = ussr_integer(failed ? 1 : 0);
    ussr_value_free(&name_v);
    if (setting)
        ussr_value_free(&value_v);
    return 0;
}

/* chr(s): b -> one-byte string for 1..255; UTF-8 text is built by joining
 * the bytes of each character. */
static int oop_do_chr(
    ussr_argument_t *arguments,
    size_t argument_count,
    ussr_value_t *out_result
)
{
    ussr_value_t value;
    ussr_int_t code;
    char text[2];

    if (argument_count != 1)
    {
        fprintf(stderr, "USSR: chr(s): byte\n");
        return -1;
    }
    if (oop_eval(&arguments[0], &value) != 0)
        return -1;
    if (!oop_expect_integer(&value, &code) || code < 1 || code > 255)
    {
        fprintf(stderr, "USSR: chr() expects an integer byte 1..255\n");
        ussr_value_free(&value);
        return -1;
    }
    ussr_value_free(&value);
    text[0] = (char)(unsigned char)code;
    text[1] = '\0';
    *out_result = ussr_string(text);
    return out_result->data.string != NULL ? 0 : -1;
}

static int oop_do_fail(
    ussr_argument_t *arguments,
    size_t argument_count
)
{
    ussr_value_t value;
    const char *message;

    if (argument_count != 1)
    {
        fprintf(stderr, "USSR: fail(_): message\n");
        return -1;
    }
    if (oop_eval(&arguments[0], &value) != 0)
        return -1;
    if (oop_expect_string(&value, &message))
        fprintf(stderr, "USSR: %s\n", message);
    else
        fprintf(stderr, "USSR: fail() expects a message string\n");
    ussr_value_free(&value);
    return -1;
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
    else if (strcmp(command, "pop") == 0)
        status = oop_do_pop(arguments, argument_count, out_result);
    else if (strcmp(command, "at") == 0)
        status = oop_do_at(arguments, argument_count, out_result);
    else if (strcmp(command, "len") == 0)
        status = oop_do_len(arguments, argument_count, out_result);
    else if (strcmp(command, "byte") == 0)
        status = oop_do_byte(arguments, argument_count, out_result);
    else if (strcmp(command, "slice") == 0)
        status = oop_do_slice(arguments, argument_count, out_result);
    else if (strcmp(command, "readfile") == 0)
        status = oop_do_readfile(arguments, argument_count, out_result);
    else if (strcmp(command, "writefile") == 0)
        status = oop_do_writefile(arguments, argument_count, out_result);
    else if (strcmp(command, "open_file") == 0)
        status = oop_do_open_file(arguments, argument_count, out_result);
    else if (strcmp(command, "close_file") == 0)
        status = oop_do_close_file(arguments, argument_count, out_result);
    else if (strcmp(command, "write_file") == 0)
        status = oop_do_write_file(arguments, argument_count, out_result);
    else if (strcmp(command, "read_file") == 0)
        status = oop_do_read_file(arguments, argument_count, out_result);
    else if (strcmp(command, "read_line") == 0)
        status = oop_do_read_line(arguments, argument_count, out_result);
    else if (strcmp(command, "sin") == 0)
        status = oop_do_math_unary("sin", sin, 0, arguments, argument_count, out_result);
    else if (strcmp(command, "ln") == 0)
        status = oop_do_math_unary("ln", log, 1, arguments, argument_count, out_result);
    else if (strcmp(command, "set_bytes") == 0)
        status = oop_do_set_bytes(arguments, argument_count, 0, out_result);
    else if (strcmp(command, "clear_bytes") == 0)
        status = oop_do_set_bytes(arguments, argument_count, 1, out_result);
    else if (strcmp(command, "file_exists") == 0)
        status = oop_do_file_exists(arguments, argument_count, out_result);
    else if (strcmp(command, "typeof") == 0)
        status = oop_do_typeof(arguments, argument_count, out_result);
    else if (strcmp(command, "getenv") == 0 ||
             strcmp(command, "setenv") == 0 ||
             strcmp(command, "unsetenv") == 0)
        status = oop_do_env(command, arguments, argument_count, out_result);
    else if (strcmp(command, "chr") == 0)
        status = oop_do_chr(arguments, argument_count, out_result);
    else if (strcmp(command, "fail") == 0)
        status = oop_do_fail(arguments, argument_count);
    else if (strcmp(command, "list_dir") == 0)
        status = oop_do_list_dir(arguments, argument_count, out_result);
    else if (strcmp(command, "is_dir") == 0)
        status = oop_do_is_dir(arguments, argument_count, out_result);
    else if (strcmp(command, "getcwd") == 0)
        status = oop_do_getcwd(argument_count, out_result);
    else if (strcmp(command, "rename_path") == 0 ||
             strcmp(command, "remove_file") == 0 ||
             strcmp(command, "remove_dir") == 0 ||
             strcmp(command, "make_dir") == 0)
        status = oop_do_path_change(command, arguments, argument_count,
                                    out_result);
    else if (strcmp(command, "terminal_escape") == 0)
        status = oop_do_terminal_escape(arguments, argument_count, out_result);
    else if (strcmp(command, "terminal_raw") == 0)
        status = oop_do_terminal_raw(arguments, argument_count, out_result);
    else if (strcmp(command, "terminal_sane") == 0)
        status = oop_do_terminal_sane(arguments, argument_count, out_result);
    else if (strcmp(command, "terminal_getch") == 0)
        status = oop_do_terminal_getch(arguments, argument_count, out_result);
    else if (strcmp(command, "terminal_size") == 0)
        status = oop_do_terminal_size(arguments, argument_count, out_result);
    else if (strcmp(command, "encode") == 0)
        status = oop_do_encode(arguments, argument_count, out_result);
    else if (strcmp(command, "decode") == 0)
        status = oop_do_decode(arguments, argument_count, out_result);
    else
        return 0; /* not ours */

    return status == 0 ? 1 : -1;
}
