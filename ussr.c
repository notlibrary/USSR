#include "ussr.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <math.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include "uthash.h"

int yylex(void);
int yyparse(void);
void yyerror(const char *message);

extern int yylineno;
extern char *yytext;
extern FILE *yyin;

typedef struct yy_buffer_state *YY_BUFFER_STATE;

extern YY_BUFFER_STATE yy_scan_string(const char *str);
extern void yy_delete_buffer(YY_BUFFER_STATE buffer);

extern ussr_command_list_t *ussr_parsed_program;

static char *ussr_strdup(const char *src);
static ussr_definition_t *ussr_find_definition(const char *name);
static int ussr_argument_evaluate(
    const ussr_argument_t *argument,
    ussr_value_t *result
);
static int ussr_remove_variable(const char *name);
void ussr_expression_free(ussr_expression_t *expression);

typedef enum
{
    USSR_EXEC_OK = 0,
    USSR_EXEC_ERROR = -1,
    USSR_EXEC_BREAK = 1,
    USSR_EXEC_CONTINUE = 2,
    USSR_EXEC_RETURN = 3
} ussr_exec_status_t;

#define USSR_MAX_LOOP_ITERATIONS 1000000UL

/* Local variables are kept in their own linear store. */
typedef struct
{
    char *name;
    ussr_value_t value;
} ussr_local_variable_t;

typedef struct ussr_hash_entry_t
{
    char *name;
    ussr_value_t value;
    UT_hash_handle hh;
} ussr_hash_entry_t;

static ussr_local_variable_t *locals = NULL;
static size_t local_count = 0;
static size_t local_capacity = 0;

/* Explicit !/? variables live in a separate uthash table. */
static ussr_hash_entry_t *map = NULL;
static ussr_definition_t *definitions = NULL;

ussr_expression_t *
ussr_make_binary_expression(
    ussr_expression_t *left,
    ussr_operator_t operator,
    ussr_expression_t *right
)
{
    ussr_expression_t *expression;

    expression = malloc(sizeof(*expression));
    if (expression == NULL)
    {
        ussr_expression_free(left);
        ussr_expression_free(right);
        return NULL;
    }

    expression->type = USSR_EXPR_BINARY;
    expression->data.binary.left = left;
    expression->data.binary.operator = operator;
    expression->data.binary.right = right;

    return expression;
}

int
ussr_execute_user_definition(
    const char *name,
    const char *return_name,
    ussr_argument_t *arguments,
    size_t argument_count
)
{
    ussr_definition_t *definition;
    ussr_saved_variable_t *saved;
    ussr_value_t *values;
    ussr_value_t result;
    const ussr_value_t *return_value;
    size_t i;
    int execute_result;

    definition = ussr_find_definition(name);

    if (definition == NULL)
        return -1;

    if (argument_count != definition->parameter_count)
    {
        fprintf(
            stderr,
            "USSR: %s expects %zu parameter%s\n",
            name,
            definition->parameter_count,
            definition->parameter_count == 1 ? "" : "s"
        );
        return -1;
    }

    saved = calloc(
        definition->parameter_count + 1,
        sizeof(*saved)
    );

    values = calloc(
        definition->parameter_count,
        sizeof(*values)
    );

    if (saved == NULL ||
        (definition->parameter_count != 0 && values == NULL))
    {
        free(saved);
        free(values);
        return -1;
    }

    /*
     * Evaluate arguments before modifying the caller's map.
     */
    for (i = 0; i < definition->parameter_count; ++i)
    {
        if (ussr_argument_evaluate(
                &arguments[i],
                &values[i]) != 0)
        {
            while (i > 0)
            {
                --i;
                ussr_value_free(&values[i]);
            }

            free(values);
            free(saved);

            return -1;
        }
    }

    /*
     * Save parameter map and the definition's return variable.
     */
    for (i = 0; i < definition->parameter_count; ++i)
    {
        const ussr_value_t *old;

        saved[i].name =
            ussr_strdup(definition->parameter_names[i]);

        old = ussr_get_variable(
            definition->parameter_names[i]
        );

        if (old != NULL)
        {
            saved[i].existed = 1;
            saved[i].value = ussr_value_copy(old);
        }
    }

    saved[definition->parameter_count].name =
        ussr_strdup(definition->return_name);

    return_value =
        ussr_get_variable(definition->return_name);

    if (return_value != NULL)
    {
        saved[definition->parameter_count].existed = 1;
        saved[definition->parameter_count].value =
            ussr_value_copy(return_value);
    }

    /*
     * Bind parameters.
     */
    for (i = 0; i < definition->parameter_count; ++i)
    {
        if (ussr_set_variable(
                definition->parameter_names[i],
                &values[i]) != 0)
        {
            execute_result = -1;
            goto restore;
        }
    }

    /*
     * Execute the definition body.
     */
    execute_result =
        ussr_execute_program(definition->body);

    if (execute_result == USSR_EXEC_RETURN)
    {
        return_value =
            ussr_get_variable(definition->return_name);

        if (return_value == NULL)
            result = ussr_null();
        else
            result = ussr_value_copy(return_value);

        execute_result = USSR_EXEC_OK;
        goto restore;
    }

    if (execute_result != USSR_EXEC_OK)
        goto restore;

    /*
     * Get the definition's return value.
     */
    return_value =
        ussr_get_variable(definition->return_name);

    if (return_value == NULL)
        result = ussr_null();
    else
        result = ussr_value_copy(return_value);

restore:

    /*
     * Restore the caller's map.
     */
    for (i = definition->parameter_count + 1; i > 0; --i)
    {
        size_t index = i - 1;

        if (saved[index].name == NULL)
            continue;

        if (saved[index].existed)
        {
            ussr_set_variable(
                saved[index].name,
                &saved[index].value
            );
        }
        else
        {
            /*
             * Variables created only inside the definition are
             * removed during restoration below.
             */
        }
    }

    /*
     * Remove variables created only inside the definition.
     */
    for (i = 0; i < definition->parameter_count + 1; ++i)
    {
        if (!saved[i].existed)
            ussr_remove_variable(saved[i].name);
    }

    for (i = 0; i < definition->parameter_count; ++i)
        ussr_value_free(&values[i]);

    free(values);

    for (i = 0; i < definition->parameter_count + 1; ++i)
    {
        free(saved[i].name);
        ussr_value_free(&saved[i].value);
    }

    free(saved);

    if (execute_result != USSR_EXEC_OK)
        return execute_result;

    /*
     * The definition's return value becomes the caller's
     * return variable.
     */
    if (ussr_set_variable(return_name, &result) != 0)
    {
        ussr_value_free(&result);
        return -1;
    }

    ussr_value_free(&result);

    return 0;
}

