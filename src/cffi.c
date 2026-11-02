#include "cffi.h"
#include "uno.h"
#include "ussr_oop_builtins.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <dlfcn.h>
#endif

/* ussr_argument_evaluate() is implemented by the runtime (ussr.c). */
extern int ussr_argument_evaluate(
    const ussr_argument_t *argument,
    ussr_value_t *result
);

#define CFFI_ARGS 12

typedef uint64_t (*cffi_fn)(uint64_t, uint64_t, uint64_t, uint64_t,
                            uint64_t, uint64_t, uint64_t, uint64_t,
                            uint64_t, uint64_t, uint64_t, uint64_t);

static int cffi_eval(ussr_argument_t *argument, ussr_value_t *v)
{
    return ussr_argument_evaluate(argument, v);
}

/* an integer argument (also null, false, true): 0 ok */
static int cffi_int(const char *command, ussr_argument_t *argument,
                    uint64_t *out)
{
    ussr_value_t v;

    if (cffi_eval(argument, &v) != 0)
        return -1;
    if (v.type == USSR_INTEGER)
        *out = (uint64_t)v.data.integer;
    else if (v.type == USSR_BOOLEAN)
        *out = v.data.boolean ? 1 : 0;
    else if (v.type == USSR_NULL)
        *out = 0;
    else
    {
        fprintf(stderr, "USSR: %s takes an address or a number\n", command);
        ussr_value_free(&v);
        return -1;
    }
    ussr_value_free(&v);
    return 0;
}

static int cffi_load(ussr_argument_t *arguments, size_t count,
                     ussr_value_t *out)
{
    ussr_value_t path;
    void *lib;

    if (count != 1 || cffi_eval(&arguments[0], &path) != 0)
        return -1;
    if (path.type != USSR_STRING)
    {
        fprintf(stderr, "USSR: c_load(lib): path\n");
        ussr_value_free(&path);
        return -1;
    }
#ifdef _WIN32
    lib = (void *)LoadLibraryA(path.data.string);
#else
    lib = dlopen(path.data.string, RTLD_NOW | RTLD_LOCAL);
#endif
    ussr_value_free(&path);
    *out = lib != NULL ? ussr_integer((ussr_int_t)(uintptr_t)lib) : ussr_null();
    return 0;
}

static int cffi_sym(ussr_argument_t *arguments, size_t count,
                    ussr_value_t *out)
{
    uint64_t lib;
    ussr_value_t name;
    uintptr_t at;

    if (count != 2 || cffi_int("c_sym", &arguments[0], &lib) != 0)
        return -1;
    if (cffi_eval(&arguments[1], &name) != 0)
        return -1;
    if (name.type != USSR_STRING || lib == 0)
    {
        fprintf(stderr, "USSR: c_sym(f): lib name\n");
        ussr_value_free(&name);
        return -1;
    }
#ifdef _WIN32
    at = (uintptr_t)GetProcAddress((HMODULE)(uintptr_t)lib, name.data.string);
#else
    at = (uintptr_t)dlsym((void *)(uintptr_t)lib, name.data.string);
#endif
    ussr_value_free(&name);
    *out = at != 0 ? ussr_integer((ussr_int_t)at) : ussr_null();
    return 0;
}

static int cffi_call(ussr_argument_t *arguments, size_t count,
                     ussr_value_t *out)
{
    uint64_t fn;
    uint64_t a[CFFI_ARGS];
    ussr_value_t held[CFFI_ARGS];
    unsigned char *copies[CFFI_ARGS];
    size_t n = count - 1;
    size_t i;
    int status = 0;
    uint64_t result;

    if (count < 1 || count > CFFI_ARGS + 1)
    {
        fprintf(stderr, "USSR: c_call(r): f [up to %d arguments]\n", CFFI_ARGS);
        return -1;
    }
    if (cffi_int("c_call", &arguments[0], &fn) != 0)
        return -1;
    if (fn == 0)
    {
        fprintf(stderr, "USSR: c_call: no function (address 0)\n");
        return -1;
    }
    memset(a, 0, sizeof a);
    memset(copies, 0, sizeof copies);
    for (i = 0; i < n; ++i)
        held[i] = ussr_null();
    for (i = 0; i < n && status == 0; ++i)
    {
        ussr_value_t *v = &held[i];

        if (cffi_eval(&arguments[i + 1], v) != 0)
        {
            status = -1;
            break;
        }
        switch (v->type)
        {
        case USSR_INTEGER:
            a[i] = (uint64_t)v->data.integer;
            break;
        case USSR_BOOLEAN:
            a[i] = v->data.boolean ? 1 : 0;
            break;
        case USSR_NULL:
            a[i] = 0;
            break;
        case USSR_STRING:
            a[i] = (uint64_t)(uintptr_t)v->data.string;
            break;
        case USSR_VECTOR:
        {
            ussr_vector_t *vec = v->data.vector;
            size_t k;

            copies[i] = malloc(vec->count + 1);
            if (copies[i] == NULL)
            {
                status = -1;
                break;
            }
            for (k = 0; k < vec->count; ++k)
            {
                if (vec->items[k].type != USSR_INTEGER ||
                    vec->items[k].data.integer < 0 ||
                    vec->items[k].data.integer > 255)
                {
                    fprintf(stderr, "USSR: c_call: a vector argument is a "
                            "vector of bytes 0..255\n");
                    status = -1;
                    break;
                }
                copies[i][k] = (unsigned char)vec->items[k].data.integer;
            }
            copies[i][vec->count] = 0;
            a[i] = (uint64_t)(uintptr_t)copies[i];
            break;
        }
        default:
            fprintf(stderr, "USSR: c_call: argument %u is an integer, a "
                    "string, a byte vector, true, false or null\n",
                    (unsigned)(i + 1));
            status = -1;
        }
    }
    if (status == 0)
    {
        result = ((cffi_fn)(uintptr_t)fn)(a[0], a[1], a[2], a[3], a[4], a[5],
                                          a[6], a[7], a[8], a[9], a[10],
                                          a[11]);
        /* the buffers come back into their vectors */
        for (i = 0; i < n; ++i)
        {
            if (copies[i] != NULL && held[i].type == USSR_VECTOR)
            {
                ussr_vector_t *vec = held[i].data.vector;
                size_t k;

                for (k = 0; k < vec->count; ++k)
                    vec->items[k].data.integer = copies[i][k];
            }
        }
        *out = ussr_integer((ussr_int_t)result);
    }
    for (i = 0; i < n; ++i)
    {
        free(copies[i]);
        ussr_value_free(&held[i]);
    }
    return status;
}

