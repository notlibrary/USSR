/* POSIX and BSD interfaces (setenv, lstat, mkdir, ...) under -std=c11. */
#if !defined(_WIN32) && !defined(_DEFAULT_SOURCE)
#define _DEFAULT_SOURCE
#endif

#include "ussr_oop_builtins.h"
#include "uno.h"
#include "ai.h"
#include "alias.h"
#include "help.h"
#include "keybind.h"

#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef _WIN32
#include <conio.h>
#include <fcntl.h>
#include <io.h>
#include <windows.h>
#include <shellapi.h>
#include <winioctl.h>
#ifndef ENABLE_VIRTUAL_TERMINAL_PROCESSING
#define ENABLE_VIRTUAL_TERMINAL_PROCESSING 0x0004
#endif
#else
#include <dirent.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <termios.h>
#include <poll.h>
#include <unistd.h>
#ifdef __linux__
#include <linux/fs.h>
#endif
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

/* terminal_key_ready(v): [ms] -> 1 when terminal_getch has a byte (or
 * the end of input) to give within ms milliseconds (default 0), else 0:
 * a program can poll the keyboard between other work, such as sockets. */
static int oop_key_ready_ms(ussr_argument_t *arguments, size_t argument_count, ussr_int_t *ms)
{
    ussr_value_t v;
    *ms = 0;
    if (argument_count == 0)
        return 0;
    if (argument_count > 1 || oop_eval(&arguments[0], &v) != 0)
        return -1;
    if (!oop_expect_integer(&v, ms) || *ms < 0)
    {
        ussr_value_free(&v);
        fprintf(stderr, "USSR: terminal_key_ready(v): [milliseconds]\n");
        return -1;
    }
    ussr_value_free(&v);
    if (*ms > 60000)
        *ms = 60000;
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

                /* F-keys and Alt chords as the bytes an xterm sends */
                {
                    char sequence[16];
                    int length = ussr_keybind_getch_sequence(
                        c, c2, sequence, sizeof(sequence));

                    if (length > 1 &&
                        (size_t)(length - 1) <= sizeof(term_pushback))
                    {
                        memcpy(term_pushback, sequence + 1,
                               (size_t)(length - 1));
                        term_pushback_count = length - 1;
                        *out_result = ussr_integer(27);
                        return 0;
                    }
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

static int oop_do_terminal_key_ready(
    ussr_argument_t *arguments,
    size_t argument_count,
    ussr_value_t *out_result
)
{
    ussr_int_t ms;
    DWORD start;

    if (oop_key_ready_ms(arguments, argument_count, &ms) != 0)
        return -1;
    start = GetTickCount();
    for (;;)
    {
        int ready = term_pushback_count > 0;

        if (!ready && term_stdin_is_console())
            ready = _kbhit() != 0;
        else if (!ready)
        {
            HANDLE hin = GetStdHandle(STD_INPUT_HANDLE);
            DWORD avail = 0;

            /* a pipe: bytes waiting, or closed (the end of input);
             * a file never blocks */
            if (GetFileType(hin) == FILE_TYPE_PIPE)
                ready = !PeekNamedPipe(hin, NULL, 0, NULL, &avail, NULL) ||
                        avail > 0;
            else
                ready = 1;
        }
        if (ready)
        {
            *out_result = ussr_integer(1);
            return 0;
        }
        if ((ussr_int_t)(GetTickCount() - start) >= ms)
            break;
        Sleep(5);
    }
    *out_result = ussr_integer(0);
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

static int oop_do_terminal_key_ready(
    ussr_argument_t *arguments,
    size_t argument_count,
    ussr_value_t *out_result
)
{
    ussr_int_t ms;
    struct pollfd p;
    int r;

    if (oop_key_ready_ms(arguments, argument_count, &ms) != 0)
        return -1;
    p.fd = 0;
    p.events = POLLIN;
    p.revents = 0;
    do
    {
        r = poll(&p, 1, (int)ms);
    } while (r < 0 && errno == EINTR);
    *out_result = ussr_integer(r > 0 ? 1 : 0);
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

/* write_stderr(n): "text" -> bytes written to standard error, unchanged
 * (no prefix, no newline added). Lets a program report errors on stderr
 * the way ussr itself does; the self-hosted russia.su relies on it. */
static int oop_do_write_stderr(
    ussr_argument_t *arguments,
    size_t argument_count,
    ussr_value_t *out_result
)
{
    ussr_value_t text_v;
    const char *text;
    size_t written;

    if (argument_count != 1)
    {
        fprintf(stderr, "USSR: write_stderr(n): \"text\"\n");
        return -1;
    }

    if (oop_eval(&arguments[0], &text_v) != 0)
        return -1;

    if (!oop_expect_string(&text_v, &text))
    {
        fprintf(stderr, "USSR: write_stderr expects a string\n");
        ussr_value_free(&text_v);
        return -1;
    }

    written = fwrite(text, 1, strlen(text), stderr);
    fflush(stderr);

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

/* read_bytes(v): "path"      -> vector of the file's bytes (0..255),
 *                               null (non-fatal) when unreadable
 * write_bytes(n): "path" vec -> the file holds exactly vec's bytes;
 *                               bytes written, or -1 (non-fatal) when
 *                               it cannot be created or written
 * Unlike readfile/writefile these keep every byte, NUL included. */
static int oop_path_argument(
    const char *name,
    ussr_argument_t *arguments,
    size_t index,
    ussr_value_t *value,
    const char **path
);

static int oop_do_read_bytes(
    ussr_argument_t *arguments,
    size_t argument_count,
    ussr_value_t *out_result
)
{
    ussr_value_t path_v;
    const char *path;
    ussr_vector_t *vector;
    FILE *input;
    int c;

    if (argument_count != 1)
    {
        fprintf(stderr, "USSR: read_bytes(v): \"path\"\n");
        return -1;
    }
    if (!oop_path_argument("read_bytes", arguments, 0, &path_v, &path))
        return -1;
    input = fopen(path, "rb");
    ussr_value_free(&path_v);
    if (input == NULL)
    {
        *out_result = ussr_null();
        return 0;
    }
    vector = ussr_vector_create(NULL);
    while (vector != NULL && (c = fgetc(input)) != EOF)
    {
        if (ussr_vector_push(vector, ussr_integer(c)) != 0)
        {
            ussr_vector_release(vector);
            vector = NULL;
        }
    }
    fclose(input);
    if (vector == NULL)
        return -1;
    *out_result = ussr_vector_value(vector);
    return 0;
}

/* text_bytes(v): "text" -> vector of the string's bytes (0..255): a
 * scanner indexes it in O(1), where byte() on a long string costs a
 * copy of the string per call. */
static int oop_do_text_bytes(
    ussr_argument_t *arguments,
    size_t argument_count,
    ussr_value_t *out_result
)
{
    ussr_value_t text_v;
    const char *text;
    ussr_vector_t *vector;
    size_t i;

    if (argument_count != 1)
    {
        fprintf(stderr, "USSR: text_bytes(v): \"text\"\n");
        return -1;
    }
    if (oop_eval(&arguments[0], &text_v) != 0)
        return -1;
    if (!oop_expect_string(&text_v, &text))
    {
        fprintf(stderr, "USSR: text_bytes expects a string\n");
        ussr_value_free(&text_v);
        return -1;
    }
    vector = ussr_vector_create(NULL);
    for (i = 0; vector != NULL && text[i] != '\0'; ++i)
    {
        if (ussr_vector_push(vector,
                ussr_integer((ussr_int_t)(unsigned char)text[i])) != 0)
        {
            ussr_vector_release(vector);
            vector = NULL;
        }
    }
    ussr_value_free(&text_v);
    if (vector == NULL)
        return -1;
    *out_result = ussr_vector_value(vector);
    return 0;
}

int ussr_value_bytes(const ussr_value_t *v, unsigned char **data,
                     size_t *length)
{
    size_t count;
    size_t i;
    unsigned char *buffer;

    if (v->type == USSR_STRING)
    {
        count = strlen(v->data.string);
        buffer = malloc(count + 1);
        if (buffer == NULL)
            return -1;
        memcpy(buffer, v->data.string, count + 1);
        *data = buffer;
        *length = count;
        return 0;
    }
    if (v->type != USSR_VECTOR)
        return -1;
    count = ussr_vector_length(v->data.vector);
    buffer = malloc(count + 1);
    if (buffer == NULL)
        return -1;
    for (i = 0; i < count; ++i)
    {
        ussr_value_t item;
        ussr_int_t byte = -1;

        if (!ussr_vector_get(v->data.vector, i, &item))
        {
            free(buffer);
            return -1;
        }
        if (!oop_expect_integer(&item, &byte) || byte < 0 || byte > 255)
        {
            ussr_value_free(&item);
            free(buffer);
            return -1;
        }
        buffer[i] = (unsigned char)byte;
    }
    buffer[count] = '\0';
    *data = buffer;
    *length = count;
    return 0;
}

int ussr_bytes_value(const unsigned char *data, size_t length,
                     ussr_value_t *out)
{
    ussr_vector_t *vector = ussr_vector_create(NULL);
    size_t i;

    for (i = 0; vector != NULL && i < length; ++i)
    {
        if (ussr_vector_push(vector, ussr_integer((ussr_int_t)data[i])) != 0)
        {
            ussr_vector_release(vector);
            vector = NULL;
        }
    }
    if (vector == NULL)
        return -1;
    *out = ussr_vector_value(vector);
    return 0;
}

/* ------------------------------------------------------------- */
/* hashes for Dubna's PostgreSQL authentication: SHA-256 (FIPS 180-4), */
/* HMAC (RFC 2104), PBKDF2 (RFC 8018) and MD5 (RFC 1321). These    */
/* authenticate a login; nothing here encrypts.                      */
/* ------------------------------------------------------------- */

typedef struct
{
    uint32_t state[8];
    uint64_t length;
    unsigned char block[64];
    size_t used;
} oop_sha256_t;

static const uint32_t oop_sha256_k[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1,
    0x923f82a4, 0xab1c5ed5, 0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3,
    0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786,
    0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147,
    0x06ca6351, 0x14292967, 0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13,
    0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85, 0xa2bfe8a1, 0xa81a664b,
    0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a,
    0x5b9cca4f, 0x682e6ff3, 0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
    0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2
};

#define OOP_ROTR(x, n) (((x) >> (n)) | ((x) << (32 - (n))))

static void oop_sha256_block(oop_sha256_t *ctx, const unsigned char *p)
{
    uint32_t w[64];
    uint32_t a, b, c, d, e, f, g, h;
    int i;

    for (i = 0; i < 16; ++i)
        w[i] = ((uint32_t)p[4 * i] << 24) | ((uint32_t)p[4 * i + 1] << 16) |
               ((uint32_t)p[4 * i + 2] << 8) | (uint32_t)p[4 * i + 3];
    for (i = 16; i < 64; ++i)
    {
        uint32_t s0 = OOP_ROTR(w[i - 15], 7) ^ OOP_ROTR(w[i - 15], 18) ^ (w[i - 15] >> 3);
        uint32_t s1 = OOP_ROTR(w[i - 2], 17) ^ OOP_ROTR(w[i - 2], 19) ^ (w[i - 2] >> 10);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }
    a = ctx->state[0]; b = ctx->state[1]; c = ctx->state[2]; d = ctx->state[3];
    e = ctx->state[4]; f = ctx->state[5]; g = ctx->state[6]; h = ctx->state[7];
    for (i = 0; i < 64; ++i)
    {
        uint32_t s1 = OOP_ROTR(e, 6) ^ OOP_ROTR(e, 11) ^ OOP_ROTR(e, 25);
        uint32_t ch = (e & f) ^ (~e & g);
        uint32_t t1 = h + s1 + ch + oop_sha256_k[i] + w[i];
        uint32_t s0 = OOP_ROTR(a, 2) ^ OOP_ROTR(a, 13) ^ OOP_ROTR(a, 22);
        uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
        uint32_t t2 = s0 + maj;
        h = g; g = f; f = e; e = d + t1;
        d = c; c = b; b = a; a = t1 + t2;
    }
    ctx->state[0] += a; ctx->state[1] += b; ctx->state[2] += c; ctx->state[3] += d;
    ctx->state[4] += e; ctx->state[5] += f; ctx->state[6] += g; ctx->state[7] += h;
}

static void oop_sha256_init(oop_sha256_t *ctx)
{
    static const uint32_t start[8] = {
        0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
        0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19
    };
    memcpy(ctx->state, start, sizeof(start));
    ctx->length = 0;
    ctx->used = 0;
}

static void oop_sha256_update(oop_sha256_t *ctx, const unsigned char *data,
                              size_t length)
{
    ctx->length += length;
    while (length > 0)
    {
        size_t take = 64 - ctx->used;
        if (take > length)
            take = length;
        memcpy(ctx->block + ctx->used, data, take);
        ctx->used += take;
        data += take;
        length -= take;
        if (ctx->used == 64)
        {
            oop_sha256_block(ctx, ctx->block);
            ctx->used = 0;
        }
    }
}

static void oop_sha256_final(oop_sha256_t *ctx, unsigned char out[32])
{
    uint64_t bits = ctx->length * 8;
    unsigned char pad = 0x80;
    unsigned char zero = 0;
    unsigned char tail[8];
    int i;

    oop_sha256_update(ctx, &pad, 1);
    while (ctx->used != 56)
        oop_sha256_update(ctx, &zero, 1);
    for (i = 0; i < 8; ++i)
        tail[i] = (unsigned char)(bits >> (56 - 8 * i));
    oop_sha256_update(ctx, tail, 8);
    for (i = 0; i < 8; ++i)
    {
        out[4 * i] = (unsigned char)(ctx->state[i] >> 24);
        out[4 * i + 1] = (unsigned char)(ctx->state[i] >> 16);
        out[4 * i + 2] = (unsigned char)(ctx->state[i] >> 8);
        out[4 * i + 3] = (unsigned char)ctx->state[i];
    }
}

static void oop_sha256(const unsigned char *data, size_t length,
                       unsigned char out[32])
{
    oop_sha256_t ctx;
    oop_sha256_init(&ctx);
    oop_sha256_update(&ctx, data, length);
    oop_sha256_final(&ctx, out);
}

/* HMAC-SHA-256 of a message given in two pieces (b may be empty). */
static void oop_hmac_sha256(const unsigned char *key, size_t key_length,
                            const unsigned char *a, size_t a_length,
                            const unsigned char *b, size_t b_length,
                            unsigned char out[32])
{
    unsigned char k[64];
    unsigned char pad[64];
    unsigned char inner[32];
    oop_sha256_t ctx;
    int i;

    memset(k, 0, sizeof(k));
    if (key_length > 64)
        oop_sha256(key, key_length, k);
    else if (key_length > 0)
        memcpy(k, key, key_length);
    for (i = 0; i < 64; ++i)
        pad[i] = k[i] ^ 0x36;
    oop_sha256_init(&ctx);
    oop_sha256_update(&ctx, pad, 64);
    oop_sha256_update(&ctx, a, a_length);
    oop_sha256_update(&ctx, b, b_length);
    oop_sha256_final(&ctx, inner);
    for (i = 0; i < 64; ++i)
        pad[i] = k[i] ^ 0x5c;
    oop_sha256_init(&ctx);
    oop_sha256_update(&ctx, pad, 64);
    oop_sha256_update(&ctx, inner, 32);
    oop_sha256_final(&ctx, out);
}

typedef struct
{
    uint32_t state[4];
    uint64_t length;
    unsigned char block[64];
    size_t used;
} oop_md5_t;

#define OOP_ROTL(x, n) (((x) << (n)) | ((x) >> (32 - (n))))

static void oop_md5_block(oop_md5_t *ctx, const unsigned char *p)
{
    static const uint32_t k[64] = {
        0xd76aa478, 0xe8c7b756, 0x242070db, 0xc1bdceee, 0xf57c0faf, 0x4787c62a,
        0xa8304613, 0xfd469501, 0x698098d8, 0x8b44f7af, 0xffff5bb1, 0x895cd7be,
        0x6b901122, 0xfd987193, 0xa679438e, 0x49b40821, 0xf61e2562, 0xc040b340,
        0x265e5a51, 0xe9b6c7aa, 0xd62f105d, 0x02441453, 0xd8a1e681, 0xe7d3fbc8,
        0x21e1cde6, 0xc33707d6, 0xf4d50d87, 0x455a14ed, 0xa9e3e905, 0xfcefa3f8,
        0x676f02d9, 0x8d2a4c8a, 0xfffa3942, 0x8771f681, 0x6d9d6122, 0xfde5380c,
        0xa4beea44, 0x4bdecfa9, 0xf6bb4b60, 0xbebfbc70, 0x289b7ec6, 0xeaa127fa,
        0xd4ef3085, 0x04881d05, 0xd9d4d039, 0xe6db99e5, 0x1fa27cf8, 0xc4ac5665,
        0xf4292244, 0x432aff97, 0xab9423a7, 0xfc93a039, 0x655b59c3, 0x8f0ccc92,
        0xffeff47d, 0x85845dd1, 0x6fa87e4f, 0xfe2ce6e0, 0xa3014314, 0x4e0811a1,
        0xf7537e82, 0xbd3af235, 0x2ad7d2bb, 0xeb86d391
    };
    static const int r[64] = {
        7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22,
        5, 9, 14, 20, 5, 9, 14, 20, 5, 9, 14, 20, 5, 9, 14, 20,
        4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23,
        6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21
    };
    uint32_t m[16];
    uint32_t a = ctx->state[0], b = ctx->state[1], c = ctx->state[2], d = ctx->state[3];
    int i;

    for (i = 0; i < 16; ++i)
        m[i] = (uint32_t)p[4 * i] | ((uint32_t)p[4 * i + 1] << 8) |
               ((uint32_t)p[4 * i + 2] << 16) | ((uint32_t)p[4 * i + 3] << 24);
    for (i = 0; i < 64; ++i)
    {
        uint32_t f;
        int g;
        if (i < 16) { f = (b & c) | (~b & d); g = i; }
        else if (i < 32) { f = (d & b) | (~d & c); g = (5 * i + 1) % 16; }
        else if (i < 48) { f = b ^ c ^ d; g = (3 * i + 5) % 16; }
        else { f = c ^ (b | ~d); g = (7 * i) % 16; }
        f = f + a + k[i] + m[g];
        a = d; d = c; c = b;
        b = b + OOP_ROTL(f, r[i]);
    }
    ctx->state[0] += a; ctx->state[1] += b; ctx->state[2] += c; ctx->state[3] += d;
}

static void oop_md5(const unsigned char *data, size_t length,
                    unsigned char out[16])
{
    oop_md5_t ctx;
    uint64_t bits = (uint64_t)length * 8;
    unsigned char tail[72];
    size_t rest;
    size_t tail_length;
    int i;

    ctx.state[0] = 0x67452301; ctx.state[1] = 0xefcdab89;
    ctx.state[2] = 0x98badcfe; ctx.state[3] = 0x10325476;
    while (length >= 64)
    {
        oop_md5_block(&ctx, data);
        data += 64;
        length -= 64;
    }
    rest = length;
    memset(tail, 0, sizeof(tail));
    memcpy(tail, data, rest);
    tail[rest] = 0x80;
    tail_length = rest + 1 + 8 <= 64 ? 64 : 128;
    for (i = 0; i < 8; ++i)
        tail[tail_length - 8 + i] = (unsigned char)(bits >> (8 * i));
    oop_md5_block(&ctx, tail);
    if (tail_length == 128)
        oop_md5_block(&ctx, tail + 64);
    for (i = 0; i < 4; ++i)
    {
        out[4 * i] = (unsigned char)ctx.state[i];
        out[4 * i + 1] = (unsigned char)(ctx.state[i] >> 8);
        out[4 * i + 2] = (unsigned char)(ctx.state[i] >> 16);
        out[4 * i + 3] = (unsigned char)(ctx.state[i] >> 24);
    }
}

/* The i-th argument as bytes: a string or a vector of 0..255. */
static int oop_bytes_argument(const char *name, ussr_argument_t *arguments,
                              size_t i, unsigned char **data, size_t *length)
{
    ussr_value_t v;
    int status;

    if (oop_eval(&arguments[i], &v) != 0)
        return -1;
    status = ussr_value_bytes(&v, data, length);
    ussr_value_free(&v);
    if (status != 0)
        fprintf(stderr, "USSR: %s expects a string or a vector of bytes "
                "0..255\n", name);
    return status;
}

/* sha256(v): data, md5(v): data, hmac_sha256(v): key data,
 * pbkdf2_sha256(v): password salt iterations [length]: digests as byte
 * vectors; data, keys, passwords and salts are strings or byte vectors. */
static int oop_do_hash(const char *command, ussr_argument_t *arguments,
                       size_t argument_count, ussr_value_t *out_result)
{
    unsigned char *a = NULL;
    unsigned char *b = NULL;
    size_t a_length = 0;
    size_t b_length = 0;
    unsigned char digest[32];
    int status = -1;

    if (strcmp(command, "sha256") == 0 || strcmp(command, "md5") == 0)
    {
        if (argument_count != 1)
        {
            fprintf(stderr, "USSR: %s(v): data\n", command);
            return -1;
        }
        if (oop_bytes_argument(command, arguments, 0, &a, &a_length) != 0)
            return -1;
        if (command[0] == 's')
        {
            oop_sha256(a, a_length, digest);
            status = ussr_bytes_value(digest, 32, out_result);
        }
        else
        {
            oop_md5(a, a_length, digest);
            status = ussr_bytes_value(digest, 16, out_result);
        }
        free(a);
        return status;
    }
    if (strcmp(command, "crc32") == 0)
    {
        /* crc32(n): data [from [count]]: the CRC-32 of zlib, GPT and
         * PNG over the bytes from..from+count-1 (default: all) */
        ussr_int_t range[2] = { 0, -1 };
        uint32_t crc = 0xFFFFFFFFu;
        size_t i;
        size_t k;

        if (argument_count < 1 || argument_count > 3)
        {
            fprintf(stderr, "USSR: crc32(n): data [from [count]]\n");
            return -1;
        }
        for (k = 1; k < argument_count; ++k)
        {
            ussr_value_t v;
            int ok;

            if (oop_eval(&arguments[k], &v) != 0)
                return -1;
            ok = oop_expect_integer(&v, &range[k - 1]);
            ussr_value_free(&v);
            if (!ok)
            {
                fprintf(stderr, "USSR: crc32: from and count are integers\n");
                return -1;
            }
        }
        if (oop_bytes_argument(command, arguments, 0, &a, &a_length) != 0)
            return -1;
        if (range[1] < 0)
            range[1] = (ussr_int_t)a_length - range[0];
        if (range[0] < 0 || range[1] < 0 ||
            (size_t)range[0] + (size_t)range[1] > a_length)
        {
            fprintf(stderr, "USSR: crc32: range out of bounds\n");
            free(a);
            return -1;
        }
        for (i = (size_t)range[0]; i < (size_t)(range[0] + range[1]); ++i)
        {
            crc ^= a[i];
            for (k = 0; k < 8; ++k)
                crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
        }
        free(a);
        *out_result = ussr_integer((ussr_int_t)(crc ^ 0xFFFFFFFFu));
        return 0;
    }
    if (strcmp(command, "hmac_sha256") == 0)
    {
        if (argument_count != 2)
        {
            fprintf(stderr, "USSR: hmac_sha256(v): key data\n");
            return -1;
        }
        if (oop_bytes_argument(command, arguments, 0, &a, &a_length) != 0)
            return -1;
        if (oop_bytes_argument(command, arguments, 1, &b, &b_length) != 0)
        {
            free(a);
            return -1;
        }
        oop_hmac_sha256(a, a_length, b, b_length, NULL, 0, digest);
        status = ussr_bytes_value(digest, 32, out_result);
        free(a);
        free(b);
        return status;
    }
    /* pbkdf2_sha256 */
    {
        ussr_value_t v;
        ussr_int_t iterations = 0;
        ussr_int_t want = 32;
        unsigned char *derived;
        unsigned char *salt_block;
        uint32_t block;
        ussr_int_t done = 0;

        if (argument_count != 3 && argument_count != 4)
        {
            fprintf(stderr, "USSR: pbkdf2_sha256(v): password salt iterations [length]\n");
            return -1;
        }
        if (oop_eval(&arguments[2], &v) != 0)
            return -1;
        if (!oop_expect_integer(&v, &iterations) || iterations < 1 ||
            iterations > 10000000)
        {
            ussr_value_free(&v);
            fprintf(stderr, "USSR: pbkdf2_sha256 takes 1 to 10000000 iterations\n");
            return -1;
        }
        ussr_value_free(&v);
        if (argument_count == 4)
        {
            if (oop_eval(&arguments[3], &v) != 0)
                return -1;
            if (!oop_expect_integer(&v, &want) || want < 1 || want > 1024)
            {
                ussr_value_free(&v);
                fprintf(stderr, "USSR: pbkdf2_sha256 length is 1 to 1024 bytes\n");
                return -1;
            }
            ussr_value_free(&v);
        }
        if (oop_bytes_argument(command, arguments, 0, &a, &a_length) != 0)
            return -1;
        if (oop_bytes_argument(command, arguments, 1, &b, &b_length) != 0)
        {
            free(a);
            return -1;
        }
        derived = malloc((size_t)want);
        salt_block = malloc(b_length + 4);
        if (derived == NULL || salt_block == NULL)
        {
            free(derived);
            free(salt_block);
            free(a);
            free(b);
            return -1;
        }
        memcpy(salt_block, b, b_length);
        for (block = 1; done < want; ++block)
        {
            unsigned char u[32];
            unsigned char t[32];
            ussr_int_t i;
            int j;
            ussr_int_t take;

            salt_block[b_length] = (unsigned char)(block >> 24);
            salt_block[b_length + 1] = (unsigned char)(block >> 16);
            salt_block[b_length + 2] = (unsigned char)(block >> 8);
            salt_block[b_length + 3] = (unsigned char)block;
            oop_hmac_sha256(a, a_length, salt_block, b_length + 4, NULL, 0, u);
            memcpy(t, u, 32);
            for (i = 1; i < iterations; ++i)
            {
                oop_hmac_sha256(a, a_length, u, 32, NULL, 0, u);
                for (j = 0; j < 32; ++j)
                    t[j] ^= u[j];
            }
            take = want - done < 32 ? want - done : 32;
            memcpy(derived + done, t, (size_t)take);
            done += take;
        }
        status = ussr_bytes_value(derived, (size_t)want, out_result);
        free(derived);
        free(salt_block);
        free(a);
        free(b);
        return status;
    }
}

/* bytes_text(s): bytes [from [count]] -> the string those bytes spell, or
 * null when one of them is 0 (a USSR string ends at a zero byte). */
static int oop_do_bytes_text(ussr_argument_t *arguments,
                             size_t argument_count, ussr_value_t *out_result)
{
    ussr_value_t vec_v;
    ussr_value_t v;
    ussr_vector_t *vector;
    ussr_int_t from = 0;
    ussr_int_t count;
    ussr_int_t total;
    char *text;
    ussr_int_t i;

    if (argument_count < 1 || argument_count > 3)
    {
        fprintf(stderr, "USSR: bytes_text(s): bytes [from [count]]\n");
        return -1;
    }
    if (oop_eval(&arguments[0], &vec_v) != 0)
        return -1;
    if (!oop_expect_vector(&vec_v, &vector))
    {
        fprintf(stderr, "USSR: bytes_text expects a vector of bytes\n");
        ussr_value_free(&vec_v);
        return -1;
    }
    total = (ussr_int_t)ussr_vector_length(vector);
    count = total;
    if (argument_count >= 2)
    {
        if (oop_eval(&arguments[1], &v) != 0 || !oop_expect_integer(&v, &from))
        {
            fprintf(stderr, "USSR: bytes_text: from must be an integer\n");
            ussr_value_free(&vec_v);
            return -1;
        }
        count = total - from;
    }
    if (argument_count == 3)
    {
        if (oop_eval(&arguments[2], &v) != 0 || !oop_expect_integer(&v, &count))
        {
            fprintf(stderr, "USSR: bytes_text: count must be an integer\n");
            ussr_value_free(&vec_v);
            return -1;
        }
    }
    if (from < 0 || count < 0 || from > total || count > total - from)
    {
        fprintf(stderr, "USSR: bytes_text: range out of bounds\n");
        ussr_value_free(&vec_v);
        return -1;
    }
    text = malloc((size_t)count + 1);
    if (text == NULL)
    {
        ussr_value_free(&vec_v);
        return -1;
    }
    for (i = 0; i < count; ++i)
    {
        ussr_value_t item;
        ussr_int_t byte = -1;

        if (!ussr_vector_get(vector, (size_t)(from + i), &item) ||
            !oop_expect_integer(&item, &byte) || byte < 0 || byte > 255)
        {
            fprintf(stderr, "USSR: bytes_text: item %lld is not a byte 0..255\n",
                    (long long)(from + i));
            free(text);
            ussr_value_free(&vec_v);
            return -1;
        }
        if (byte == 0)
        {
            free(text);
            ussr_value_free(&vec_v);
            *out_result = ussr_null();
            return 0;
        }
        text[i] = (char)byte;
    }
    text[count] = '\0';
    ussr_value_free(&vec_v);
    *out_result = ussr_string(text);
    free(text);
    return 0;
}

static int oop_do_write_bytes(
    ussr_argument_t *arguments,
    size_t argument_count,
    ussr_value_t *out_result
)
{
    ussr_value_t path_v;
    ussr_value_t vec_v;
    const char *path;
    ussr_vector_t *vector;
    unsigned char *data;
    size_t count;
    size_t i;
    FILE *output;
    int written;

    if (argument_count != 2)
    {
        fprintf(stderr, "USSR: write_bytes(n): \"path\" vec\n");
        return -1;
    }
    if (!oop_path_argument("write_bytes", arguments, 0, &path_v, &path))
        return -1;
    if (oop_eval(&arguments[1], &vec_v) != 0)
    {
        ussr_value_free(&path_v);
        return -1;
    }
    if (!oop_expect_vector(&vec_v, &vector))
    {
        fprintf(stderr, "USSR: write_bytes expects a vector\n");
        ussr_value_free(&path_v);
        ussr_value_free(&vec_v);
        return -1;
    }
    count = ussr_vector_length(vector);
    data = malloc(count > 0 ? count : 1);
    if (data == NULL)
    {
        ussr_value_free(&path_v);
        ussr_value_free(&vec_v);
        return -1;
    }
    for (i = 0; i < count; ++i)
    {
        ussr_value_t item;
        ussr_int_t byte = -1;

        if (!ussr_vector_get(vector, i, &item) ||
            !oop_expect_integer(&item, &byte) || byte < 0 || byte > 255)
        {
            fprintf(stderr, "USSR: write_bytes: item %u is not a byte "
                    "0..255\n", (unsigned)i);
            free(data);
            ussr_value_free(&path_v);
            ussr_value_free(&vec_v);
            return -1;
        }
        data[i] = (unsigned char)byte;
        ussr_value_free(&item);
    }
    ussr_value_free(&vec_v);

    output = fopen(path, "wb");
    ussr_value_free(&path_v);
    if (output == NULL)
    {
        free(data);
        *out_result = ussr_integer(-1);
        return 0;
    }
    written = fwrite(data, 1, count, output) == count;
    if (fclose(output) != 0)
        written = 0;
    free(data);
    *out_result = ussr_integer(written ? (ussr_int_t)count : -1);
    return 0;
}

/* ------------------------------------------------------------- */
/* Block devices (stalin.su):                                      */
/*   bread(v): dev lba [count [sector]]  -> count sectors (default  */
/*             1 of 512 bytes) from sector lba as a byte vector;    */
/*             null (non-fatal) when they cannot all be read        */
/*   bwrite(n): dev lba bytes [sector]   -> writes the bytes (a     */
/*             whole number of sectors) at sector lba; bytes        */
/*             written, or -1 (non-fatal)                           */
/*   bsize(n): dev                       -> size in bytes, -1 when  */
/*             unknown                                              */
/*   bdevices(v):                        -> one [path bytes kind    */
/*             bus model sector] vector per storage device; kind    */
/*             is HDD, SSD, flash, virtual or disk                  */
/* dev is a disk image file or a raw device: \\.\PhysicalDrive0 on  */
/* Windows, /dev/sda on Linux. The file must already exist; writing */
/* past the end of an image grows it.                               */
/* ------------------------------------------------------------- */

/* Reads or writes length bytes at offset; 0 when all of them moved. */
static int oop_block_io(const char *path, int writing, long long offset,
                        unsigned char *data, size_t length)
{
    size_t done = 0;
#ifdef _WIN32
    HANDLE handle;
    LARGE_INTEGER where;
    int ok;

    handle = CreateFileA(path,
                         writing ? GENERIC_READ | GENERIC_WRITE : GENERIC_READ,
                         FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                         OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (handle == INVALID_HANDLE_VALUE)
        return -1;
    where.QuadPart = offset;
    ok = SetFilePointerEx(handle, where, NULL, FILE_BEGIN) != 0;
    while (ok && done < length)
    {
        DWORD chunk = length - done > 0x40000000u ? 0x40000000u
                                                  : (DWORD)(length - done);
        DWORD moved = 0;

        if (writing)
            ok = WriteFile(handle, data + done, chunk, &moved, NULL) != 0;
        else
            ok = ReadFile(handle, data + done, chunk, &moved, NULL) != 0;
        if (moved == 0)
            ok = 0;
        done += moved;
    }
    if (ok && writing)
        FlushFileBuffers(handle);
    CloseHandle(handle);
    return ok && done == length ? 0 : -1;
#else
    int fd = open(path, writing ? O_RDWR : O_RDONLY);
    int ok = fd >= 0;

    while (ok && done < length)
    {
        ssize_t moved = writing
            ? pwrite(fd, data + done, length - done, (off_t)(offset + done))
            : pread(fd, data + done, length - done, (off_t)(offset + done));
        if (moved <= 0)
            ok = 0;
        else
            done += (size_t)moved;
    }
    if (ok && writing && fsync(fd) != 0)
        ok = 0;
    if (fd >= 0)
        close(fd);
    return ok && done == length ? 0 : -1;
#endif
}

/* The integer arguments of bread/bwrite from index first on: lba,
 * then count and sector when given (defaults 1 and 512). */
static int oop_block_numbers(const char *name, ussr_argument_t *arguments,
                             size_t first, size_t argument_count,
                             ussr_int_t *numbers, size_t wanted)
{
    size_t i;

    for (i = 0; i < wanted && first + i < argument_count; ++i)
    {
        ussr_value_t v;
        int ok;

        if (oop_eval(&arguments[first + i], &v) != 0)
            return 0;
        ok = oop_expect_integer(&v, &numbers[i]);
        ussr_value_free(&v);
        if (!ok)
        {
            fprintf(stderr, "USSR: %s expects integer sector numbers\n",
                    name);
            return 0;
        }
    }
    return 1;
}

static int oop_do_bread(
    ussr_argument_t *arguments,
    size_t argument_count,
    ussr_value_t *out_result
)
{
    ussr_value_t path_v;
    const char *path;
    ussr_int_t numbers[3] = { 0, 1, 512 };
    unsigned char *data;
    size_t length;
    int status;

    if (argument_count < 2 || argument_count > 4)
    {
        fprintf(stderr, "USSR: bread(v): device lba [count [sector]]\n");
        return -1;
    }
    if (!oop_block_numbers("bread", arguments, 1, argument_count,
                           numbers, 3))
        return -1;
    if (numbers[0] < 0 || numbers[1] < 0 || numbers[2] <= 0 ||
        numbers[1] > (1 << 24) / numbers[2])
    {
        fprintf(stderr, "USSR: bread: bad lba, count or sector size\n");
        return -1;
    }
    if (!oop_path_argument("bread", arguments, 0, &path_v, &path))
        return -1;
    length = (size_t)(numbers[1] * numbers[2]);
    data = malloc(length > 0 ? length : 1);
    if (data == NULL)
    {
        ussr_value_free(&path_v);
        return -1;
    }
    status = oop_block_io(path, 0, (long long)numbers[0] * numbers[2],
                          data, length);
    ussr_value_free(&path_v);
    if (status != 0)
    {
        free(data);
        *out_result = ussr_null();
        return 0;
    }
    status = ussr_bytes_value(data, length, out_result);
    free(data);
    return status;
}

static int oop_do_bwrite(
    ussr_argument_t *arguments,
    size_t argument_count,
    ussr_value_t *out_result
)
{
    ussr_value_t path_v;
    ussr_value_t bytes_v;
    const char *path;
    ussr_int_t numbers[2] = { 0, 512 };
    ussr_int_t sector;
    unsigned char *data = NULL;
    size_t length = 0;
    int status;

    if (argument_count < 3 || argument_count > 4)
    {
        fprintf(stderr, "USSR: bwrite(n): device lba bytes [sector]\n");
        return -1;
    }
    if (!oop_block_numbers("bwrite", arguments, 1, 2, numbers, 1))
        return -1;
    if (argument_count == 4 &&
        !oop_block_numbers("bwrite", arguments, 3, 4, numbers + 1, 1))
        return -1;
    sector = numbers[1];
    if (numbers[0] < 0 || sector <= 0)
    {
        fprintf(stderr, "USSR: bwrite: bad lba or sector size\n");
        return -1;
    }
    if (oop_eval(&arguments[2], &bytes_v) != 0)
        return -1;
    status = bytes_v.type == USSR_VECTOR
        ? ussr_value_bytes(&bytes_v, &data, &length) : -1;
    ussr_value_free(&bytes_v);
    if (status != 0)
    {
        fprintf(stderr, "USSR: bwrite expects a vector of bytes 0..255\n");
        return -1;
    }
    if (length == 0 || length % (size_t)sector != 0)
    {
        fprintf(stderr, "USSR: bwrite: %u bytes is not a whole number of "
                "%d-byte sectors\n", (unsigned)length, (int)sector);
        free(data);
        return -1;
    }
    if (!oop_path_argument("bwrite", arguments, 0, &path_v, &path))
    {
        free(data);
        return -1;
    }
    status = oop_block_io(path, 1, (long long)numbers[0] * sector,
                          data, length);
    ussr_value_free(&path_v);
    free(data);
    *out_result = ussr_integer(status == 0 ? (ussr_int_t)length : -1);
    return 0;
}

static long long oop_block_size(const char *path)
{
#ifdef _WIN32
    HANDLE handle;
    LARGE_INTEGER size;
    DISK_GEOMETRY_EX geometry;
    DWORD got = 0;
    long long result = -1;

    handle = CreateFileA(path, FILE_READ_ATTRIBUTES,
                         FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                         OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (handle == INVALID_HANDLE_VALUE)
        return -1;
    if (GetFileType(handle) == FILE_TYPE_DISK &&
        GetFileSizeEx(handle, &size))
        result = size.QuadPart;
    if (DeviceIoControl(handle, IOCTL_DISK_GET_DRIVE_GEOMETRY_EX, NULL, 0,
                        &geometry, sizeof geometry, &got, NULL))
        result = geometry.DiskSize.QuadPart;
    CloseHandle(handle);
    return result;
#else
    struct stat info;
    long long result = -1;
    int fd;

    if (stat(path, &info) != 0)
        return -1;
    if (S_ISREG(info.st_mode))
        return (long long)info.st_size;
    fd = open(path, O_RDONLY);
    if (fd < 0)
        return -1;
#if defined(__linux__) && defined(BLKGETSIZE64)
    {
        unsigned long long bytes = 0;
        if (ioctl(fd, BLKGETSIZE64, &bytes) == 0)
            result = (long long)bytes;
    }
#endif
    if (result < 0)
    {
        off_t end = lseek(fd, 0, SEEK_END);
        if (end > 0)
            result = (long long)end;
    }
    close(fd);
    return result;
#endif
}

static int oop_do_bsize(
    ussr_argument_t *arguments,
    size_t argument_count,
    ussr_value_t *out_result
)
{
    ussr_value_t path_v;
    const char *path;

    if (argument_count != 1)
    {
        fprintf(stderr, "USSR: bsize(n): device\n");
        return -1;
    }
    if (!oop_path_argument("bsize", arguments, 0, &path_v, &path))
        return -1;
    *out_result = ussr_integer((ussr_int_t)oop_block_size(path));
    ussr_value_free(&path_v);
    return 0;
}

/* Appends [path bytes kind bus model sector] to devices. */
static int oop_block_device_push(ussr_vector_t *devices, const char *path,
                                 long long bytes, const char *kind,
                                 const char *bus, const char *model,
                                 long long sector)
{
    ussr_vector_t *entry = ussr_vector_create(NULL);
    const char *texts[4];
    int failed;
    int i;

    if (entry == NULL)
        return -1;
    texts[0] = path;
    texts[1] = kind;
    texts[2] = bus;
    texts[3] = model;
    failed = 0;
    /* push retains what it stores: each temporary is released here */
    for (i = 0; i < 4 && !failed; ++i)
    {
        ussr_value_t text = ussr_string(texts[i]);

        failed = text.data.string == NULL ||
                 ussr_vector_push(entry, text) != 0;
        ussr_value_free(&text);
        if (!failed && i == 0)
            failed = ussr_vector_push(entry,
                                      ussr_integer((ussr_int_t)bytes)) != 0;
    }
    if (!failed)
        failed = ussr_vector_push(entry,
                                  ussr_integer((ussr_int_t)sector)) != 0;
    if (!failed)
        failed = ussr_vector_push(devices, ussr_vector_value(entry)) != 0;
    ussr_vector_release(entry);
    return failed ? -1 : 0;
}

/* Copies text without leading/trailing blanks into out (size bytes). */
static void oop_block_trim(const char *text, char *out, size_t size)
{
    size_t n;

    while (*text == ' ' || *text == '\t')
        ++text;
    n = strlen(text);
    while (n > 0 && (text[n - 1] == ' ' || text[n - 1] == '\t' ||
                     text[n - 1] == '\n' || text[n - 1] == '\r'))
        --n;
    if (n >= size)
        n = size - 1;
    memcpy(out, text, n);
    out[n] = '\0';
}

#ifdef _WIN32
static const char *oop_block_bus_name(int bus)
{
    static const char *names[] = {
        "unknown", "SCSI", "ATAPI", "ATA", "1394", "SSA", "FC", "USB",
        "RAID", "iSCSI", "SAS", "SATA", "SD", "MMC", "virtual",
        "file", "spaces", "NVMe", "SCM", "UFS"
    };
    return bus >= 0 && bus < (int)(sizeof names / sizeof names[0])
        ? names[bus] : "unknown";
}

static int oop_block_list(ussr_vector_t *devices)
{
    int i;

    for (i = 0; i < 64; ++i)
    {
        char path[64];
        char model[256];
        char raw[256];
        HANDLE handle;
        STORAGE_PROPERTY_QUERY query;
        BYTE buffer[1024];
        STORAGE_DEVICE_DESCRIPTOR *descriptor;
        DEVICE_SEEK_PENALTY_DESCRIPTOR penalty;
        DISK_GEOMETRY_EX geometry;
        DWORD got = 0;
        long long bytes = -1;
        long long sector = 512;
        int bus = 0;
        int removable = 0;
        int seeks = -1;
        const char *kind;

        snprintf(path, sizeof path, "\\\\.\\PhysicalDrive%d", i);
        handle = CreateFileA(path, 0, FILE_SHARE_READ | FILE_SHARE_WRITE,
                             NULL, OPEN_EXISTING, 0, NULL);
        if (handle == INVALID_HANDLE_VALUE)
            continue;
        model[0] = '\0';
        memset(&query, 0, sizeof query);
        query.PropertyId = StorageDeviceProperty;
        query.QueryType = PropertyStandardQuery;
        memset(buffer, 0, sizeof buffer);
        if (DeviceIoControl(handle, IOCTL_STORAGE_QUERY_PROPERTY,
                            &query, sizeof query, buffer, sizeof buffer,
                            &got, NULL) && got >= sizeof *descriptor)
        {
            descriptor = (STORAGE_DEVICE_DESCRIPTOR *)buffer;
            bus = (int)descriptor->BusType;
            removable = descriptor->RemovableMedia != 0;
            raw[0] = '\0';
            if (descriptor->VendorIdOffset != 0 &&
                descriptor->VendorIdOffset < sizeof buffer)
            {
                strncat(raw, (char *)buffer + descriptor->VendorIdOffset,
                        100);
                strcat(raw, " ");
            }
            if (descriptor->ProductIdOffset != 0 &&
                descriptor->ProductIdOffset < sizeof buffer)
                strncat(raw, (char *)buffer + descriptor->ProductIdOffset,
                        100);
            oop_block_trim(raw, model, sizeof model);
        }
        query.PropertyId = StorageDeviceSeekPenaltyProperty;
        memset(&penalty, 0, sizeof penalty);
        if (DeviceIoControl(handle, IOCTL_STORAGE_QUERY_PROPERTY,
                            &query, sizeof query, &penalty, sizeof penalty,
                            &got, NULL) && got >= sizeof penalty)
            seeks = penalty.IncursSeekPenalty != 0;
        if (DeviceIoControl(handle, IOCTL_DISK_GET_DRIVE_GEOMETRY_EX, NULL,
                            0, &geometry, sizeof geometry, &got, NULL))
        {
            bytes = geometry.DiskSize.QuadPart;
            if (geometry.Geometry.BytesPerSector > 0)
                sector = geometry.Geometry.BytesPerSector;
        }
        CloseHandle(handle);
        if (bus == BusTypeUsb || bus == BusTypeSd || bus == BusTypeMmc ||
            removable)
            kind = "flash";
        else if (bus == BusTypeVirtual || bus == BusTypeFileBackedVirtual)
            kind = "virtual";
        else if (bus == BusTypeNvme || seeks == 0)
            kind = "SSD";
        else if (seeks == 1)
            kind = "HDD";
        else
            kind = "disk";
        if (oop_block_device_push(devices, path, bytes, kind,
                                  oop_block_bus_name(bus), model,
                                  sector) != 0)
            return -1;
    }
    return 0;
}
#elif defined(__linux__)
/* The first line of /sys/block/name/what, trimmed; "" when unreadable. */
static void oop_block_sys(const char *name, const char *what, char *out,
                          size_t size)
{
    char path[512];
    char line[256];
    FILE *input;

    out[0] = '\0';
    snprintf(path, sizeof path, "/sys/block/%s/%s", name, what);
    input = fopen(path, "r");
    if (input == NULL)
        return;
    if (fgets(line, sizeof line, input) != NULL)
        oop_block_trim(line, out, size);
    fclose(input);
}

static int oop_block_list(ussr_vector_t *devices)
{
    DIR *dir = opendir("/sys/block");
    struct dirent *entry;
    int status = 0;

    if (dir == NULL)
        return 0;
    while (status == 0 && (entry = readdir(dir)) != NULL)
    {
        const char *name = entry->d_name;
        char text[256];
        char model[256];
        char link[1024];
        char path[300];
        char sys[300];
        long long bytes;
        long long sector;
        int rotational;
        int removable;
        ssize_t got;
        const char *kind;
        const char *bus = "SCSI";

        if (name[0] == '.' || strncmp(name, "loop", 4) == 0 ||
            strncmp(name, "ram", 3) == 0 || strncmp(name, "zram", 4) == 0 ||
            strncmp(name, "fd", 2) == 0 || strncmp(name, "sr", 2) == 0 ||
            strncmp(name, "dm-", 3) == 0 || strncmp(name, "md", 2) == 0)
            continue;
        oop_block_sys(name, "size", text, sizeof text);
        bytes = text[0] != '\0' ? atoll(text) * 512 : -1;
        oop_block_sys(name, "queue/logical_block_size", text, sizeof text);
        sector = text[0] != '\0' ? atoll(text) : 512;
        oop_block_sys(name, "queue/rotational", text, sizeof text);
        rotational = text[0] == '1';
        oop_block_sys(name, "removable", text, sizeof text);
        removable = text[0] == '1';
        oop_block_sys(name, "device/model", model, sizeof model);
        snprintf(sys, sizeof sys, "/sys/block/%s", name);
        got = readlink(sys, link, sizeof link - 1);
        link[got > 0 ? got : 0] = '\0';
        if (strstr(link, "/usb") != NULL)
            bus = "USB";
        else if (strncmp(name, "mmcblk", 6) == 0)
            bus = "MMC";
        else if (strncmp(name, "nvme", 4) == 0)
            bus = "NVMe";
        else if (strstr(link, "/ata") != NULL)
            bus = "ATA";
        else if (strstr(link, "virtio") != NULL ||
                 strncmp(name, "vd", 2) == 0)
            bus = "virtual";
        /* disks a hypervisor emulates on an ordinary bus */
        if (strstr(model, "Virtual") != NULL ||
            strstr(model, "QEMU") != NULL || strstr(model, "VBOX") != NULL ||
            strstr(model, "VMware") != NULL)
            removable = 0, rotational = -1;
        if (strcmp(bus, "USB") == 0 || strcmp(bus, "MMC") == 0 ||
            removable)
            kind = "flash";
        else if (strcmp(bus, "virtual") == 0 || rotational < 0)
            kind = "virtual";
        else
            kind = rotational ? "HDD" : "SSD";
        snprintf(path, sizeof path, "/dev/%s", name);
        status = oop_block_device_push(devices, path, bytes, kind, bus,
                                       model, sector);
    }
    closedir(dir);
    return status;
}
#else
static int oop_block_list(ussr_vector_t *devices)
{
    (void)devices;
    return 0;
}
#endif

static int oop_do_bdevices(
    size_t argument_count,
    ussr_value_t *out_result
)
{
    ussr_vector_t *devices;

    if (argument_count != 0)
    {
        fprintf(stderr, "USSR: bdevices(v):\n");
        return -1;
    }
    devices = ussr_vector_create(NULL);
    if (devices == NULL)
        return -1;
    if (oop_block_list(devices) != 0)
    {
        ussr_vector_release(devices);
        return -1;
    }
    *out_result = ussr_vector_value(devices);
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
/*   copy_file(v): from to    -> 0 ok / 1 error; copies one file's */
/*                               bytes, never replaces             */
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

/* Copies the bytes of file from into a new file to. Fails (nonzero) when
 * from is not a file or to already exists; a partial copy is removed. */
static int oop_copy_file(const char *from, const char *to)
{
#ifdef _WIN32
    return !oop_path_exists(from) || oop_path_is_dir(from) ||
           oop_path_exists(to) || !CopyFileA(from, to, TRUE);
#else
    struct stat info;
    FILE *in;
    FILE *out;
    char buffer[65536];
    size_t got;
    int failed = 0;

    if (stat(from, &info) != 0 || !S_ISREG(info.st_mode) ||
        oop_path_exists(to))
        return 1;
    in = fopen(from, "rb");
    if (in == NULL)
        return 1;
    out = fopen(to, "wb");
    if (out == NULL)
    {
        fclose(in);
        return 1;
    }
    while ((got = fread(buffer, 1, sizeof(buffer), in)) > 0)
        if (fwrite(buffer, 1, got, out) != got)
        {
            failed = 1;
            break;
        }
    if (ferror(in))
        failed = 1;
    fclose(in);
    if (fclose(out) != 0)
        failed = 1;
    if (failed)
        remove(to);
    else
        chmod(to, info.st_mode & 07777);
    return failed;
#endif
}

/* rename_path / copy_file / remove_file / remove_dir / make_dir: 0 ok,
 * 1 refused or failed. */
static int oop_do_path_change(
    const char *name,
    ussr_argument_t *arguments,
    size_t argument_count,
    ussr_value_t *out_result
)
{
    int copying = strcmp(name, "copy_file") == 0;
    int renaming = copying || strcmp(name, "rename_path") == 0;
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

    if (copying)
        failed = oop_copy_file(first, second);
    else if (renaming)
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

#ifndef _WIN32
/* Splits params at blanks into argv[first..]; "double quotes" group words.
 * The words point into params, which is modified. Returns the new count. */
static size_t oop_launch_words(char *params, char **argv, size_t first,
                               size_t limit)
{
    size_t count = first;
    char *p = params;

    while (*p != '\0' && count + 1 < limit)
    {
        char *word;
        while (*p == ' ' || *p == '\t')
            ++p;
        if (*p == '\0')
            break;
        if (*p == '"')
        {
            word = ++p;
            while (*p != '\0' && *p != '"')
                ++p;
        }
        else
        {
            word = p;
            while (*p != '\0' && *p != ' ' && *p != '\t')
                ++p;
        }
        if (*p != '\0')
            *p++ = '\0';
        argv[count++] = word;
    }
    argv[count] = NULL;
    return count;
}
#endif

/* launch(ok): target [params [hidden]] -> starts target without waiting:
 * a program (with the params string as its arguments), or a document,
 * folder or URL, which opens with the program the system has for it.
 * hidden starts a program without a console window (Windows). 0 when it
 * started, 1 when it could not. */
static int oop_do_launch(
    ussr_argument_t *arguments,
    size_t argument_count,
    ussr_value_t *out_result
)
{
    ussr_value_t target_v;
    ussr_value_t params_v;
    ussr_value_t hidden_v;
    const char *target;
    const char *params = "";
    int hidden = 0;
    int failed = 1;

    if (argument_count < 1 || argument_count > 3)
    {
        fprintf(stderr, "USSR: launch(ok): target [params [hidden]]\n");
        return -1;
    }
    if (!oop_env_argument("launch", arguments, 0, &target_v, &target))
        return -1;
    params_v = ussr_null();
    if (argument_count >= 2 &&
        !oop_env_argument("launch", arguments, 1, &params_v, &params))
    {
        ussr_value_free(&target_v);
        return -1;
    }
    if (argument_count == 3)
    {
        if (oop_eval(&arguments[2], &hidden_v) != 0)
        {
            ussr_value_free(&target_v);
            ussr_value_free(&params_v);
            return -1;
        }
        hidden = (hidden_v.type == USSR_BOOLEAN && hidden_v.data.boolean) ||
                 (hidden_v.type == USSR_INTEGER && hidden_v.data.integer != 0);
        ussr_value_free(&hidden_v);
    }
#ifdef _WIN32
    {
        /* Windows paths take backslashes; URLs keep their slashes. */
        size_t n = strlen(target);
        char *native = malloc(n + 1);
        if (native != NULL)
        {
            size_t i;
            memcpy(native, target, n + 1);
            if (strstr(native, "://") == NULL)
                for (i = 0; i < n; ++i)
                    if (native[i] == '/')
                        native[i] = '\\';
            if (hidden)
            {
                size_t size = n + strlen(params) + 4;
                char *line = malloc(size);
                if (line != NULL)
                {
                    STARTUPINFOA startup;
                    PROCESS_INFORMATION info;
                    memset(&startup, 0, sizeof(startup));
                    startup.cb = sizeof(startup);
                    snprintf(line, size, "\"%s\" %s", native, params);
                    if (CreateProcessA(NULL, line, NULL, NULL, FALSE,
                                       CREATE_NO_WINDOW |
                                           CREATE_NEW_PROCESS_GROUP,
                                       NULL, NULL, &startup, &info))
                    {
                        CloseHandle(info.hThread);
                        CloseHandle(info.hProcess);
                        failed = 0;
                    }
                    free(line);
                }
            }
            else
            {
                HINSTANCE status = ShellExecuteA(
                    NULL, "open", native, params[0] != '\0' ? params : NULL,
                    NULL, SW_SHOWNORMAL);
                failed = (INT_PTR)status <= 32;
            }
            free(native);
        }
    }
#else
    {
        /* Double fork so the program is not our child; a close-on-exec pipe
         * tells whether the exec worked. */
        char *words = strdup(params);
        int report[2];
        if (words != NULL && pipe(report) == 0)
        {
            pid_t child;
            fcntl(report[1], F_SETFD, FD_CLOEXEC);
            child = fork();
            if (child == 0)
            {
                char *argv[64];
                struct stat st;
                int is_program = 0;
                int null_fd;
                close(report[0]);
                setsid();
                if (fork() != 0)
                    _exit(0);
                null_fd = open("/dev/null", O_RDWR);
                if (null_fd >= 0)
                {
                    dup2(null_fd, 0);
                    if (hidden)
                    {
                        dup2(null_fd, 1);
                        dup2(null_fd, 2);
                    }
                }
                /* a bare name is a program on PATH; a path is one when it
                 * is an executable file */
                if (strchr(target, '/') == NULL && strstr(target, "://") == NULL)
                    is_program = 1;
                else if (stat(target, &st) == 0 && S_ISREG(st.st_mode) &&
                         access(target, X_OK) == 0)
                    is_program = 1;
                if (is_program)
                {
                    argv[0] = (char *)target;
                    oop_launch_words(words, argv, 1, 64);
                }
                else
                {
#ifdef __APPLE__
                    argv[0] = "open";
#else
                    argv[0] = "xdg-open";
#endif
                    argv[1] = (char *)target;
                    argv[2] = NULL;
                }
                execvp(argv[0], argv);
                {
                    char code = 1;
                    ssize_t ignored = write(report[1], &code, 1);
                    (void)ignored;
                }
                _exit(127);
            }
            close(report[1]);
            if (child > 0)
            {
                char code;
                waitpid(child, NULL, 0);
                failed = read(report[0], &code, 1) > 0;
            }
            close(report[0]);
        }
        free(words);
    }
#endif
    *out_result = ussr_integer(failed ? 1 : 0);
    ussr_value_free(&target_v);
    ussr_value_free(&params_v);
    return 0;
}

/* localtime(v): [seconds] -> local date and time of a time(t): value (now
 * without one) as [year month day hour minute second weekday yearday];
 * month 1..12, weekday 0 (Sunday)..6, yearday 1..366. */
static int oop_do_localtime(
    ussr_argument_t *arguments,
    size_t argument_count,
    ussr_value_t *out_result
)
{
    time_t when = time(NULL);
    struct tm parts;
    ussr_vector_t *vector;
    long fields[8];
    size_t i;

    if (argument_count > 1)
    {
        fprintf(stderr, "USSR: localtime(v): [seconds]\n");
        return -1;
    }
    if (argument_count == 1)
    {
        ussr_value_t seconds_v;
        if (oop_eval(&arguments[0], &seconds_v) != 0)
            return -1;
        if (seconds_v.type != USSR_INTEGER)
        {
            fprintf(stderr, "USSR: localtime() expects integer seconds\n");
            ussr_value_free(&seconds_v);
            return -1;
        }
        when = (time_t)seconds_v.data.integer;
        ussr_value_free(&seconds_v);
    }
#ifdef _WIN32
    if (localtime_s(&parts, &when) != 0)
#else
    if (localtime_r(&when, &parts) == NULL)
#endif
    {
        fprintf(stderr, "USSR: localtime cannot convert that time\n");
        return -1;
    }
    fields[0] = parts.tm_year + 1900L;
    fields[1] = parts.tm_mon + 1L;
    fields[2] = parts.tm_mday;
    fields[3] = parts.tm_hour;
    fields[4] = parts.tm_min;
    fields[5] = parts.tm_sec;
    fields[6] = parts.tm_wday;
    fields[7] = parts.tm_yday + 1L;
    vector = ussr_vector_create(NULL);
    for (i = 0; vector != NULL && i < 8; ++i)
        if (ussr_vector_push(vector, ussr_integer(fields[i])) != 0)
        {
            ussr_vector_release(vector);
            vector = NULL;
        }
    if (vector == NULL)
    {
        fprintf(stderr, "USSR: localtime: out of memory\n");
        return -1;
    }
    *out_result = ussr_vector_value(vector);
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

static void oop_free_values(ussr_value_t *values, size_t count);

/* Evaluate every argument as a string; on success the caller frees
 * values[0..count) and the array. */
static int oop_string_arguments(
    const char *usage,
    ussr_argument_t *arguments,
    size_t argument_count,
    ussr_value_t **out_values
)
{
    ussr_value_t *values;
    size_t i;

    values = calloc(argument_count ? argument_count : 1, sizeof(*values));
    if (values == NULL)
        return -1;
    for (i = 0; i < argument_count; ++i)
    {
        if (oop_eval(&arguments[i], &values[i]) != 0 ||
            values[i].type != USSR_STRING || values[i].data.string == NULL)
        {
            if (values[i].type != USSR_STRING)
                fprintf(stderr, "USSR: %s: arguments must be strings\n", usage);
            oop_free_values(values, i + 1);
            return -1;
        }
    }
    *out_values = values;
    return 0;
}

static void oop_free_values(ussr_value_t *values, size_t count)
{
    size_t i;

    for (i = 0; i < count; ++i)
        ussr_value_free(&values[i]);
    free(values);
}

/* keybind(_): "Alt+F1" "help(_):"   bind (an empty line unbinds)
 * keybind(line): "Alt+F1"           the bound line, or null
 * keybind(n):                       print every binding, n = count */
static int oop_do_keybind(
    ussr_argument_t *arguments,
    size_t argument_count,
    ussr_value_t *out_result
)
{
    ussr_value_t *values = NULL;
    char error[160];

    if (argument_count > 2)
    {
        fprintf(stderr, "USSR: keybind(_): \"Alt+F1\" \"help(_):\"\n");
        return -1;
    }
    if (oop_string_arguments("keybind", arguments, argument_count,
                             &values) != 0)
        return -1;

    if (argument_count == 0)
    {
        size_t i;
        int mods;
        int key;
        const char *line;

        for (i = 0; (line = ussr_keybind_at(i, &mods, &key)) != NULL; ++i)
        {
            char name[32];

            ussr_keybind_name(mods, key, name, sizeof(name));
            printf("%-14s %s\n", name, line);
        }
        *out_result = ussr_integer((ussr_int_t)i);
    }
    else if (argument_count == 1)
    {
        int mods;
        int key;
        const char *line;

        if (ussr_keybind_parse(values[0].data.string, &mods, &key,
                               error, sizeof(error)) != 0)
        {
            fprintf(stderr, "USSR: keybind: %s\n", error);
            oop_free_values(values, argument_count);
            return -1;
        }
        line = ussr_keybind_find(mods, key);
        *out_result = line != NULL ? ussr_string(line) : ussr_null();
    }
    else
    {
        if (ussr_keybind_set(values[0].data.string, values[1].data.string,
                             error, sizeof(error)) != 0)
        {
            fprintf(stderr, "USSR: keybind: %s\n", error);
            oop_free_values(values, argument_count);
            return -1;
        }
        *out_result = ussr_integer(0);
    }
    oop_free_values(values, argument_count);
    return 0;
}

/* text as a USSR string literal, quotes included */
static void oop_print_quoted(const char *text)
{
    putchar('"');
    for (; *text != '\0'; ++text)
    {
        if (*text == '"' || *text == '\\')
            putchar('\\');
        putchar(*text);
    }
    putchar('"');
}

/* alias(_): "ll" "ls" "-l"   ll(_): x then runs ls(_): "-l" x
 * alias(_): "ll" ""          remove it
 * alias(words): "ll"         its words as a vector, or null
 * alias(n):                  print every alias as the command that makes
 *                            it, n = count */
static int oop_do_alias(
    ussr_argument_t *arguments,
    size_t argument_count,
    ussr_value_t *out_result
)
{
    ussr_value_t *values = NULL;
    char error[160];

    if (oop_string_arguments("alias", arguments, argument_count,
                             &values) != 0)
        return -1;

    if (argument_count == 0)
    {
        size_t i;
        const char *name;
        const char *const *words;
        size_t count;

        for (i = 0; (name = ussr_alias_at(i, &words, &count)) != NULL; ++i)
        {
            size_t j;

            printf("alias(_): ");
            oop_print_quoted(name);
            for (j = 0; j < count; ++j)
            {
                putchar(' ');
                oop_print_quoted(words[j]);
            }
            putchar('\n');
        }
        fflush(stdout);
        *out_result = ussr_integer((ussr_int_t)i);
    }
    else if (argument_count == 1)
    {
        size_t count;
        const char *const *words =
            ussr_alias_find(values[0].data.string, &count);

        if (words == NULL)
            *out_result = ussr_null();
        else
        {
            ussr_vector_t *vector = ussr_vector_create(NULL);
            size_t j;

            if (vector == NULL)
            {
                oop_free_values(values, argument_count);
                return -1;
            }
            for (j = 0; j < count; ++j)
            {
                ussr_value_t word = ussr_string(words[j]);
                int failed = word.data.string == NULL ||
                             ussr_vector_push(vector, word) != 0;

                ussr_value_free(&word);
                if (failed)
                {
                    ussr_vector_release(vector);
                    oop_free_values(values, argument_count);
                    return -1;
                }
            }
            *out_result = ussr_vector_value(vector);
        }
    }
    else
    {
        const char **words = malloc((argument_count - 1) * sizeof(*words));
        size_t count = argument_count - 1;
        size_t j;
        int status;

        if (words == NULL)
        {
            oop_free_values(values, argument_count);
            return -1;
        }
        for (j = 0; j < count; ++j)
            words[j] = values[j + 1].data.string;
        /* alias(_): "ll" "" removes the alias */
        if (count == 1 && words[0][0] == '\0')
            count = 0;
        status = ussr_alias_set(values[0].data.string, words, count,
                                error, sizeof(error));
        free(words);
        if (status != 0)
        {
            fprintf(stderr, "USSR: alias: %s\n", error);
            oop_free_values(values, argument_count);
            return -1;
        }
        *out_result = ussr_integer(0);
    }
    oop_free_values(values, argument_count);
    return 0;
}

/* keybind_match(line): bytes   the line bound to the key that sends
 * these bytes (a terminal's sequence, as terminal_getch delivers it), or
 * null: lets a REPL written in USSR fire key bindings. */
static int oop_do_keybind_match(
    ussr_argument_t *arguments,
    size_t argument_count,
    ussr_value_t *out_result
)
{
    ussr_value_t *values = NULL;
    const char *line;

    if (argument_count != 1)
    {
        fprintf(stderr, "USSR: keybind_match(line): bytes\n");
        return -1;
    }
    if (oop_string_arguments("keybind_match", arguments, argument_count,
                             &values) != 0)
        return -1;
    line = ussr_keybind_match_sequence(values[0].data.string,
                                       strlen(values[0].data.string));
    *out_result = line != NULL ? ussr_string(line) : ussr_null();
    oop_free_values(values, argument_count);
    return 0;
}

/* prompt(answer): "question" ...   arguments are joined with spaces;
 * prompt(_) prints the answer instead of storing it. */
static int oop_do_prompt(
    const char *return_name,
    ussr_argument_t *arguments,
    size_t argument_count,
    ussr_value_t *out_result
)
{
    ussr_value_t *values = NULL;
    char *question;
    char *answer;
    char error[512];
    size_t length = 1;
    size_t i;

    if (argument_count == 0)
    {
        fprintf(stderr, "USSR: prompt(answer): \"question\"\n");
        return -1;
    }
    if (oop_string_arguments("prompt", arguments, argument_count,
                             &values) != 0)
        return -1;
    for (i = 0; i < argument_count; ++i)
        length += strlen(values[i].data.string) + 1;
    question = malloc(length);
    if (question == NULL)
    {
        oop_free_values(values, argument_count);
        return -1;
    }
    question[0] = '\0';
    for (i = 0; i < argument_count; ++i)
    {
        if (i > 0)
            strcat(question, " ");
        strcat(question, values[i].data.string);
    }
    oop_free_values(values, argument_count);

    answer = ussr_ai_prompt(question, error, sizeof(error));
    free(question);
    if (answer == NULL)
    {
        fprintf(stderr, "USSR: prompt: %s\n", error);
        return -1;
    }
    if (return_name == NULL || strcmp(return_name, "_") == 0)
    {
        size_t answer_length = strlen(answer);

        fputs(answer, stdout);
        if (answer_length == 0 || answer[answer_length - 1] != '\n')
            fputc('\n', stdout);
        fflush(stdout);
    }
    *out_result = ussr_string(answer);
    free(answer);
    return out_result->data.string != NULL ? 0 : -1;
}

/* help(_):  or  help(_): "name" */
static int oop_do_help(
    ussr_argument_t *arguments,
    size_t argument_count,
    ussr_value_t *out_result
)
{
    ussr_value_t *values = NULL;
    int found;

    if (argument_count > 1)
    {
        fprintf(stderr, "USSR: help(_): [\"name\"]\n");
        return -1;
    }
    if (oop_string_arguments("help", arguments, argument_count,
                             &values) != 0)
        return -1;
    found = ussr_help_print(argument_count ? values[0].data.string : NULL);
    fflush(stdout);
    oop_free_values(values, argument_count);
    *out_result = ussr_integer(found == 0 ? 0 : 1);
    return 0;
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
    else if (strcmp(command, "write_stderr") == 0)
        status = oop_do_write_stderr(arguments, argument_count, out_result);
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
    else if (strcmp(command, "read_bytes") == 0)
        status = oop_do_read_bytes(arguments, argument_count, out_result);
    else if (strcmp(command, "write_bytes") == 0)
        status = oop_do_write_bytes(arguments, argument_count, out_result);
    else if (strcmp(command, "bread") == 0)
        status = oop_do_bread(arguments, argument_count, out_result);
    else if (strcmp(command, "bwrite") == 0)
        status = oop_do_bwrite(arguments, argument_count, out_result);
    else if (strcmp(command, "bsize") == 0)
        status = oop_do_bsize(arguments, argument_count, out_result);
    else if (strcmp(command, "bdevices") == 0)
        status = oop_do_bdevices(argument_count, out_result);
    else if (strcmp(command, "text_bytes") == 0)
        status = oop_do_text_bytes(arguments, argument_count, out_result);
    else if (strcmp(command, "bytes_text") == 0)
        status = oop_do_bytes_text(arguments, argument_count, out_result);
    else if (strcmp(command, "sha256") == 0 ||
             strcmp(command, "md5") == 0 ||
             strcmp(command, "crc32") == 0 ||
             strcmp(command, "hmac_sha256") == 0 ||
             strcmp(command, "pbkdf2_sha256") == 0)
        status = oop_do_hash(command, arguments, argument_count, out_result);
    else if (strcmp(command, "file_exists") == 0)
        status = oop_do_file_exists(arguments, argument_count, out_result);
    else if (strcmp(command, "typeof") == 0)
        status = oop_do_typeof(arguments, argument_count, out_result);
    else if (strcmp(command, "getenv") == 0 ||
             strcmp(command, "setenv") == 0 ||
             strcmp(command, "unsetenv") == 0)
        status = oop_do_env(command, arguments, argument_count, out_result);
    else if (strcmp(command, "launch") == 0)
        status = oop_do_launch(arguments, argument_count, out_result);
    else if (strcmp(command, "localtime") == 0)
        status = oop_do_localtime(arguments, argument_count, out_result);
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
             strcmp(command, "copy_file") == 0 ||
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
    else if (strcmp(command, "terminal_key_ready") == 0)
        status = oop_do_terminal_key_ready(arguments, argument_count, out_result);
    else if (strcmp(command, "terminal_size") == 0)
        status = oop_do_terminal_size(arguments, argument_count, out_result);
    else if (strcmp(command, "encode") == 0)
        status = oop_do_encode(arguments, argument_count, out_result);
    else if (strcmp(command, "decode") == 0)
        status = oop_do_decode(arguments, argument_count, out_result);
    else if (strcmp(command, "keybind") == 0)
        status = oop_do_keybind(arguments, argument_count, out_result);
    else if (strcmp(command, "alias") == 0)
        status = oop_do_alias(arguments, argument_count, out_result);
    else if (strcmp(command, "keybind_match") == 0)
        status = oop_do_keybind_match(arguments, argument_count, out_result);
    else if (strcmp(command, "prompt") == 0)
        status = oop_do_prompt(return_name, arguments, argument_count,
                               out_result);
    else if (strcmp(command, "help") == 0)
        status = oop_do_help(arguments, argument_count, out_result);
    else
        return 0; /* not ours */

    return status == 0 ? 1 : -1;
}