static int
ussr_command_is_definition(const ussr_command_t *command)
{
    if (command == NULL || command->argument_count == 0)
        return 0;

    if (command->arguments[
            command->argument_count - 1
        ].type != USSR_ARGUMENT_COMMAND_LIST)
        return 0;

    if (strcmp(command->name, "if") == 0 ||
        strcmp(command->name, "while") == 0 ||
        strcmp(command->name, "break") == 0 ||
        strcmp(command->name, "continue") == 0 ||
        strcmp(command->name, "return") == 0)
        return 0;

    return 1;
}

static ussr_definition_t *
ussr_find_definition(const char *name)
{
    ussr_definition_t *definition;

    if (name == NULL)
        return NULL;

    definition = definitions;

    while (definition != NULL)
    {
        if (strcmp(definition->name, name) == 0)
            return definition;

        definition = definition->next;
    }

    return NULL;
}

int
ussr_is_user_definition(const char *name)
{
    return ussr_find_definition(name) != NULL;
}

int
ussr_define_command(
    const char *name,
    const char *return_name,
    char **parameter_names,
    size_t parameter_count,
    ussr_command_list_t *body
)
{
    ussr_definition_t *definition;
    size_t i;

    if (name == NULL ||
        return_name == NULL ||
        body == NULL)
        return -1;

    if (ussr_find_definition(name) != NULL)
    {
        fprintf(
            stderr,
            "USSR: definition '%s' already exists\n",
            name
        );
        return -1;
    }

    definition = calloc(1, sizeof(*definition));

    if (definition == NULL)
        return -1;

    definition->name = ussr_strdup(name);
    definition->return_name = ussr_strdup(return_name);
    definition->parameter_count = parameter_count;
    definition->body = body;

    if (definition->name == NULL ||
        definition->return_name == NULL)
    {
        free(definition->name);
        free(definition->return_name);
        free(definition);
        return -1;
    }

    if (parameter_count != 0)
    {
        definition->parameter_names = calloc(
            parameter_count,
            sizeof(*definition->parameter_names)
        );

        if (definition->parameter_names == NULL)
        {
            free(definition->name);
            free(definition->return_name);
            free(definition);
            return -1;
        }

        for (i = 0; i < parameter_count; ++i)
        {
            definition->parameter_names[i] =
                ussr_strdup(parameter_names[i]);

            if (definition->parameter_names[i] == NULL)
            {
                while (i > 0)
                {
                    --i;
                    free(definition->parameter_names[i]);
                }

                free(definition->parameter_names);
                free(definition->name);
                free(definition->return_name);
                free(definition);
                return -1;
            }
        }
    }

    definition->next = definitions;
    definitions = definition;

    return 0;
}

void
ussr_definitions_cleanup(void)
{
    ussr_definition_t *definition;
    ussr_definition_t *next;
    size_t i;

    definition = definitions;

    while (definition != NULL)
    {
        next = definition->next;

        free(definition->name);
        free(definition->return_name);

        for (i = 0; i < definition->parameter_count; ++i)
            free(definition->parameter_names[i]);

        free(definition->parameter_names);

/*        ussr_command_list_free(definition->body); */

        free(definition);

        definition = next;
    }

    definitions = NULL;
}



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
    locals = NULL;
    local_count = 0;
    local_capacity = 0;
    map = NULL;
}

void ussr_cleanup(void)
{
    ussr_hash_entry_t *variable;
    ussr_hash_entry_t *tmp;
    size_t i;

    for (i = 0; i < local_count; ++i)
    {
        free(locals[i].name);
        ussr_value_free(&locals[i].value);
    }

    free(locals);
    locals = NULL;
    local_count = 0;
    local_capacity = 0;

    HASH_ITER(hh, map, variable, tmp)
    {
        HASH_DEL(map, variable);
        free(variable->name);
        ussr_value_free(&variable->value);
        free(variable);
    }

    map = NULL;
    ussr_definitions_cleanup();
}

ussr_value_t ussr_null(void)
{
    ussr_value_t value;

    memset(&value, 0, sizeof(value));
    value.type = USSR_NULL;

    return value;
}

ussr_value_t ussr_integer(long integer)
{
    ussr_value_t value;

    memset(&value, 0, sizeof(value));
    value.type = USSR_INTEGER;
    value.data.integer = integer;

    return value;
}

ussr_value_t ussr_real(double real)
{
    ussr_value_t value;

    memset(&value, 0, sizeof(value));
    value.type = USSR_REAL;
    value.data.real = real;

    return value;
}

ussr_value_t ussr_boolean(int boolean)
{
    ussr_value_t value;

    memset(&value, 0, sizeof(value));
    value.type = USSR_BOOLEAN;
    value.data.boolean = boolean != 0;

    return value;
}