static int cffi_alloc(ussr_argument_t *arguments, size_t count,
                      ussr_value_t *out)
{
    uint64_t n;
    void *p;

    if (count != 1 || cffi_int("c_alloc", &arguments[0], &n) != 0)
        return -1;
    p = calloc(n > 0 ? (size_t)n : 1, 1);
    *out = p != NULL ? ussr_integer((ussr_int_t)(uintptr_t)p) : ussr_null();
    return 0;
}

static int cffi_free(ussr_argument_t *arguments, size_t count,
                     ussr_value_t *out)
{
    uint64_t p;

    if (count != 1 || cffi_int("c_free", &arguments[0], &p) != 0)
        return -1;
    free((void *)(uintptr_t)p);
    *out = ussr_integer(0);
    return 0;
}

static int cffi_read(ussr_argument_t *arguments, size_t count,
                     ussr_value_t *out)
{
    uint64_t p;
    uint64_t n;

    if (count != 2 || cffi_int("c_read", &arguments[0], &p) != 0 ||
        cffi_int("c_read", &arguments[1], &n) != 0)
        return -1;
    if (p == 0)
    {
        fprintf(stderr, "USSR: c_read: address 0\n");
        return -1;
    }
    return ussr_bytes_value((const unsigned char *)(uintptr_t)p, (size_t)n,
                            out) == 0 ? 0 : -1;
}

static int cffi_write(ussr_argument_t *arguments, size_t count,
                      ussr_value_t *out)
{
    uint64_t p;
    ussr_value_t v;
    unsigned char *data;
    size_t length;

    if (count != 2 || cffi_int("c_write", &arguments[0], &p) != 0)
        return -1;
    if (cffi_eval(&arguments[1], &v) != 0)
        return -1;
    if (p == 0 || ussr_value_bytes(&v, &data, &length) != 0)
    {
        fprintf(stderr, "USSR: c_write(n): p data (a string or a byte vector)\n");
        ussr_value_free(&v);
        return -1;
    }
    memcpy((void *)(uintptr_t)p, data, length);
    free(data);
    ussr_value_free(&v);
    *out = ussr_integer((ussr_int_t)length);
    return 0;
}

static int cffi_string(ussr_argument_t *arguments, size_t count,
                       ussr_value_t *out)
{
    uint64_t p;

    if (count != 1 || cffi_int("c_string", &arguments[0], &p) != 0)
        return -1;
    *out = p != 0 ? ussr_string((const char *)(uintptr_t)p) : ussr_null();
    return 0;
}

int ussr_cffi_dispatch(const char *command, ussr_argument_t *arguments,
                       size_t argument_count, ussr_value_t *out_result)
{
    int status;

    if (command[0] != 'c' || command[1] != '_')
        return 0;
    if (strcmp(command, "c_load") == 0)
        status = cffi_load(arguments, argument_count, out_result);
    else if (strcmp(command, "c_sym") == 0)
        status = cffi_sym(arguments, argument_count, out_result);
    else if (strcmp(command, "c_call") == 0)
        status = cffi_call(arguments, argument_count, out_result);
    else if (strcmp(command, "c_alloc") == 0)
        status = cffi_alloc(arguments, argument_count, out_result);
    else if (strcmp(command, "c_free") == 0)
        status = cffi_free(arguments, argument_count, out_result);
    else if (strcmp(command, "c_read") == 0)
        status = cffi_read(arguments, argument_count, out_result);
    else if (strcmp(command, "c_write") == 0)
        status = cffi_write(arguments, argument_count, out_result);
    else if (strcmp(command, "c_string") == 0)
        status = cffi_string(arguments, argument_count, out_result);
    else
        return 0;
    return status == 0 ? 1 : -1;
}
