#include "ussr.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define USSR_INITIAL_VARIABLES 32

static ussr_variable_t *variables = NULL;
static size_t variable_count = 0;
static size_t variable_capacity = 0;

static void ussr_fatal(const char *message)
{
    fprintf(stderr, "USSR: %s\n", message);
    exit(EXIT_FAILURE);
}

static char *ussr_strdup(const char *src)
{
    size_t length;
    char *dst;

    if (src == NULL)
        return NULL;

    length = strlen(src);

    dst = malloc(length + 1);

    if (dst == NULL)
        ussr_fatal("out of memory");

    memcpy(dst, src, length + 1);

    return dst;
}

void ussr_init(void)
{
    variables = calloc(
        USSR_INITIAL_VARIABLES,
        sizeof(*variables)
    );

    if (variables == NULL)
        ussr_fatal("out of memory");

    variable_capacity = USSR_INITIAL_VARIABLES;
    variable_count = 0;
}

void ussr_cleanup(void)
{
    size_t i;

    for (i = 0; i < variable_count; ++i)
    {
        free(variables[i].name);
        ussr_value_free(&variables[i].value);
    }

    free(variables);

    variables = NULL;
    variable_count = 0;
    variable_capacity = 0;
}

ussr_value_t ussr_null(void)
{
    ussr_value_t value;

    value.type = USSR_NULL;

    return value;
}

ussr_value_t ussr_integer(long integer)
{
    ussr_value_t value;

    value.type = USSR_INTEGER;
    value.data.integer = integer;

    return value;
}

ussr_value_t ussr_real(double real)
{
    ussr_value_t value;

    value.type = USSR_REAL;
    value.data.real = real;

    return value;
}

ussr_value_t ussr_boolean(int boolean)
{
    ussr_value_t value;

    value.type = USSR_BOOLEAN;
    value.data.boolean = boolean != 0;

    return value;
}

ussr_value_t ussr_string(const char *string)
{
    ussr_value_t value;

    value.type = USSR_STRING;
    value.data.string = ussr_strdup(string);

    return value;
}

void ussr_value_free(ussr_value_t *value)
{
    if (value == NULL)
        return;

    if (value->type == USSR_STRING)
        free(value->data.string);

    value->type = USSR_NULL;
}

ussr_value_t ussr_value_copy(const ussr_value_t *value)
{
    ussr_value_t copy;

    if (value == NULL)
        return ussr_null();

    copy.type = value->type;

    switch (value->type)
    {
        case USSR_NULL:
            break;

        case USSR_INTEGER:
            copy.data.integer = value->data.integer;
            break;

        case USSR_REAL:
            copy.data.real = value->data.real;
            break;

        case USSR_BOOLEAN:
            copy.data.boolean = value->data.boolean;
            break;

        case USSR_STRING:
            copy.data.string = ussr_strdup(value->data.string);
            break;
    }

    return copy;
}

static void ussr_grow_variables(void)
{
    size_t new_capacity;
    ussr_variable_t *new_variables;

    new_capacity = variable_capacity * 2;

    new_variables = realloc(
        variables,
        new_capacity * sizeof(*new_variables)
    );

    if (new_variables == NULL)
        ussr_fatal("out of memory");

    variables = new_variables;
    variable_capacity = new_capacity;
}

int ussr_set_variable(
    const char *name,
    const ussr_value_t *value
)
{
    size_t i;

    if (name == NULL || value == NULL)
        return -1;

    for (i = 0; i < variable_count; ++i)
    {
        if (strcmp(variables[i].name, name) == 0)
        {
            ussr_value_free(&variables[i].value);
            variables[i].value = ussr_value_copy(value);
            return 0;
        }
    }

    if (variable_count == variable_capacity)
        ussr_grow_variables();

    variables[variable_count].name = ussr_strdup(name);
    variables[variable_count].value = ussr_value_copy(value);

    ++variable_count;

    return 0;
}

const ussr_value_t *ussr_get_variable(const char *name)
{
    size_t i;

    if (name == NULL)
        return NULL;

    for (i = 0; i < variable_count; ++i)
    {
        if (strcmp(variables[i].name, name) == 0)
            return &variables[i].value;
    }

    return NULL;
}

void ussr_print_value(const ussr_value_t *value)
{
    if (value == NULL)
    {
        printf("null\n");
        return;
    }

    switch (value->type)
    {
        case USSR_NULL:
            printf("null\n");
            break;

        case USSR_INTEGER:
            printf("%ld\n", value->data.integer);
            break;

        case USSR_REAL:
            printf("%g\n", value->data.real);
            break;

        case USSR_STRING:
            printf("%s\n", value->data.string);
            break;

        case USSR_BOOLEAN:
            printf("%s\n", value->data.boolean ? "true" : "false");
            break;
    }
}

int ussr_execute_command(
    const char *command,
    const char *return_name,
    ussr_value_t *parameters,
    size_t parameter_count
)
{
    ussr_value_t result;
    size_t i;

    if (command == NULL || return_name == NULL)
        return -1;

    result = ussr_null();

    /*
     * set(value): value
     */
    if (strcmp(command, "set") == 0)
    {
        if (parameter_count != 1)
        {
            fprintf(stderr,
                    "USSR: set expects 1 parameter\n");
            return -1;
        }

        result = ussr_value_copy(&parameters[0]);
    }

    /*
     * print(value): value
     */
    else if (strcmp(command, "print") == 0)
    {
        if (parameter_count != 1)
        {
            fprintf(stderr,
                    "USSR: print expects 1 parameter\n");
            return -1;
        }

        ussr_print_value(&parameters[0]);
        result = ussr_value_copy(&parameters[0]);
    }

    /*
     * add(a, b): a b
     *
     * The language currently uses space-separated parameters,
     * therefore:
     *
     * add(result): 10 20
     */
    else if (strcmp(command, "add") == 0)
    {
        if (parameter_count != 2)
        {
            fprintf(stderr,
                    "USSR: add expects 2 parameters\n");
            return -1;
        }

        if (parameters[0].type == USSR_INTEGER &&
            parameters[1].type == USSR_INTEGER)
        {
            result = ussr_integer(
                parameters[0].data.integer +
                parameters[1].data.integer
            );
        }
        else if (
            (parameters[0].type == USSR_INTEGER ||
             parameters[0].type == USSR_REAL) &&
            (parameters[1].type == USSR_INTEGER ||
             parameters[1].type == USSR_REAL))
        {
            double a;
            double b;

            a = parameters[0].type == USSR_REAL
                ? parameters[0].data.real
                : (double)parameters[0].data.integer;

            b = parameters[1].type == USSR_REAL
                ? parameters[1].data.real
                : (double)parameters[1].data.integer;

            result = ussr_real(a + b);
        }
        else
        {
            fprintf(stderr,
                    "USSR: add requires numeric parameters\n");
            return -1;
        }
    }

    else
    {
        fprintf(stderr,
                "USSR: unknown command '%s'\n",
                command);
        return -1;
    }

    if (ussr_set_variable(return_name, &result) != 0)
    {
        ussr_value_free(&result);
        return -1;
    }

    /*
     * The command result is now owned by the variable table.
     */
    ussr_value_free(&result);

    for (i = 0; i < parameter_count; ++i)
        ussr_value_free(&parameters[i]);

    return 0;
}