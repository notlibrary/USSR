#include "ussr_oop_builtins.h"
#include "uno.h"

#include <errno.h>
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
#include <sys/ioctl.h>
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

    if (vec_v.type == USSR_STRING && vec_v.data.string != NULL)
    {
        const char *text = vec_v.data.string;
        size_t text_length = strlen(text);
        char *piece;

        if (!oop_expect_integer(&index_v, &index) || index < 0 ||
            (size_t)index >= text_length)
        {
            fprintf(stderr, "USSR: string index %ld out of range\n", index);
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

    if (vec_v.type == USSR_STRING && vec_v.data.string != NULL)
    {
        *out_result = ussr_integer((long)strlen(vec_v.data.string));
        ussr_value_free(&vec_v);
        return 0;
    }

    if (!oop_expect_vector(&vec_v, &vector))
    {
        fprintf(stderr, "USSR: len() expects a vector or a string\n");
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
    long index;

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

    *out_result = ussr_integer((long)(unsigned char)text[index]);
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
    long index, count, text_length;

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

    text_length = (long)strlen(text);
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
/* Portable host primitives for interactive programs (vibe.su).  */
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
static unsigned char term_pushback[4];
static int term_pushback_count = 0;
static int term_atexit_registered = 0;

static void term_restore(void)
{
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
            int c = _getch();

            if (c == 0 || c == 0xE0)
            {
                /* special key: translate arrows to ESC [ letter */
                int c2 = _getch();
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

            *out_result = ussr_integer(c);
            return 0;
        }
    }

    /* redirected stdin (pipe/file): plain byte read */
    {
        unsigned char c;
        int n = _read(0, &c, 1);

        *out_result = ussr_integer(n == 1 ? (long)c : -1);
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

    *out_result = ussr_integer(n == 1 ? (long)c : -1);
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
    else if (strcmp(command, "byte") == 0)
        status = oop_do_byte(arguments, argument_count, out_result);
    else if (strcmp(command, "slice") == 0)
        status = oop_do_slice(arguments, argument_count, out_result);
    else if (strcmp(command, "readfile") == 0)
        status = oop_do_readfile(arguments, argument_count, out_result);
    else if (strcmp(command, "writefile") == 0)
        status = oop_do_writefile(arguments, argument_count, out_result);
    else if (strcmp(command, "file_exists") == 0)
        status = oop_do_file_exists(arguments, argument_count, out_result);
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
