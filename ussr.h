#ifndef USSR_H
#define USSR_H

#include <stddef.h>
typedef struct ussr_command_list_t ussr_command_list_t;

typedef struct ussr_definition_t
{
    char *name;
    char *return_name;

    char **parameter_names;
    size_t parameter_count;

    ussr_command_list_t *body;

    struct ussr_definition_t *next;
} ussr_definition_t;

typedef enum
{
    USSR_NULL,
    USSR_INTEGER,
    USSR_REAL,
    USSR_STRING,
    USSR_BOOLEAN
} ussr_value_type_t;

typedef struct
{
    ussr_value_type_t type;

    union
    {
        long integer;
        double real;
        char *string;
        int boolean;
    } data;
} ussr_value_t;

typedef struct ussr_variable_t
{
    char *name;
    ussr_value_t value;
} ussr_variable_t;

typedef enum
{
    USSR_EXPR_VALUE,
    USSR_EXPR_VARIABLE,
    USSR_EXPR_BINARY,
    USSR_EXPR_UNARY
} ussr_expression_type_t;

typedef enum
{
    USSR_OP_ADD,
    USSR_OP_SUB,
    USSR_OP_MUL,
    USSR_OP_DIV,
    USSR_OP_MOD,
    USSR_OP_GT,
    USSR_OP_LT,
    USSR_OP_GE,
    USSR_OP_LE,
    USSR_OP_EQ,
    USSR_OP_NE
} ussr_operator_t;

typedef struct ussr_expression_t
{
    ussr_expression_type_t type;

    union
    {
        ussr_value_t value;
        char *variable;

        struct
        {
            struct ussr_expression_t *left;
            ussr_operator_t operator;
            struct ussr_expression_t *right;
        } binary;

        struct
        {
            ussr_operator_t operator;
            struct ussr_expression_t *operand;
        } unary;
    } data;
} ussr_expression_t;

typedef enum
{
    USSR_ARGUMENT_VALUE,
    USSR_ARGUMENT_EXPRESSION,
    USSR_ARGUMENT_COMMAND_LIST,
    USSR_ARGUMENT_LOOKUP
} ussr_argument_type_t;

typedef struct
{
    ussr_argument_type_t type;
    int assignment;

    union
    {
        ussr_value_t value;
        ussr_expression_t *expression;
        ussr_command_list_t *command_list;
    } data;
} ussr_argument_t;

typedef struct ussr_saved_variable_t
{
    char *name;
    ussr_value_t value;
    int existed;
} ussr_saved_variable_t;

typedef struct ussr_command_t
{
    char *name;
    char *return_name;

    ussr_argument_t *arguments;
    size_t argument_count;

    struct ussr_command_t *next;
} ussr_command_t;

struct ussr_command_list_t
{
    ussr_command_t *head;
    ussr_command_t *tail;
};

int ussr_define_command(
    const char *name,
    const char *return_name,
    char **parameter_names,
    size_t parameter_count,
    ussr_command_list_t *body
);

int ussr_is_user_definition(const char *name);

int ussr_execute_user_definition(
    const char *name,
    const char *return_name,
    ussr_argument_t *arguments,
    size_t argument_count
);

void ussr_definitions_cleanup(void);

void ussr_init(void);
void ussr_cleanup(void);

ussr_value_t ussr_null(void);
ussr_value_t ussr_integer(long value);
ussr_value_t ussr_real(double value);
ussr_value_t ussr_boolean(int value);
ussr_value_t ussr_string(const char *value);

void ussr_value_free(ussr_value_t *value);
ussr_value_t ussr_value_copy(const ussr_value_t *value);

int ussr_set_variable(
    const char *name,
    const ussr_value_t *value
);

const ussr_value_t *ussr_get_variable(const char *name);

void ussr_print_value(const ussr_value_t *value);

int ussr_execute_command(
    const char *command,
    const char *return_name,
    ussr_argument_t *arguments,
    size_t argument_count
);

int ussr_execute_program(ussr_command_list_t *program);

ussr_command_list_t *ussr_command_list_create(void);
ussr_command_t *ussr_command_create(
    char *name,
    char *return_name,
    ussr_argument_t *arguments,
    size_t argument_count
);
int ussr_command_list_append(
    ussr_command_list_t *list,
    ussr_command_t *command
);

void ussr_command_list_free(ussr_command_list_t *list);

void ussr_expression_free(ussr_expression_t *expression);

#endif