ussr_value_t ussr_string(const char *string)
{
    ussr_value_t value;

    memset(&value, 0, sizeof(value));
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

    *value = ussr_null();
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

int ussr_set_variable(
    const char *name,
    const ussr_value_t *value
)
{
    size_t i;

    if (name == NULL || value == NULL)
        return -1;

    for (i = 0; i < local_count; ++i)
    {
        if (strcmp(locals[i].name, name) == 0)
        {
            ussr_value_t copy = ussr_value_copy(value);
            ussr_value_free(&locals[i].value);
            locals[i].value = copy;
            return 0;
        }
    }

    if (local_count == local_capacity)
    {
        size_t capacity = local_capacity == 0 ? 16 : local_capacity * 2;
        ussr_local_variable_t *new_locals;

        new_locals = realloc(locals, capacity * sizeof(*new_locals));
        if (new_locals == NULL)
            return -1;

        locals = new_locals;
        local_capacity = capacity;
    }

    locals[local_count].name = ussr_strdup(name);
    if (locals[local_count].name == NULL)
        return -1;

    locals[local_count].value = ussr_value_copy(value);
    ++local_count;

    return 0;
}

const ussr_value_t *ussr_get_variable(const char *name)
{
    size_t i;

    if (name == NULL)
        return NULL;

    for (i = 0; i < local_count; ++i)
    {
        if (strcmp(locals[i].name, name) == 0)
            return &locals[i].value;
    }

    return NULL;
}

static int ussr_remove_variable(const char *name)
{
    size_t i;

    if (name == NULL)
        return -1;

    for (i = 0; i < local_count; ++i)
    {
        if (strcmp(locals[i].name, name) == 0)
        {
            free(locals[i].name);
            ussr_value_free(&locals[i].value);

            if (i + 1 < local_count)
                memmove(
                    &locals[i],
                    &locals[i + 1],
                    (local_count - i - 1) * sizeof(*locals)
                );

            --local_count;
            return 0;
        }
    }

    return 0;
}

static int ussr_hash_set(
    const char *name,
    const ussr_value_t *value
)
{
    ussr_hash_entry_t *variable;
    unsigned key_length;

    if (name == NULL || value == NULL)
        return -1;

    key_length = (unsigned)strlen(name);

    HASH_FIND(
        hh,
        map,
        name,
        key_length,
        variable
    );

    if (variable != NULL)
    {
        ussr_value_t copy = ussr_value_copy(value);
        ussr_value_free(&variable->value);
        variable->value = copy;
        return 0;
    }

    variable = calloc(1, sizeof(*variable));
    if (variable == NULL)
        return -1;

    variable->name = ussr_strdup(name);
    if (variable->name == NULL)
    {
        free(variable);
        return -1;
    }

    variable->value = ussr_value_copy(value);

    HASH_ADD_KEYPTR(
        hh,
        map,
        variable->name,
        key_length,
        variable
    );

    return 0;
}

static const ussr_value_t *ussr_hash_get(const char *name)
{
    ussr_hash_entry_t *variable;

    if (name == NULL)
        return NULL;

    HASH_FIND(
        hh,
        map,
        name,
        (unsigned)strlen(name),
        variable
    );

    if (variable == NULL)
        return NULL;

    return &variable->value;
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

static int ussr_is_numeric(const ussr_value_t *value)
{
    return value != NULL &&
           (value->type == USSR_INTEGER ||
            value->type == USSR_REAL);
}

static double ussr_to_real(const ussr_value_t *value)
{
    if (value->type == USSR_REAL)
        return value->data.real;

    return (double)value->data.integer;
}

static int ussr_values_equal(
    const ussr_value_t *left,
    const ussr_value_t *right
)
{
    if (left == NULL || right == NULL)
        return 0;

    if (ussr_is_numeric(left) && ussr_is_numeric(right))
        return ussr_to_real(left) == ussr_to_real(right);

    if (left->type != right->type)
        return 0;

    switch (left->type)
    {
        case USSR_NULL:
            return 1;

        case USSR_INTEGER:
            return left->data.integer == right->data.integer;

        case USSR_REAL:
            return left->data.real == right->data.real;

        case USSR_BOOLEAN:
            return left->data.boolean == right->data.boolean;

        case USSR_STRING:
            return strcmp(
                left->data.string,
                right->data.string
            ) == 0;
    }

    return 0;
}

static int ussr_value_truthy(const ussr_value_t *value)
{
    if (value == NULL)
        return 0;

    switch (value->type)
    {
        case USSR_NULL:
            return 0;

        case USSR_BOOLEAN:
            return value->data.boolean != 0;

        case USSR_INTEGER:
            return value->data.integer != 0;

        case USSR_REAL:
            return value->data.real != 0.0;

        case USSR_STRING:
            return value->data.string != NULL &&
                   value->data.string[0] != '\0';
    }

    return 0;
}

static int ussr_expression_evaluate(
    const ussr_expression_t *expression,
    ussr_value_t *result
)
{
    ussr_value_t left;
    ussr_value_t right;
    const ussr_value_t *variable_value;

    if (expression == NULL || result == NULL)
        return -1;

    *result = ussr_null();

    switch (expression->type)
    {
        case USSR_EXPR_VALUE:
            *result = ussr_value_copy(&expression->data.value);
            return 0;

        case USSR_EXPR_VARIABLE:
            variable_value = ussr_get_variable(
                expression->data.variable
            );

            if (variable_value == NULL)
            {
                fprintf(
                    stderr,
                    "USSR: undefined variable '%s'\n",
                    expression->data.variable
                );
                return -1;
            }

            *result = ussr_value_copy(variable_value);
            return 0;

        case USSR_EXPR_UNARY:
            if (ussr_expression_evaluate(
                    expression->data.unary.operand,
                    &right) != 0)
                return -1;

            if (!ussr_is_numeric(&right))
            {
                fprintf(
                    stderr,
                    "USSR: unary '-' requires numeric operand\n"
                );
                ussr_value_free(&right);
                return -1;
            }

            if (right.type == USSR_REAL)
                *result = ussr_real(-right.data.real);
            else
                *result = ussr_integer(-right.data.integer);

            ussr_value_free(&right);
            return 0;

        case USSR_EXPR_BINARY:
            break;
    }

    if (ussr_expression_evaluate(
            expression->data.binary.left,
            &left) != 0)
        return -1;

    if (expression->data.binary.operator == USSR_OP_LOGICAL_AND ||
        expression->data.binary.operator == USSR_OP_LOGICAL_OR)
    {
        int left_truth = ussr_value_truthy(&left);

        if (expression->data.binary.operator == USSR_OP_LOGICAL_AND &&
            !left_truth)
        {
            *result = ussr_boolean(0);
            ussr_value_free(&left);
            return 0;
        }

        if (expression->data.binary.operator == USSR_OP_LOGICAL_OR &&
            left_truth)
        {
            *result = ussr_boolean(1);
            ussr_value_free(&left);
            return 0;
        }
    }

    if (ussr_expression_evaluate(
            expression->data.binary.right,
            &right) != 0)
    {
        ussr_value_free(&left);
        return -1;
    }

    switch (expression->data.binary.operator)
    {
        case USSR_OP_ADD:
        case USSR_OP_SUB:
        case USSR_OP_MUL:
            if (!ussr_is_numeric(&left) ||
                !ussr_is_numeric(&right))
            {
                fprintf(
                    stderr,
                    "USSR: arithmetic requires numeric operands\n"
                );
                ussr_value_free(&left);
                ussr_value_free(&right);
                return -1;
            }

            if (left.type == USSR_INTEGER &&
                right.type == USSR_INTEGER)
            {
                long a = left.data.integer;
                long b = right.data.integer;

                if (expression->data.binary.operator == USSR_OP_ADD)
                    *result = ussr_integer(a + b);
                else if (expression->data.binary.operator == USSR_OP_SUB)
                    *result = ussr_integer(a - b);
                else
                    *result = ussr_integer(a * b);
            }
            else
            {
                double a = ussr_to_real(&left);
                double b = ussr_to_real(&right);

                if (expression->data.binary.operator == USSR_OP_ADD)
                    *result = ussr_real(a + b);
                else if (expression->data.binary.operator == USSR_OP_SUB)
                    *result = ussr_real(a - b);
                else
                    *result = ussr_real(a * b);
            }
            break;

        case USSR_OP_DIV:
            if (!ussr_is_numeric(&left) ||
                !ussr_is_numeric(&right))
            {
                fprintf(
                    stderr,
                    "USSR: division requires numeric operands\n"
                );
                ussr_value_free(&left);
                ussr_value_free(&right);
                return -1;
            }

            if ((right.type == USSR_INTEGER &&
                 right.data.integer == 0) ||
                (right.type == USSR_REAL &&
                 right.data.real == 0.0))
            {
                fprintf(stderr, "USSR: division by zero\n");
                ussr_value_free(&left);
                ussr_value_free(&right);
                return -1;
            }

            if (left.type == USSR_INTEGER &&
                right.type == USSR_INTEGER)
            {
                *result = ussr_integer(
                    left.data.integer / right.data.integer
                );
            }
            else
            {
                *result = ussr_real(
                    ussr_to_real(&left) /
                    ussr_to_real(&right)
                );
            }
            break;

        case USSR_OP_MOD:
            if (left.type != USSR_INTEGER ||
                right.type != USSR_INTEGER)
            {
                fprintf(
                    stderr,
                    "USSR: modulo requires integer operands\n"
                );
                ussr_value_free(&left);
                ussr_value_free(&right);
                return -1;
            }

            if (right.data.integer == 0)
            {
                fprintf(stderr, "USSR: modulo by zero\n");
                ussr_value_free(&left);
                ussr_value_free(&right);
                return -1;
            }

            *result = ussr_integer(
                left.data.integer % right.data.integer
            );
            break;

        case USSR_OP_LOGICAL_AND:
            *result = ussr_boolean(
                ussr_value_truthy(&left) &&
                ussr_value_truthy(&right)
            );
            break;

        case USSR_OP_LOGICAL_OR:
            *result = ussr_boolean(
                ussr_value_truthy(&left) ||
                ussr_value_truthy(&right)
            );
            break;

        case USSR_OP_SHIFT_LEFT:
        case USSR_OP_SHIFT_RIGHT:
            if (left.type != USSR_INTEGER ||
                right.type != USSR_INTEGER ||
                right.data.integer < 0 ||
                (unsigned long)right.data.integer >=
                    sizeof(unsigned long) * 8UL)
            {
                fprintf(
                    stderr,
                    "USSR: shift requires integer operands and a valid shift count\n"
                );
                ussr_value_free(&left);
                ussr_value_free(&right);
                return -1;
            }

            if (expression->data.binary.operator == USSR_OP_SHIFT_LEFT)
            {
                *result = ussr_integer(
                    (long)((unsigned long)left.data.integer <<
                           (unsigned long)right.data.integer)
                );
            }
            else
            {
                *result = ussr_integer(
                    (long)((unsigned long)left.data.integer >>
                           (unsigned long)right.data.integer)
                );
            }
            break;

        case USSR_OP_BITWISE_XOR:
        case USSR_OP_BITWISE_AND:
        case USSR_OP_BITWISE_OR:
            if (left.type != USSR_INTEGER ||
                right.type != USSR_INTEGER)
            {
                fprintf(
                    stderr,
                    "USSR: bitwise operators require integer operands\n"
                );
                ussr_value_free(&left);
                ussr_value_free(&right);
                return -1;
            }

            if (expression->data.binary.operator == USSR_OP_BITWISE_XOR)
                *result = ussr_integer(
                    left.data.integer ^ right.data.integer
                );
            else if (expression->data.binary.operator == USSR_OP_BITWISE_AND)
                *result = ussr_integer(
                    left.data.integer & right.data.integer
                );
            else
                *result = ussr_integer(
                    left.data.integer | right.data.integer
                );
            break;

        case USSR_OP_GT:
        case USSR_OP_LT:
        case USSR_OP_GE:
        case USSR_OP_LE:
            if (!ussr_is_numeric(&left) ||
                !ussr_is_numeric(&right))
            {
                fprintf(
                    stderr,
                    "USSR: ordered comparison requires numeric operands\n"
                );
                ussr_value_free(&left);
                ussr_value_free(&right);
                return -1;
            }

            {
                double a = ussr_to_real(&left);
                double b = ussr_to_real(&right);
                int value = 0;

                switch (expression->data.binary.operator)
                {
                    case USSR_OP_GT:
                        value = a > b;
                        break;
                    case USSR_OP_LT:
                        value = a < b;
                        break;
                    case USSR_OP_GE:
                        value = a >= b;
                        break;
                    case USSR_OP_LE:
                        value = a <= b;
                        break;
                    default:
                        break;
                }

                *result = ussr_boolean(value);
            }
            break;

        case USSR_OP_EQ:
            *result = ussr_boolean(
                ussr_values_equal(&left, &right)
            );
            break;

        case USSR_OP_NE:
            *result = ussr_boolean(
                !ussr_values_equal(&left, &right)
            );
            break;
    }

    ussr_value_free(&left);
    ussr_value_free(&right);

    return 0;
}

static int ussr_argument_evaluate(
    const ussr_argument_t *argument,
    ussr_value_t *result
)
{
    const char *key;
    const ussr_value_t *value;

    if (argument == NULL || result == NULL)
        return -1;

    *result = ussr_null();

    if (argument->type == USSR_ARGUMENT_VALUE)
    {
        *result = ussr_value_copy(&argument->data.value);
        return 0;
    }

    if (argument->type == USSR_ARGUMENT_EXPRESSION)
        return ussr_expression_evaluate(
            argument->data.expression,
            result
        );

    if (argument->type == USSR_ARGUMENT_LOOKUP)
    {
        if (argument->data.expression == NULL ||
            argument->data.expression->type != USSR_EXPR_VARIABLE)
        {
            fprintf(stderr, "USSR: hash lookup requires a key identifier\n");
            return -1;
        }

        key = argument->data.expression->data.variable;
        value = ussr_hash_get(key);

        if (value == NULL)
        {
            fprintf(stderr, "USSR: undefined hash key '%s'\n", key);
            return -1;
        }

        *result = ussr_value_copy(value);
        return 0;
    }

    fprintf(
        stderr,
        "USSR: command block cannot be used as a value\n"
    );
    return -1;
}

static int ussr_execute_list(
    ussr_command_list_t *list
)
{
    ussr_command_t *command;

    if (list == NULL)
        return 0;

    command = list->head;

    while (command != NULL)
    {
        if (ussr_command_is_definition(command))
        {
            ussr_argument_t *body_argument;
            char **parameter_names;
            size_t parameter_count;
            size_t i;
            int result;

            body_argument =
                &command->arguments[
                    command->argument_count - 1
                ];

            parameter_count =
                command->argument_count - 1;

            parameter_names = calloc(
                parameter_count,
                sizeof(*parameter_names)
            );

            if (parameter_names == NULL &&
                parameter_count != 0)
                return -1;

            for (i = 0; i < parameter_count; ++i)
            {
                if (command->arguments[i].type !=
                    USSR_ARGUMENT_EXPRESSION ||
                    command->arguments[i].data.expression == NULL ||
                    command->arguments[i].data.expression->type !=
                    USSR_EXPR_VARIABLE)
                {
                    fprintf(
                        stderr,
                        "USSR: definition '%s' "
                        "parameters must be identifiers\n",
                        command->name
                    );

                    free(parameter_names);
                    return -1;
                }

                parameter_names[i] =
                    command->arguments[i]
                        .data.expression
                        ->data.variable;
            }

            result = ussr_define_command(
                command->name,
                command->return_name,
                parameter_names,
                parameter_count,
                body_argument->data.command_list
            );

            free(parameter_names);

            if (result != 0)
                return -1;

            /*
             * Ownership of the body has now moved into
             * the definition. Don't execute it.
             */
        }
        else
        {
            {
                int execute_result;

                execute_result = ussr_execute_command(
                    command->name,
                    command->return_name,
                    command->arguments,
                    command->argument_count
                );

                if (execute_result != USSR_EXEC_OK)
                    return execute_result;
            }
        }

        command = command->next;
    }

    return 0;
}

static int ussr_execute_if(
    const char *return_name,
    ussr_argument_t *arguments,
    size_t argument_count
)
{
    ussr_value_t condition;
    ussr_value_t result;

    if (argument_count < 2 || argument_count > 3)
    {
        fprintf(
            stderr,
            "USSR: if expects condition and then block, "
            "optionally an else block\n"
        );
        return -1;
    }

    if (ussr_argument_evaluate(&arguments[0], &condition) != 0)
        return -1;

    if (condition.type == USSR_BOOLEAN)
    {
        /* already boolean */
    }

    if (condition.type == USSR_NULL ||
        condition.type == USSR_BOOLEAN ||
        condition.type == USSR_INTEGER ||
        condition.type == USSR_REAL ||
        condition.type == USSR_STRING)
    {
        int true_condition = ussr_value_truthy(&condition);

        ussr_value_free(&condition);

        if (true_condition)
        {
            if (arguments[1].type != USSR_ARGUMENT_COMMAND_LIST)
            {
                fprintf(stderr, "USSR: if then argument must be a block\n");
                return -1;
            }

            {
                int execute_result = ussr_execute_list(
                    arguments[1].data.command_list
                );

                if (execute_result != USSR_EXEC_OK)
                    return execute_result;
            }
        }
        else if (argument_count == 3)
        {
            if (arguments[2].type != USSR_ARGUMENT_COMMAND_LIST)
            {
                fprintf(stderr, "USSR: if else argument must be a block\n");
                return -1;
            }

            {
                int execute_result = ussr_execute_list(
                    arguments[2].data.command_list
                );

                if (execute_result != USSR_EXEC_OK)
                    return execute_result;
            }
        }

        result = ussr_boolean(true_condition);

        if (return_name != NULL)
        {
            if (ussr_set_variable(return_name, &result) != 0)
                return -1;
        }

        return 0;
    }

    ussr_value_free(&condition);
    return -1;
}

static int ussr_execute_while(
    const char *return_name,
    ussr_argument_t *arguments,
    size_t argument_count
)
{
    unsigned long iterations = 0;
    ussr_value_t condition;
    ussr_value_t result;

    if (argument_count != 2)
    {
        fprintf(
            stderr,
            "USSR: while expects condition and block\n"
        );
        return -1;
    }

    if (arguments[1].type != USSR_ARGUMENT_COMMAND_LIST)
    {
        fprintf(stderr, "USSR: while block is required\n");
        return -1;
    }

    while (1)
    {
        if (ussr_argument_evaluate(
                &arguments[0],
                &condition) != 0)
            return -1;

        if (!ussr_value_truthy(&condition))
        {
            ussr_value_free(&condition);
            break;
        }

        ussr_value_free(&condition);

        if (++iterations > USSR_MAX_LOOP_ITERATIONS)
        {
            fprintf(
                stderr,
                "USSR: while loop exceeded maximum iterations\n"
            );
            return -1;
        }

        {
            int execute_result = ussr_execute_list(
                arguments[1].data.command_list
            );

            if (execute_result == USSR_EXEC_BREAK)
                break;

            if (execute_result == USSR_EXEC_CONTINUE)
                continue;

            if (execute_result != USSR_EXEC_OK)
                return execute_result;
        }
    }

    result = ussr_boolean(1);

    if (return_name != NULL)
    {
        if (ussr_set_variable(return_name, &result) != 0)
            return -1;
    }

    return 0;
}

static int
ussr_command_is_executable(const char *path)
{
    if (path == NULL)
        return 0;

    return access(path, X_OK) == 0;
}

static char *
ussr_find_external_command(const char *command)
{
    const char *path;
    const char *start;
    const char *end;
    size_t directory_length;
    size_t command_length;
    size_t length;
    char *candidate;

    if (command == NULL || *command == '\0')
        return NULL;

    /*
     * A command containing '/' is already a path.
     */
    if (strchr(command, '/') != NULL)
    {
        if (ussr_command_is_executable(command))
            return ussr_strdup(command);

        return NULL;
    }

    /*
     * Search PATH first.
     */
    path = getenv("PATH");

    if (path != NULL)
    {
        start = path;
        command_length = strlen(command);

        while (*start != '\0')
        {
            end = strchr(start, ':');

            if (end == NULL)
                end = start + strlen(start);

            directory_length = (size_t)(end - start);

            /*
             * Empty PATH component means current directory.
             */
            if (directory_length == 0)
            {
                length = 2 + command_length;

                candidate = malloc(length);

                if (candidate == NULL)
                    return NULL;

                snprintf(
                    candidate,
                    length,
                    "./%s",
                    command
                );
            }
            else
            {
                length =
                    directory_length +
                    1 +
                    command_length +
                    1;

                candidate = malloc(length);

                if (candidate == NULL)
                    return NULL;

                snprintf(
                    candidate,
                    length,
                    "%.*s/%s",
                    (int)directory_length,
                    start,
                    command
                );
            }

            if (ussr_command_is_executable(candidate))
                return candidate;

            free(candidate);

            if (*end == '\0')
                break;

            start = end + 1;
        }
    }

    /*
     * Finally check the current working directory explicitly.
     */
    length = 2 + strlen(command);

    candidate = malloc(length);

    if (candidate == NULL)
        return NULL;

    snprintf(candidate, length, "./%s", command);

    if (ussr_command_is_executable(candidate))
        return candidate;

    free(candidate);

    return NULL;
}

static int
ussr_execute_external(
    const char *command,
    const char *return_name,
    ussr_argument_t *arguments,
    size_t argument_count
)
{
    char *path;
    char **argv;
    ussr_value_t *values;
    size_t i;
    pid_t pid;
    int status;
    ussr_value_t result;

    path = ussr_find_external_command(command);

    if (path == NULL)
    {
        fprintf(
            stderr,
            "USSR: command not found: %s\n",
            command
        );
        return -1;
    }

    argv = calloc(argument_count + 2, sizeof(*argv));

    if (argv == NULL)
    {
        free(path);
        return -1;
    }

    values = calloc(argument_count, sizeof(*values));

    if (values == NULL)
    {
        free(argv);
        free(path);
        return -1;
    }

    argv[0] = path;

    for (i = 0; i < argument_count; ++i)
    {
        if (ussr_argument_evaluate(
                &arguments[i],
                &values[i]) != 0)
        {
            size_t j;

            for (j = 0; j < i; ++j)
                ussr_value_free(&values[j]);

            free(values);
            free(argv);
            free(path);

            return -1;
        }

        if (values[i].type != USSR_STRING)
        {
            /*
             * Convert numeric/boolean/null values to their
             * textual representation for exec().
             */
            char buffer[64];

            switch (values[i].type)
            {
                case USSR_INTEGER:
                    snprintf(
                        buffer,
                        sizeof(buffer),
                        "%ld",
                        values[i].data.integer
                    );
                    break;

                case USSR_REAL:
                    snprintf(
                        buffer,
                        sizeof(buffer),
                        "%.17g",
                        values[i].data.real
                    );
                    break;

                case USSR_BOOLEAN:
                    snprintf(
                        buffer,
                        sizeof(buffer),
                        "%s",
                        values[i].data.boolean ?
                            "true" : "false"
                    );
                    break;

                case USSR_NULL:
                    snprintf(
                        buffer,
                        sizeof(buffer),
                        "null"
                    );
                    break;

                default:
                    buffer[0] = '\0';
                    break;
            }

            argv[i + 1] = ussr_strdup(buffer);
        }
        else
        {
            argv[i + 1] = ussr_strdup(values[i].data.string);
        }

        if (argv[i + 1] == NULL)
        {
            size_t j;

            for (j = 0; j <= i; ++j)
            {
                free(argv[j + 1]);
                ussr_value_free(&values[j]);
            }

            free(values);
            free(argv);
            free(path);

            return -1;
        }
    }

    argv[argument_count + 1] = NULL;

    pid = fork();

    if (pid < 0)
    {
        perror("USSR: fork");

        for (i = 0; i < argument_count; ++i)
        {
            free(argv[i + 1]);
            ussr_value_free(&values[i]);
        }

        free(values);
        free(argv);
        free(path);

        return -1;
    }

    if (pid == 0)
    {
        execv(path, argv);

        /*
         * Only reached when execv() fails.
         */
        fprintf(
            stderr,
            "USSR: cannot execute '%s': %s\n",
            path,
            strerror(errno)
        );

        _exit(126);
    }

    do
    {
        if (waitpid(pid, &status, 0) < 0)
        {
            if (errno == EINTR)
                continue;

            perror("USSR: waitpid");

            for (i = 0; i < argument_count; ++i)
            {
                free(argv[i + 1]);
                ussr_value_free(&values[i]);
            }

            free(values);
            free(argv);
            free(path);

            return -1;
        }

        break;
    }
    while (1);

    for (i = 0; i < argument_count; ++i)
    {
        free(argv[i + 1]);
        ussr_value_free(&values[i]);
    }

    free(values);
    free(argv);
    free(path);

    /*
     * Return the normal process exit status.
     *
     * If the process was killed by a signal, use 128 + signal
     * in the same general convention used by shells.
     */
    if (WIFEXITED(status))
    {
        result = ussr_integer((long)WEXITSTATUS(status));
    }
    else if (WIFSIGNALED(status))
    {
        result = ussr_integer(
            128L + (long)WTERMSIG(status)
        );
    }
    else
    {
        result = ussr_integer(-1);
    }

    if (ussr_set_variable(return_name, &result) != 0)
    {
        ussr_value_free(&result);
        return -1;
    }

    ussr_value_free(&result);

    return 0;
}


static int
ussr_execute_eval(
    const char *return_name,
    ussr_argument_t *arguments,
    size_t argument_count
)
{
    ussr_value_t source;
    ussr_value_t result;
    ussr_command_list_t *program;
    YY_BUFFER_STATE buffer;
    int parse_result;

    if (return_name == NULL)
        return -1;

    if (argument_count != 1)
    {
        fprintf(
            stderr,
            "USSR: eval expects 1 parameter\n"
        );
        return -1;
    }

    if (ussr_argument_evaluate(
            &arguments[0],
            &source) != 0)
        return -1;

    if (source.type != USSR_STRING)
    {
        fprintf(
            stderr,
            "USSR: eval requires a string parameter\n"
        );

        ussr_value_free(&source);
        return -1;
    }

    /*
     * Save the current parsed program because yyparse()
     * writes to the global parser result.
     */
    program = ussr_parsed_program;
    ussr_parsed_program = NULL;

    buffer = yy_scan_string(source.data.string);

    if (buffer == NULL)
    {
        ussr_parsed_program = program;
        ussr_value_free(&source);

        fprintf(
            stderr,
            "USSR: eval could not create parser buffer\n"
        );

        return -1;
    }

    parse_result = yyparse();

    yy_delete_buffer(buffer);

    ussr_value_free(&source);

    if (parse_result != 0 ||
        ussr_parsed_program == NULL)
    {
        ussr_parsed_program = program;

        fprintf(
            stderr,
            "USSR: eval syntax error\n"
        );

        return -1;
    }

    /*
     * Execute the dynamically parsed program.
     */
    if (ussr_execute_program(ussr_parsed_program) != 0)
    {
        ussr_command_list_free(ussr_parsed_program);
        ussr_parsed_program = program;

        return -1;
    }

    /*
     * eval returns the value of its return variable.
     */
    {
        const ussr_value_t *value;

        value = ussr_get_variable(return_name);

        if (value == NULL)
        {
            result = ussr_null();
        }
        else
        {
            result = ussr_value_copy(value);
        }
    }

    ussr_command_list_free(ussr_parsed_program);
    ussr_parsed_program = program;

    if (ussr_set_variable(return_name, &result) != 0)
    {
        ussr_value_free(&result);
        return -1;
    }

    ussr_value_free(&result);

    return 0;
}


int ussr_execute_command(
    const char *command,
    const char *return_name,
    ussr_argument_t *arguments,
    size_t argument_count
)
{
    ussr_value_t result;
    ussr_value_t assignment_value;
    int assignment_index = -1;
    size_t i;

    if (command == NULL)
        return -1;

    if (strcmp(command, "if") == 0)
        return ussr_execute_if(
            return_name,
            arguments,
            argument_count
        );

    if (strcmp(command, "while") == 0)
        return ussr_execute_while(
            return_name,
            arguments,
            argument_count
        );

    if (strcmp(command, "break") == 0 ||
        strcmp(command, "continue") == 0 ||
        strcmp(command, "return") == 0)
    {
        int status;

        if (return_name == NULL || argument_count != 1 ||
            arguments[0].type == USSR_ARGUMENT_COMMAND_LIST)
        {
            fprintf(
                stderr,
                "USSR: %s expects 1 value parameter\n",
                command
            );
            return USSR_EXEC_ERROR;
        }

        if (ussr_argument_evaluate(
                &arguments[0], &result) != 0)
            return USSR_EXEC_ERROR;

        if (ussr_set_variable(return_name, &result) != 0)
        {
            ussr_value_free(&result);
            return USSR_EXEC_ERROR;
        }

        if (arguments[0].assignment)
        {
            if (ussr_hash_set(return_name, &result) != 0)
            {
                ussr_value_free(&result);
                return USSR_EXEC_ERROR;
            }
        }

        if (strcmp(command, "break") == 0)
            status = USSR_EXEC_BREAK;
        else if (strcmp(command, "continue") == 0)
            status = USSR_EXEC_CONTINUE;
        else
            status = USSR_EXEC_RETURN;

        ussr_value_free(&result);

        return status;
    }

    if (return_name == NULL)
        return -1;

    /*
     * A ! marker makes the marked argument an explicit hash write.
     * The command executes normally; after it succeeds, the marked
     * argument's value is stored under return_name.
     */
    for (i = 0; i < argument_count; ++i)
    {
        if (arguments[i].assignment)
        {
            if (assignment_index >= 0)
            {
                fprintf(
                    stderr,
                    "USSR: a command may have only one ! assignment\n"
                );
                return -1;
            }

            assignment_index = (int)i;
        }
    }

    result = ussr_null();
	
    if (strcmp(command, "eval") == 0)
    {
        int execute_result;

        execute_result = ussr_execute_eval(
            return_name,
            arguments,
            argument_count
        );

        if (execute_result != 0)
            return execute_result;

        if (assignment_index >= 0)
        {
            if (ussr_argument_evaluate(
                    &arguments[assignment_index],
                    &assignment_value) != 0)
                return -1;

            if (ussr_hash_set(
                    return_name,
                    &assignment_value) != 0)
            {
                ussr_value_free(&assignment_value);
                return -1;
            }

            ussr_value_free(&assignment_value);
        }

        return 0;
    }
    if (strcmp(command, "set") == 0)
    {
        if (argument_count != 1)
        {
            fprintf(stderr, "USSR: set expects 1 parameter\n");
            return -1;
        }

        if (ussr_argument_evaluate(
                &arguments[0],
                &result) != 0)
            return -1;
    }
    else if (strcmp(command, "print") == 0)
    {
        if (argument_count != 1)
        {
            fprintf(stderr, "USSR: print expects 1 parameter\n");
            return -1;
        }

        if (ussr_argument_evaluate(
                &arguments[0],
                &result) != 0)
            return -1;

        ussr_print_value(&result);
    }
    else if (strcmp(command, "add") == 0)
    {
        ussr_value_t a;
        ussr_value_t b;

        if (argument_count != 2)
        {
            fprintf(stderr, "USSR: add expects 2 parameters\n");
            return -1;
        }

        if (ussr_argument_evaluate(
                &arguments[0], &a) != 0)
            return -1;

        if (ussr_argument_evaluate(
                &arguments[1], &b) != 0)
        {
            ussr_value_free(&a);
            return -1;
        }

        if (!ussr_is_numeric(&a) ||
            !ussr_is_numeric(&b))
        {
            fprintf(
                stderr,
                "USSR: add requires numeric parameters\n"
            );
            ussr_value_free(&a);
            ussr_value_free(&b);
            return -1;
        }

        if (a.type == USSR_INTEGER &&
            b.type == USSR_INTEGER)
        {
            result = ussr_integer(
                a.data.integer + b.data.integer
            );
        }
        else
        {
            result = ussr_real(
                ussr_to_real(&a) +
                ussr_to_real(&b)
            );
        }

        ussr_value_free(&a);
        ussr_value_free(&b);
    }
	else if (strcmp(command, "concat") == 0)
    {
        ussr_value_t *values;
        size_t total_length = 0;
        char *string;
        char *cursor;

        if (argument_count < 2)
        {
            fprintf(
                stderr,
                "USSR: concat expects at least 2 parameters\n"
            );
            return -1;
        }

        values = calloc(argument_count, sizeof(*values));

        if (values == NULL)
            return -1;

        for (i = 0; i < argument_count; ++i)
        {
            if (ussr_argument_evaluate(
                    &arguments[i],
                    &values[i]) != 0)
            {
                size_t j;

                for (j = 0; j < i; ++j)
                    ussr_value_free(&values[j]);

                free(values);
                return -1;
            }

            if (values[i].type != USSR_STRING)
            {
                fprintf(
                    stderr,
                    "USSR: concat requires string parameters\n"
                );

                for (size_t j = 0; j <= i; ++j)
                    ussr_value_free(&values[j]);

                free(values);
                return -1;
            }

            if (total_length >
                SIZE_MAX - strlen(values[i].data.string) - 1)
            {
                fprintf(
                    stderr,
                    "USSR: concat result is too large\n"
                );

                for (size_t j = 0; j <= i; ++j)
                    ussr_value_free(&values[j]);

                free(values);
                return -1;
            }

            total_length += strlen(values[i].data.string);
        }

        string = malloc(total_length + 1);

        if (string == NULL)
        {
            for (i = 0; i < argument_count; ++i)
                ussr_value_free(&values[i]);

            free(values);
            return -1;
        }

        cursor = string;

        for (i = 0; i < argument_count; ++i)
        {
            size_t length = strlen(values[i].data.string);

            memcpy(cursor, values[i].data.string, length);
            cursor += length;
            ussr_value_free(&values[i]);
        }

        *cursor = '\0';
        free(values);

        result.type = USSR_STRING;
        result.data.string = string;
    }
    else if (ussr_is_user_definition(command))
    {
        int execute_result;

        execute_result = ussr_execute_user_definition(
            command,
            return_name,
            arguments,
            argument_count
        );

        if (execute_result != 0)
        {
            ussr_value_free(&assignment_value);
            return execute_result;
        }

        if (assignment_index >= 0)
        {
            if (ussr_argument_evaluate(
                    &arguments[assignment_index],
                    &assignment_value) != 0)
                return -1;

            if (ussr_hash_set(
                    return_name,
                    &assignment_value) != 0)
            {
                ussr_value_free(&assignment_value);
                return -1;
            }

            ussr_value_free(&assignment_value);
        }

        return 0;
    }
    else
    {
        int execute_result;

        execute_result = ussr_execute_external(
            command,
            return_name,
            arguments,
            argument_count
        );

        if (execute_result != 0)
        {
            ussr_value_free(&assignment_value);
            return execute_result;
        }

        if (assignment_index >= 0)
        {
            if (ussr_argument_evaluate(
                    &arguments[assignment_index],
                    &assignment_value) != 0)
                return -1;

            if (ussr_hash_set(
                    return_name,
                    &assignment_value) != 0)
            {
                ussr_value_free(&assignment_value);
                return -1;
            }

            ussr_value_free(&assignment_value);
        }

        return 0;
    }

    if (ussr_set_variable(return_name, &result) != 0)
    {
        ussr_value_free(&result);
        return -1;
    }

    ussr_value_free(&result);

    if (assignment_index >= 0)
    {
        if (ussr_argument_evaluate(
                &arguments[assignment_index],
                &assignment_value) != 0)
            return -1;

        if (ussr_hash_set(return_name, &assignment_value) != 0)
        {
            ussr_value_free(&assignment_value);
            return -1;
        }

        ussr_value_free(&assignment_value);
    }

    /*
     * Arguments belong to the command tree and are freed when
     * the complete command tree is destroyed. Do not free them here.
     */
    (void)i;

    return 0;
}

ussr_command_list_t *
ussr_command_list_create(void)
{
    ussr_command_list_t *list;

    list = calloc(1, sizeof(*list));

    return list;
}

ussr_command_t *
ussr_command_create(
    char *name,
    char *return_name,
    ussr_argument_t *arguments,
    size_t argument_count
)
{
    ussr_command_t *command;

    command = calloc(1, sizeof(*command));

    if (command == NULL)
        return NULL;

    command->name = name;
    command->return_name = return_name;
    command->arguments = arguments;
    command->argument_count = argument_count;

    return command;
}

int ussr_command_list_append(
    ussr_command_list_t *list,
    ussr_command_t *command
)
{
    if (list == NULL || command == NULL)
        return -1;

    command->next = NULL;

    if (list->tail == NULL)
    {
        list->head = command;
        list->tail = command;
    }
    else
    {
        list->tail->next = command;
        list->tail = command;
    }

    return 0;
}

void ussr_expression_free(ussr_expression_t *expression)
{
    if (expression == NULL)
        return;

    switch (expression->type)
    {
        case USSR_EXPR_VALUE:
            ussr_value_free(&expression->data.value);
            break;

        case USSR_EXPR_VARIABLE:
            free(expression->data.variable);
            break;

        case USSR_EXPR_BINARY:
            ussr_expression_free(
                expression->data.binary.left
            );
            ussr_expression_free(
                expression->data.binary.right
            );
            break;

        case USSR_EXPR_UNARY:
            ussr_expression_free(
                expression->data.unary.operand
            );
            break;
    }

    free(expression);
}

static void ussr_argument_free(ussr_argument_t *argument)
{
    if (argument == NULL)
        return;

    if (argument->type == USSR_ARGUMENT_VALUE)
    {
        ussr_value_free(&argument->data.value);
    }
    else if (argument->type == USSR_ARGUMENT_EXPRESSION ||
             argument->type == USSR_ARGUMENT_LOOKUP)
    {
        ussr_expression_free(argument->data.expression);
    }
    else if (argument->type == USSR_ARGUMENT_COMMAND_LIST)
    {
        ussr_command_list_free(argument->data.command_list);
    }

    argument->type = USSR_ARGUMENT_VALUE;
    argument->data.value = ussr_null();
}

void ussr_command_list_free(ussr_command_list_t *list)
{
    ussr_command_t *command;
    ussr_command_t *next;
    size_t i;

    if (list == NULL)
        return;

    command = list->head;

    while (command != NULL)
    {
        next = command->next;

        free(command->name);
        free(command->return_name);

        for (i = 0; i < command->argument_count; ++i)
            ussr_argument_free(&command->arguments[i]);

        free(command->arguments);
        free(command);

        command = next;
    }

    free(list);
}

int ussr_execute_program(ussr_command_list_t *program)
{
    return ussr_execute_list(program);
}
