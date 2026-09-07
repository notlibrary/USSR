#include "ussr.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#define USSR_INITIAL_VARIABLES 32
#define USSR_MAX_LOOP_ITERATIONS 1000000UL

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
    if (argument == NULL || result == NULL)
        return -1;

    *result = ussr_null();

    if (argument->type == USSR_ARGUMENT_VALUE)
        *result = ussr_value_copy(&argument->data.value);
    else if (argument->type == USSR_ARGUMENT_EXPRESSION)
        return ussr_expression_evaluate(
            argument->data.expression,
            result
        );
    else
    {
        fprintf(
            stderr,
            "USSR: command block cannot be used as a value\n"
        );
        return -1;
    }

    return 0;
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
        if (ussr_execute_command(
                command->name,
                command->return_name,
                command->arguments,
                command->argument_count) != 0)
            return -1;

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

            if (ussr_execute_list(
                    arguments[1].data.command_list) != 0)
                return -1;
        }
        else if (argument_count == 3)
        {
            if (arguments[2].type != USSR_ARGUMENT_COMMAND_LIST)
            {
                fprintf(stderr, "USSR: if else argument must be a block\n");
                return -1;
            }

            if (ussr_execute_list(
                    arguments[2].data.command_list) != 0)
                return -1;
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

        if (ussr_execute_list(
                arguments[1].data.command_list) != 0)
            return -1;
    }

    result = ussr_boolean(1);

    if (return_name != NULL)
    {
        if (ussr_set_variable(return_name, &result) != 0)
            return -1;
    }

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

    if (return_name == NULL)
        return -1;

    result = ussr_null();

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
    else
    {
        fprintf(
            stderr,
            "USSR: unknown command '%s'\n",
            command
        );
        return -1;
    }

    if (ussr_set_variable(return_name, &result) != 0)
    {
        ussr_value_free(&result);
        return -1;
    }

    ussr_value_free(&result);

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
    else if (argument->type == USSR_ARGUMENT_EXPRESSION)
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
