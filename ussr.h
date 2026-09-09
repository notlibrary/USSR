#ifndef USSR_H
#define USSR_H

#include <stddef.h>

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

typedef struct
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
    USSR_ARGUMENT_COMMAND_LIST
} ussr_argument_type_t;

struct ussr_command_t;

typedef struct ussr_command_list_t
{
    struct ussr_command_t *head;
    struct ussr_command_t *tail;
} ussr_command_list_t;

/*
 * User-defined command.
 *
 * This must come after ussr_command_list_t because
 * body contains a pointer to that type.
 */
typedef struct ussr_definition_t
{
    char *name;
    char *return_name;

    char **parameter_names;
    size_t parameter_count;

    ussr_command_list_t *body;

    struct ussr_definition_t *next;
} ussr_definition_t;

typedef struct
{
    ussr_argument_type_t type;

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

/* User-defined commands. */
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

/* Runtime. */
void ussr_init(void);
void ussr_cleanup(void);

/* Values. */
ussr_value_t ussr_null(void);
ussr_value_t ussr_integer(long value);
ussr_value_t ussr_real(double value);
ussr_value_t ussr_boolean(int value);
ussr_value_t ussr_string(const char *value);

void ussr_value_free(ussr_value_t *value);
ussr_value_t ussr_value_copy(const ussr_value_t *value);

/* Variables. */
int ussr_set_variable(
    const char *name,
    const ussr_value_t *value
);

const ussr_value_t *ussr_get_variable(const char *name);

/* Execution. */
void ussr_print_value(const ussr_value_t *value);

int ussr_execute_command(
    const char *command,
    const char *return_name,
    ussr_argument_t *arguments,
    size_t argument_count
);

int ussr_execute_program(ussr_command_list_t *program);

/* Memory management. */
void ussr_command_list_free(ussr_command_list_t *list);
void ussr_expression_free(ussr_expression_t *expression);
int ussr_argument_evaluate(const ussr_argument_t *argument, ussr_value_t *result);
int ussr_expression_evaluate( const ussr_expression_t *expression,ussr_value_t *result);

#endif