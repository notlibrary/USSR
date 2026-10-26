#include "ussr_bytecode.h"
#include "ussr_uuasm.h"
#include "vm.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <limits.h>
#include <stdarg.h>


/*
 * Compiler only.
 *
 * IMPORTANT:
 *   This file never calls:
 *
 *       ussr_execute_command()
 *       ussr_execute_program()
 *       ussr_execute_user_definition()
 *
 * Those are runtime interpreter entry points.  The compiler translates
 * their semantics into bytecode instructions instead.
 */

typedef struct
{
    size_t *breaks;
    size_t break_count;
    size_t break_capacity;

    size_t *continues;
    size_t continue_count;
    size_t continue_capacity;
} bc_loop_t;

/*
 * Diagnostics. The first failure of a compilation is printed as
 * "file:line: reason", at the command being compiled; later failures on
 * the way out are not printed again. bc_error_command is the innermost
 * command being compiled, used when a helper has no command at hand.
 */
static const ussr_command_t *bc_error_command = NULL;
static int bc_error_reported = 0;

static void bc_error(
    const ussr_command_t *command,
    const char *format,
    ...
)
{
    va_list arguments;

    if (bc_error_reported)
        return;

    bc_error_reported = 1;

    if (command == NULL)
        command = bc_error_command;

    if (command != NULL && command->source_file != NULL)
        fprintf(
            stderr,
            "%s:%d: ",
            command->source_file,
            command->source_line
        );
    else
        fputs("USSR compiler: ", stderr);

    va_start(arguments, format);
    vfprintf(stderr, format, arguments);
    va_end(arguments);
    fputc('\n', stderr);
}

static void bc_warning(
    const ussr_command_t *command,
    const char *format,
    ...
)
{
    va_list arguments;

    if (command != NULL && command->source_file != NULL)
        fprintf(
            stderr,
            "%s:%d: warning: ",
            command->source_file,
            command->source_line
        );
    else
        fputs("USSR compiler: warning: ", stderr);

    va_start(arguments, format);
    vfprintf(stderr, format, arguments);
    va_end(arguments);
    fputc('\n', stderr);
}

/*
 * Variable scope (manual, "Variables in functions"). Names declared with
 * global(_) at the top level are global in every function; "*" there
 * makes every non-parameter variable global (the old behavior).
 * bc_code_commands remembers which command emitted each instruction,
 * for the warnings.
 */
typedef struct
{
    char **items;
    size_t count;
    size_t capacity;
} bc_names_t;

static bc_names_t bc_program_globals;
static int bc_program_all_global = 0;
static const ussr_command_t **bc_code_commands = NULL;
static size_t bc_code_command_capacity = 0;

static int bc_names_contains(const bc_names_t *names, const char *name)
{
    size_t i;

    for (i = 0; i < names->count; ++i)
        if (strcmp(names->items[i], name) == 0)
            return 1;

    return 0;
}

/* Adds a borrowed name once; the list never owns its strings. */
static int bc_names_add(bc_names_t *names, const char *name)
{
    if (bc_names_contains(names, name))
        return 0;

    if (names->count == names->capacity)
    {
        size_t capacity = names->capacity == 0 ? 16 : names->capacity * 2;
        char **items = realloc(names->items, capacity * sizeof(*items));

        if (items == NULL)
            return -1;
        names->items = items;
        names->capacity = capacity;
    }

    names->items[names->count++] = (char *)name;
    return 0;
}

static void bc_names_free(bc_names_t *names)
{
    free(names->items);
    memset(names, 0, sizeof(*names));
}

static int bc_is_variable_argument(const ussr_argument_t *argument)
{
    return argument->type == USSR_ARGUMENT_EXPRESSION &&
           argument->data.expression != NULL &&
           argument->data.expression->type == USSR_EXPR_VARIABLE;
}

static char *bc_strdup(const char *s)
{
    size_t n;
    char *p;

    if (s == NULL)
        return NULL;

    n = strlen(s);
    p = malloc(n + 1);

    if (p != NULL)
        memcpy(p, s, n + 1);

    return p;
}

static int bc_emit(
    ussr_bc_program_t *p,
    uint8_t opcode,
    uint8_t a,
    uint8_t b,
    uint8_t c,
    uint32_t immediate
)
{
    ussr_bc_instruction_t *q;
    size_t capacity;

    if (p == NULL || p->code_count >= USSR_BC_MAX_CODE)
        return -1;

    if (p->code_count == p->code_capacity)
    {
        capacity = p->code_capacity == 0 ? 256 : p->code_capacity * 2;

        if (capacity > USSR_BC_MAX_CODE)
            capacity = USSR_BC_MAX_CODE;

        q = realloc(p->code, capacity * sizeof(*q));

        if (q == NULL)
            return -1;

        p->code = q;
        p->code_capacity = capacity;
    }

    if (p->code_count >= bc_code_command_capacity)
    {
        size_t grown = bc_code_command_capacity == 0
            ? 256 : bc_code_command_capacity * 2;
        const ussr_command_t **commands = realloc(
            (void *)bc_code_commands, grown * sizeof(*commands));

        if (commands == NULL)
            return -1;
        bc_code_commands = commands;
        bc_code_command_capacity = grown;
    }
    bc_code_commands[p->code_count] = bc_error_command;

    p->code[p->code_count].opcode = opcode;
    p->code[p->code_count].a = a;
    p->code[p->code_count].b = b;
    p->code[p->code_count].c = c;
    p->code[p->code_count].immediate = immediate;

    return (int)p->code_count++;
}

static int bc_add_constant(
    ussr_bc_program_t *p,
    const ussr_value_t *value
)
{
    ussr_value_t *q;
    size_t capacity;

    if (p == NULL || value == NULL)
        return -1;

    if (p->constant_count == p->constant_capacity)
    {
        capacity = p->constant_capacity == 0 ?
            32 : p->constant_capacity * 2;

        q = realloc(
            p->constants,
            capacity * sizeof(*q)
        );

        if (q == NULL)
            return -1;

        p->constants = q;
        p->constant_capacity = capacity;
    }

    p->constants[p->constant_count] =
        ussr_value_copy(value);

    return (int)p->constant_count++;
}

static int bc_add_string(
    ussr_bc_program_t *p,
    const char *value
)
{
    char **q;
    size_t capacity;
    char *copy;

    if (p == NULL || value == NULL)
        return -1;

    if (p->string_count == p->string_capacity)
    {
        capacity = p->string_capacity == 0 ?
            32 : p->string_capacity * 2;

        q = realloc(
            p->strings,
            capacity * sizeof(*q)
        );

        if (q == NULL)
            return -1;

        p->strings = q;
        p->string_capacity = capacity;
    }

    copy = bc_strdup(value);

    if (copy == NULL)
        return -1;

    p->strings[p->string_count] = copy;

    return (int)p->string_count++;
}

static ussr_bc_function_t *
bc_find_function(
    ussr_bc_program_t *p,
    const char *name
)
{
    size_t i;

    if (p == NULL || name == NULL)
        return NULL;

    for (i = 0; i < p->function_count; ++i)
    {
        if (strcmp(p->functions[i].name, name) == 0)
            return &p->functions[i];
    }

    return NULL;
}

static int bc_is_definition(
    const ussr_command_t *command
)
{
    const char *name;

    if (command == NULL ||
        command->argument_count == 0)
        return 0;

    if (command->arguments[
            command->argument_count - 1
        ].type != USSR_ARGUMENT_COMMAND_LIST)
        return 0;

    name = command->name;

    if (name == NULL)
        return 0;

    /*
     * These commands also accept blocks, but they are control-flow
     * constructs, not user-defined function declarations.
     */
    if (strcmp(name, "@") == 0 ||
        strcmp(name, "if") == 0 ||
        strcmp(name, "elif") == 0 ||
        strcmp(name, "else") == 0 ||
        strcmp(name, "choose") == 0 ||
        strcmp(name, "option") == 0 ||
        strcmp(name, "default") == 0 ||
        strcmp(name, "for") == 0 ||
        strcmp(name, "while") == 0 ||
        strcmp(name, "break") == 0 ||
        strcmp(name, "continue") == 0 ||
        strcmp(name, "return") == 0 ||
	    strcmp(name, "do") == 0 ||
		strcmp(name, "loop") == 0 ||
        strcmp(name, "method") == 0 ||
        strcmp(name, "each") == 0 ||
        strcmp(name, "map") == 0 ||
        strcmp(name, "filter") == 0)
        return 0;

    return 1;
}

/* Adds the names of one global(_): declaration to names; "*" sets *all. */
static int bc_declare_globals(
    const ussr_command_t *command,
    bc_names_t *names,
    int *all
)
{
    size_t i;

    for (i = 0; i < command->argument_count; ++i)
    {
        const ussr_argument_t *a = &command->arguments[i];

        if (bc_is_variable_argument(a))
        {
            if (bc_names_add(names, a->data.expression->data.variable) != 0)
                return -1;
        }
        else if (a->type == USSR_ARGUMENT_VALUE &&
                 a->data.value.type == USSR_STRING &&
                 strcmp(a->data.value.data.string, "*") == 0)
        {
            *all = 1;
        }
        else
        {
            bc_error(
                command,
                "global expects variable names (or \"*\")"
            );
            return -1;
        }
    }

    return 0;
}

/* Collects global(_) declarations in list and its blocks, not looking
 * into function definitions (they have their own). */
static int bc_collect_globals(
    const ussr_command_list_t *list,
    bc_names_t *names,
    int *all
)
{
    const ussr_command_t *command;
    size_t i;

    if (list == NULL)
        return 0;

    for (command = list->head; command != NULL; command = command->next)
    {
        if (bc_is_definition(command))
            continue;

        if (strcmp(command->name, "global") == 0)
        {
            if (bc_declare_globals(command, names, all) != 0)
                return -1;
            continue;
        }

        for (i = 0; i < command->argument_count; ++i)
            if (command->arguments[i].type == USSR_ARGUMENT_COMMAND_LIST &&
                bc_collect_globals(
                    command->arguments[i].data.command_list,
                    names, all) != 0)
                return -1;
    }

    return 0;
}

static int bc_collect_functions(
    ussr_bc_program_t *p,
    const ussr_command_list_t *list
)
{
    const ussr_command_t *command;

    if (list == NULL)
        return 0;

    for (command = list->head;
         command != NULL;
         command = command->next)
    {
        size_t i;

        if (bc_is_definition(command))
        {
            ussr_bc_function_t *f;
            size_t count;
            size_t capacity;
            size_t j;

            if (bc_find_function(p, command->name) != NULL)
            {
                bc_error(
                    command,
                    "function '%s' is defined twice",
                    command->name
                );
                return -1;
            }

            count = command->argument_count - 1;

            if (p->function_count == p->function_capacity)
            {
                capacity = p->function_capacity == 0 ?
                    16 : p->function_capacity * 2;

                f = realloc(
                    p->functions,
                    capacity * sizeof(*f)
                );

                if (f == NULL)
                    return -1;

                p->functions = f;
                p->function_capacity = capacity;
            }

            f = &p->functions[p->function_count];
            memset(f, 0, sizeof(*f));

            f->name = bc_strdup(command->name);
            f->return_name = bc_strdup(command->return_name);

            if (f->name == NULL ||
                f->return_name == NULL)
                return -1;

            f->parameter_count = count;

            if (count != 0)
            {
                f->parameters = calloc(
                    count,
                    sizeof(*f->parameters)
                );

                if (f->parameters == NULL)
                    return -1;

                for (j = 0; j < count; ++j)
                {
                    const ussr_argument_t *a =
                        &command->arguments[j];

                    if (a->type != USSR_ARGUMENT_EXPRESSION ||
                        a->data.expression == NULL ||
                        a->data.expression->type !=
                            USSR_EXPR_VARIABLE)
                    {
                        bc_error(
                            command,
                            "parameter %zu of function '%s' must be a "
                            "name",
                            j + 1,
                            command->name
                        );
                        return -1;
                    }

                    f->parameters[j] =
                        bc_strdup(
                            a->data.expression->data.variable
                        );

                    if (f->parameters[j] == NULL)
                        return -1;
                }
            }

            f->entry = 0;
            ++p->function_count;

            if (bc_collect_functions(
                    p,
                    command->arguments[
                        command->argument_count - 1
                    ].data.command_list
                ) != 0)
                return -1;
        }
        else
        {
            for (i = 0; i < command->argument_count; ++i)
            {
                if (command->arguments[i].type ==
                    USSR_ARGUMENT_COMMAND_LIST)
                {
                    if (bc_collect_functions(
                            p,
                            command->arguments[i]
                                .data.command_list
                        ) != 0)
                        return -1;
                }
            }
        }
    }

    return 0;
}

static int bc_patch(
    ussr_bc_program_t *p,
    size_t instruction,
    size_t target
)
{
    if (p == NULL ||
        instruction >= p->code_count ||
        target > UINT32_MAX)
        return -1;

    p->code[instruction].immediate =
        (uint32_t)target;

    return 0;
}

static int bc_loop_add(
    size_t **items,
    size_t *count,
    size_t *capacity,
    size_t value
)
{
    size_t new_capacity;
    size_t *q;

    if (*count == *capacity)
    {
        new_capacity =
            *capacity == 0 ? 8 : *capacity * 2;

        q = realloc(
            *items,
            new_capacity * sizeof(*q)
        );

        if (q == NULL)
            return -1;

        *items = q;
        *capacity = new_capacity;
    }

    (*items)[(*count)++] = value;

    return 0;
}

static void bc_loop_free(bc_loop_t *loop)
{
    if (loop == NULL)
        return;

    free(loop->breaks);
    free(loop->continues);

    memset(loop, 0, sizeof(*loop));
}

/* The VM opcode of a binary operator, or 0 when there is none. */
static uint8_t bc_binary_opcode(ussr_operator_t op)
{
    switch (op)
    {
        case USSR_OP_ADD:          return USSR_BC_ADD;
        case USSR_OP_SUB:          return USSR_BC_SUB;
        case USSR_OP_MUL:          return USSR_BC_MUL;
        case USSR_OP_DIV:          return USSR_BC_DIV;
        case USSR_OP_MOD:          return USSR_BC_MOD;
        case USSR_OP_EQ:           return USSR_BC_EQ;
        case USSR_OP_NE:           return USSR_BC_NE;
        case USSR_OP_LT:           return USSR_BC_LT;
        case USSR_OP_LE:           return USSR_BC_LE;
        case USSR_OP_GT:           return USSR_BC_GT;
        case USSR_OP_GE:           return USSR_BC_GE;
        case USSR_OP_LOGICAL_AND:  return USSR_BC_LAND;
        case USSR_OP_LOGICAL_OR:   return USSR_BC_LOR;
        case USSR_OP_SHIFT_LEFT:   return USSR_BC_SHL;
        case USSR_OP_SHIFT_RIGHT:  return USSR_BC_SHR;
        case USSR_OP_BITWISE_XOR:  return USSR_BC_XOR;
        case USSR_OP_BITWISE_AND:  return USSR_BC_BAND;
        case USSR_OP_BITWISE_OR:   return USSR_BC_BOR;
        default:                   return 0;
    }
}

/*
 * Constant folding (docs/OPTIMIZATIONS.md, O1): an expression made only of
 * literals, operators and unary minus is computed here with the VM's own
 * operators, so it runs as one LOAD_CONST. Returns 0 with *out set, or -1
 * (nothing to free) when it is not constant or would fail at run time;
 * the VM then reports that failure where it always did. && and || are
 * left alone: they short-circuit and yield an operand, not a boolean.
 */
static int bc_fold(const ussr_expression_t *expression, ussr_value_t *out)
{
    ussr_value_t left;
    ussr_value_t right;
    uint8_t opcode;
    int status;

    switch (expression->type)
    {
        case USSR_EXPR_VALUE:
            if (expression->data.value.type != USSR_INTEGER &&
                expression->data.value.type != USSR_REAL &&
                expression->data.value.type != USSR_BOOLEAN &&
                expression->data.value.type != USSR_STRING)
                return -1;
            *out = ussr_value_copy(&expression->data.value);
            return 0;

        case USSR_EXPR_UNARY:
            if (expression->data.unary.operator != USSR_OP_SUB ||
                bc_fold(expression->data.unary.operand, &left) != 0)
                return -1;
            if (left.type == USSR_INTEGER)
                *out = ussr_integer(-left.data.integer);
            else if (left.type == USSR_REAL)
                *out = ussr_real(-left.data.real);
            else
            {
                ussr_value_free(&left);
                return -1;
            }
            return 0;

        case USSR_EXPR_BINARY:
            opcode = bc_binary_opcode(expression->data.binary.operator);
            if (opcode == 0 ||
                opcode == USSR_BC_LAND ||
                opcode == USSR_BC_LOR ||
                bc_fold(expression->data.binary.left, &left) != 0)
                return -1;
            if (bc_fold(expression->data.binary.right, &right) != 0)
            {
                ussr_value_free(&left);
                return -1;
            }
            status = vm_fold_binary(opcode, &left, &right, out);
            ussr_value_free(&left);
            ussr_value_free(&right);
            return status;

        default:
            return -1;
    }
}

static int bc_emit_binary(
    ussr_bc_program_t *p,
    ussr_operator_t op,
    uint8_t dst,
    uint8_t left,
    uint8_t right
)
{
    uint8_t opcode = bc_binary_opcode(op);

    if (opcode == 0)
        return -1;

    return bc_emit(
        p,
        opcode,
        dst,
        left,
        right,
        0
    ) < 0 ? -1 : 0;
}

static int bc_compile_expression(
    ussr_bc_program_t *p,
    const ussr_expression_t *expression,
    uint8_t dst
);

static int bc_compile_expression_short(
    ussr_bc_program_t *p,
    const ussr_expression_t *expression,
    uint8_t dst
)
{
    int jump;

    if (expression == NULL)
        return -1;

    if (expression->type == USSR_EXPR_BINARY &&
        expression->data.binary.operator ==
            USSR_OP_LOGICAL_AND)
    {
        if (bc_compile_expression_short(
                p,
                expression->data.binary.left,
                dst
            ) != 0)
            return -1;

        jump = bc_emit(
            p,
            USSR_BC_JMP_FALSE,
            dst,
            0,
            0,
            0
        );

        if (jump < 0)
            return -1;

        if (bc_compile_expression_short(
                p,
                expression->data.binary.right,
                dst
            ) != 0)
            return -1;

        return bc_patch(
            p,
            (size_t)jump,
            p->code_count
        );
    }

    if (expression->type == USSR_EXPR_BINARY &&
        expression->data.binary.operator ==
            USSR_OP_LOGICAL_OR)
    {
        if (bc_compile_expression_short(
                p,
                expression->data.binary.left,
                dst
            ) != 0)
            return -1;

        jump = bc_emit(
            p,
            USSR_BC_JMP_TRUE,
            dst,
            0,
            0,
            0
        );

        if (jump < 0)
            return -1;

        if (bc_compile_expression_short(
                p,
                expression->data.binary.right,
                dst
            ) != 0)
            return -1;

        return bc_patch(
            p,
            (size_t)jump,
            p->code_count
        );
    }

    return bc_compile_expression(
        p,
        expression,
        dst
    );
}

static int bc_compile_expression(
    ussr_bc_program_t *p,
    const ussr_expression_t *expression,
    uint8_t dst
)
{
    uint8_t left_reg;
    uint8_t right_reg;
    int index;

    if (p == NULL ||
        expression == NULL ||
        dst >= USSR_BC_RETURN_REG)
        return -1;

    if (expression->type == USSR_EXPR_BINARY ||
        expression->type == USSR_EXPR_UNARY)
    {
        ussr_value_t folded;

        if (bc_fold(expression, &folded) == 0)
        {
            index = bc_add_constant(p, &folded);
            ussr_value_free(&folded);

            if (index < 0)
                return -1;

            return bc_emit(
                p,
                USSR_BC_LOAD_CONST,
                dst,
                0,
                0,
                (uint32_t)index
            ) < 0 ? -1 : 0;
        }
    }

    switch (expression->type)
    {
        case USSR_EXPR_VALUE:
            index = bc_add_constant(
                p,
                &expression->data.value
            );

            if (index < 0)
                return -1;

            return bc_emit(
                p,
                USSR_BC_LOAD_CONST,
                dst,
                0,
                0,
                (uint32_t)index
            ) < 0 ? -1 : 0;

        case USSR_EXPR_VARIABLE:
            index = bc_add_string(
                p,
                expression->data.variable
            );

            if (index < 0)
                return -1;

            return bc_emit(
                p,
                USSR_BC_LOAD_VAR,
                dst,
                0,
                0,
                (uint32_t)index
            ) < 0 ? -1 : 0;

        case USSR_EXPR_UNARY:
            if (bc_compile_expression(
                    p,
                    expression->data.unary.operand,
                    dst
                ) != 0)
                return -1;

            if (expression->data.unary.operator ==
                USSR_OP_SUB)
            {
                return bc_emit(
                    p,
                    USSR_BC_NEG,
                    dst,
                    dst,
                    0,
                    0
                ) < 0 ? -1 : 0;
            }

            bc_error(NULL, "unsupported unary operator");
            return -1;

        case USSR_EXPR_BINARY:
            left_reg = (uint8_t)(dst + 1);
            right_reg = (uint8_t)(dst + 2);

            if (right_reg >= USSR_BC_RETURN_REG)
            {
                bc_error(
                    NULL,
                    "expression is nested too deeply; split it with set"
                );
                return -1;
            }

            if (expression->data.binary.operator ==
                    USSR_OP_LOGICAL_AND ||
                expression->data.binary.operator ==
                    USSR_OP_LOGICAL_OR)
            {
                return bc_compile_expression_short(
                    p,
                    expression,
                    dst
                );
            }

            if (bc_compile_expression(
                    p,
                    expression->data.binary.left,
                    left_reg
                ) != 0)
                return -1;

            if (bc_compile_expression(
                    p,
                    expression->data.binary.right,
                    right_reg
                ) != 0)
                return -1;

            return bc_emit_binary(
                p,
                expression->data.binary.operator,
                dst,
                left_reg,
                right_reg
            );

        default:
            return -1;
    }
}

static int bc_compile_argument(
    ussr_bc_program_t *p,
    const ussr_argument_t *argument,
    uint8_t dst
)
{
    int index;

    if (p == NULL ||
        argument == NULL ||
        dst > USSR_BC_RETURN_REG)
        return -1;

    switch (argument->type)
    {
        case USSR_ARGUMENT_VALUE:
            index = bc_add_constant(
                p,
                &argument->data.value
            );

            if (index < 0)
                return -1;

            return bc_emit(
                p,
                USSR_BC_LOAD_CONST,
                dst,
                0,
                0,
                (uint32_t)index
            ) < 0 ? -1 : 0;

        case USSR_ARGUMENT_EXPRESSION:
            return bc_compile_expression_short(
                p,
                argument->data.expression,
                dst
            );

        case USSR_ARGUMENT_LOOKUP:
            if (argument->data.expression == NULL ||
                argument->data.expression->type !=
                    USSR_EXPR_VARIABLE)
            {
                bc_error(NULL, "'?' lookup needs a variable name");
                return -1;
            }

            index = bc_add_string(
                p,
                argument->data.expression->data.variable
            );

            if (index < 0)
                return -1;

            return bc_emit(
                p,
                USSR_BC_LOAD_HASH,
                dst,
                0,
                0,
                (uint32_t)index
            ) < 0 ? -1 : 0;

        case USSR_ARGUMENT_UNO_LITERAL:
            index = bc_add_string(
                p,
                argument->data.uno_text
            );

            if (index < 0)
                return -1;

            return bc_emit(
                p,
                USSR_BC_DECODE_UNO,
                dst,
                0,
                0,
                (uint32_t)index
            ) < 0 ? -1 : 0;

        case USSR_ARGUMENT_COMMAND_LIST:
            bc_error(NULL, "a [ ] block cannot be used as a value here");
            return -1;

        default:
            return -1;
    }
}

static int bc_compile_list(
    ussr_bc_program_t *p,
    const ussr_command_list_t *list,
    bc_loop_t *loop,
    ussr_bc_function_t *current
);

static int bc_store_result(
    ussr_bc_program_t *p,
    const char *name,
    uint8_t reg,
    int hash
)
{
    int index;

    if (name == NULL)
        return -1;

    index = bc_add_string(p, name);

    if (index < 0)
        return -1;

    return bc_emit(
        p,
        hash ? USSR_BC_STORE_HASH : USSR_BC_STORE_VAR,
        reg,
        0,
        0,
        (uint32_t)index
    ) < 0 ? -1 : 0;
}

static int bc_store_boolean(
    ussr_bc_program_t *p,
    const char *name,
    int value
)
{
    ussr_value_t v;
    int index;
    int result;

    v = ussr_boolean(value);

    index = bc_add_constant(p, &v);
    ussr_value_free(&v);

    if (index < 0)
        return -1;

    if (bc_emit(
            p,
            USSR_BC_LOAD_CONST,
            0,
            0,
            0,
            (uint32_t)index
        ) < 0)
        return -1;

    result = bc_store_result(
        p,
        name,
        0,
        0
    );

    return result;
}

static int bc_add_oop_site(
    ussr_bc_program_t *p,
    uint32_t instruction,
    const ussr_command_t *command
)
{
    ussr_bc_oop_site_t *q;
    size_t capacity;

    if (p == NULL || command == NULL)
        return -1;

    if (p->oop_site_count == p->oop_site_capacity)
    {
        capacity = p->oop_site_capacity == 0 ?
            16 : p->oop_site_capacity * 2;

        q = realloc(
            p->oop_sites,
            capacity * sizeof(*q)
        );

        if (q == NULL)
            return -1;

        p->oop_sites = q;
        p->oop_site_capacity = capacity;
    }

    p->oop_sites[p->oop_site_count].instruction = instruction;
    p->oop_sites[p->oop_site_count].command = command;
    ++p->oop_site_count;

    return 0;
}

static int bc_add_scan_site(
    ussr_bc_program_t *p,
    uint32_t instruction,
    const ussr_command_t *command
)
{
    ussr_bc_scan_site_t *q;
    size_t capacity;

    if (p == NULL || command == NULL)
        return -1;

    if (p->scan_site_count == p->scan_site_capacity)
    {
        capacity = p->scan_site_capacity == 0 ?
            16 : p->scan_site_capacity * 2;
        q = realloc(p->scan_sites, capacity * sizeof(*q));
        if (q == NULL)
            return -1;
        p->scan_sites = q;
        p->scan_site_capacity = capacity;
    }

    p->scan_sites[p->scan_site_count].instruction = instruction;
    p->scan_sites[p->scan_site_count].command = command;
    ++p->scan_site_count;
    return 0;
}

static int bc_compile_until(
    ussr_bc_program_t *p,
    const ussr_command_t *start,
    const ussr_command_t *stop,
    bc_loop_t *loop,
    ussr_bc_function_t *current,
    const ussr_command_t **next_out
);

static int bc_compile_conditional_chain(
    ussr_bc_program_t *p,
    const ussr_command_t *first,
    bc_loop_t *loop,
    ussr_bc_function_t *current,
    const ussr_command_t **next_out
);

static int bc_compile_block_chain(
    ussr_bc_program_t *p,
    const ussr_command_t *first,
    bc_loop_t *loop,
    ussr_bc_function_t *current,
    const ussr_command_t **next_out
);

static int bc_compile_choose(
    ussr_bc_program_t *p,
    const ussr_command_t *first,
    bc_loop_t *outer_loop,
    ussr_bc_function_t *current,
    const ussr_command_t **next_out
);

static int bc_compile_command_body(
    ussr_bc_program_t *p,
    const ussr_command_t *command,
    bc_loop_t *loop,
    ussr_bc_function_t *current
);

static int bc_compile_command(
    ussr_bc_program_t *p,
    const ussr_command_t *command,
    bc_loop_t *loop,
    ussr_bc_function_t *current
)
{
    const ussr_command_t *outer = bc_error_command;
    int result;

    bc_error_command = command;
    result = bc_compile_command_body(p, command, loop, current);

    /* Every failure gets a location, even one without its own message
     * (running out of memory, for example). */
    if (result != 0 && command != NULL)
        bc_error(command, "cannot compile '%s'", command->name);

    bc_error_command = outer;
    return result;
}

static int bc_compile_command_body(
    ussr_bc_program_t *p,
    const ussr_command_t *command,
    bc_loop_t *loop,
    ussr_bc_function_t *current
)
{
    const ussr_argument_t *a;
    size_t count;
    size_t i;
    int jump_false;
    int jump_end;
    int index;
    uint8_t opcode;
    ussr_bc_function_t *function;

    if (p == NULL || command == NULL)
        return -1;

    if (bc_is_definition(command))
        return 0;

    a = command->arguments;
    count = command->argument_count;

    /* global(_): names... is a declaration; it emits no code. */
    if (strcmp(command->name, "global") == 0)
        return 0;

    /*
     * if(condition): [then] [else]
     *
     * The AST block is compiled directly into the instruction stream.
     */
    if (strcmp(command->name, "if") == 0)
    {
        if (count < 2 || count > 3 ||
            a[1].type != USSR_ARGUMENT_COMMAND_LIST ||
            (count == 3 &&
             a[2].type != USSR_ARGUMENT_COMMAND_LIST))
        {
            bc_error(
                command,
                "if expects a condition and a [ ] block "
                "(optionally followed by an else block)"
            );
            return -1;
        }

        if (bc_compile_argument(p, &a[0], 0) != 0)
            return -1;

        jump_false = bc_emit(
            p,
            USSR_BC_JMP_FALSE,
            0,
            0,
            0,
            0
        );

        if (jump_false < 0)
            return -1;

        if (bc_compile_list(
                p,
                a[1].data.command_list,
                loop,
                current
            ) != 0)
            return -1;

        if (count == 3)
        {
            jump_end = bc_emit(
                p,
                USSR_BC_JMP,
                0,
                0,
                0,
                0
            );

            if (jump_end < 0)
                return -1;

            if (bc_patch(
                    p,
                    (size_t)jump_false,
                    p->code_count
                ) != 0)
                return -1;

            if (bc_compile_list(
                    p,
                    a[2].data.command_list,
                    loop,
                    current
                ) != 0)
                return -1;

            if (bc_patch(
                    p,
                    (size_t)jump_end,
                    p->code_count
                ) != 0)
                return -1;
        }
        else
        {
            if (bc_patch(
                    p,
                    (size_t)jump_false,
                    p->code_count
                ) != 0)
                return -1;
        }

        if (command->return_name != NULL)
        {
            if (bc_store_boolean(
                    p,
                    command->return_name,
                    1
                ) != 0)
                return -1;
        }

        return 0;
    }

    /*
     * while(condition): [body]
     */
if (strcmp(command->name, "do") == 0)
    {
        bc_loop_t child;
        const ussr_command_t *loop_command;
        size_t body_start;
        size_t cond_start;
        size_t end;
        size_t n;
        int jump_false;

        memset(&child, 0, sizeof(child));

        /*
         * Syntax:
         *
         *     do(_): 0 [
         *         ...
         *     ]
         *     loop(b): {condition}
         *
         * The first do argument is the do command's value/result
         * parameter. The second argument is the body block.
         * The loop command is the NEXT command in the list.
         */
        if (count != 2 ||
            a[1].type != USSR_ARGUMENT_COMMAND_LIST)
        {
            bc_error(command, "do expects a value and a [ ] body");
            return -1;
        }

        loop_command = command->next;

        if (loop_command == NULL ||
            strcmp(loop_command->name, "loop") != 0 ||
            loop_command->argument_count != 1)
        {
            bc_error(
                command,
                "do must be followed by loop(_): condition"
            );
            return -1;
        }

        body_start = p->code_count;

        if (bc_compile_list(
                p,
                a[1].data.command_list,
                &child,
                current
            ) != 0)
            goto do_error;

        /*
         * continue jumps here, so the loop condition is evaluated
         * before another body iteration.
         */
        cond_start = p->code_count;

        if (bc_compile_argument(
                p,
                &loop_command->arguments[0],
                0
            ) != 0)
            goto do_error;

        jump_false = bc_emit(
            p,
            USSR_BC_JMP_FALSE,
            0,
            0,
            0,
            0
        );

        if (jump_false < 0)
            goto do_error;

        if (bc_emit(
                p,
                USSR_BC_JMP,
                0,
                0,
                0,
                (uint32_t)body_start
            ) < 0)
            goto do_error;

        end = p->code_count;

        for (n = 0; n < child.continue_count; ++n)
        {
            if (bc_patch(
                    p,
                    child.continues[n],
                    cond_start
                ) != 0)
                goto do_error;
        }

        if (bc_patch(
                p,
                (size_t)jump_false,
                end
            ) != 0)
            goto do_error;

        for (n = 0; n < child.break_count; ++n)
        {
            if (bc_patch(
                    p,
                    child.breaks[n],
                    end
                ) != 0)
                goto do_error;
        }

        bc_loop_free(&child);

        if (command->return_name != NULL)
        {
            if (bc_store_boolean(
                    p,
                    command->return_name,
                    1
                ) != 0)
                return -1;
        }

        return 0;

do_error:
        bc_loop_free(&child);
        return -1;
    }
    /*
     * for(condition): [ initializer iterator body... ]
     *
     * The ordinary command grammar already gives us one command list.
     * Its first command is the initializer, its second command is the
     * iterator, and every remaining command belongs to the loop body.
     * No special grammar is required.
     *
     * Execution order is:
     *
     *     initializer
     *     condition
     *     body...
     *     iterator
     *     condition
     *     ...
     *
     * Therefore continue must jump to iterator_start, not condition_start.
     */
    if (strcmp(command->name, "for") == 0)
    {
        bc_loop_t child;
        const ussr_command_list_t *for_body;
        const ussr_command_t *initializer;
        const ussr_command_t *iterator;
        const ussr_command_t *body;
        size_t condition_start;
        size_t iterator_start;
        size_t end;
        size_t n;
        int jump_false;

        memset(&child, 0, sizeof(child));

        if (count != 2 ||
            a[1].type != USSR_ARGUMENT_COMMAND_LIST ||
            a[1].data.command_list == NULL)
        {
            bc_error(command, "for expects a condition and a [ ] block");
            return -1;
        }

        for_body = a[1].data.command_list;
        initializer = for_body->head;
        iterator = initializer != NULL ? initializer->next : NULL;
        body = iterator != NULL ? iterator->next : NULL;

        /* Initializer and iterator are mandatory. Everything after them
         * is the body, including zero or more commands. */
        if (initializer == NULL || iterator == NULL)
        {
            bc_error(
                command,
                "for needs an initializer and an iterator command "
                "at the start of its block"
            );
            return -1;
        }

        /* Initializer executes exactly once, before the first condition. */
        if (bc_compile_command(p, initializer, loop, current) != 0)
            return -1;

        condition_start = p->code_count;

        if (bc_compile_argument(p, &a[0], 0) != 0)
            return -1;

        jump_false = bc_emit(
            p, USSR_BC_JMP_FALSE, 0, 0, 0, 0
        );
        if (jump_false < 0)
            return -1;

        /* Compile every command after initializer/iterator as the body. */
        while (body != NULL)
        {
            const ussr_command_t *next = body->next;

            if (bc_compile_command(p, body, &child, current) != 0)
                goto for_error;

            /* Match bc_compile_list(): do/loop is one construct. */
            if (strcmp(body->name, "do") == 0)
            {
                if (next == NULL || strcmp(next->name, "loop") != 0)
                {
                    bc_error(
                        body,
                        "do must be followed by loop(_): condition"
                    );
                    goto for_error;
                }

                body = next->next;
            }
            else
            {
                if (strcmp(body->name, "loop") == 0)
                {
                    bc_error(body, "loop without a do before it");
                    goto for_error;
                }

                body = next;
            }
        }

        /* Iterator executes after the complete body on every iteration. */
        iterator_start = p->code_count;

        if (bc_compile_command(p, iterator, &child, current) != 0)
            goto for_error;

        if (bc_emit(
                p, USSR_BC_JMP, 0, 0, 0,
                (uint32_t)condition_start
            ) < 0)
            goto for_error;

        end = p->code_count;

        /* continue -> iterator, not condition */
        for (n = 0; n < child.continue_count; ++n)
        {
            if (bc_patch(
                    p, child.continues[n], iterator_start
                ) != 0)
                goto for_error;
        }

        if (bc_patch(p, (size_t)jump_false, end) != 0)
            goto for_error;

        for (n = 0; n < child.break_count; ++n)
        {
            if (bc_patch(p, child.breaks[n], end) != 0)
                goto for_error;
        }

        bc_loop_free(&child);

        if (command->return_name != NULL &&
            bc_store_boolean(p, command->return_name, 1) != 0)
            return -1;

        return 0;

for_error:
        bc_loop_free(&child);
        return -1;
    }

 if (strcmp(command->name, "while") == 0)
    {
        bc_loop_t child;
        size_t start;
        size_t end;
        size_t n;

        memset(&child, 0, sizeof(child));

        if (count != 2 ||
            a[1].type != USSR_ARGUMENT_COMMAND_LIST)
        {
            bc_error(command, "while expects a condition and a [ ] body");
            return -1;
        }

        start = p->code_count;

        if (bc_compile_argument(p, &a[0], 0) != 0)
            goto while_error;

        jump_false = bc_emit(
            p,
            USSR_BC_JMP_FALSE,
            0,
            0,
            0,
            0
        );

        if (jump_false < 0)
            goto while_error;

        if (bc_compile_list(
                p,
                a[1].data.command_list,
                &child,
                current
            ) != 0)
            goto while_error;

        for (n = 0; n < child.continue_count; ++n)
        {
            if (bc_patch(
                    p,
                    child.continues[n],
                    start
                ) != 0)
                goto while_error;
        }

        if (bc_emit(
                p,
                USSR_BC_JMP,
                0,
                0,
                0,
                (uint32_t)start
            ) < 0)
            goto while_error;

        end = p->code_count;

        if (bc_patch(
                p,
                (size_t)jump_false,
                end
            ) != 0)
            goto while_error;

        for (n = 0; n < child.break_count; ++n)
        {
            if (bc_patch(
                    p,
                    child.breaks[n],
                    end
                ) != 0)
                goto while_error;
        }

        bc_loop_free(&child);

        if (command->return_name != NULL)
        {
            /*
             * ussr_execute_while() returns true after normal termination.
             */
            if (bc_store_boolean(
                    p,
                    command->return_name,
                    1
                ) != 0)
                return -1;
        }

        return 0;

while_error:
        bc_loop_free(&child);
        return -1;
    }

    if (strcmp(command->name, "break") == 0 ||
        strcmp(command->name, "continue") == 0)
    {
        int jump;

        if (loop == NULL)
        {
            bc_error(command, "%s outside a loop", command->name);
            return -1;
        }

        if (count > 1)
        {
            bc_error(
                command,
                "%s takes at most one value, got %zu",
                command->name,
                count
            );
            return -1;
        }

        if (count == 1)
        {
            if (bc_compile_argument(p, &a[0], 0) != 0)
                return -1;

            if (command->return_name != NULL &&
                bc_store_result(p, command->return_name, 0, a[0].assignment) != 0)
                return -1;
        }
        else if (command->return_name != NULL &&
                 bc_store_boolean(p, command->return_name, 1) != 0)
        {
            return -1;
        }

        jump = bc_emit(
            p,
            USSR_BC_JMP,
            0,
            0,
            0,
            0
        );

        if (jump < 0)
            return -1;

        if (strcmp(command->name, "break") == 0)
        {
            return bc_loop_add(
                &loop->breaks,
                &loop->break_count,
                &loop->break_capacity,
                (size_t)jump
            );
        }

        return bc_loop_add(
            &loop->continues,
            &loop->continue_count,
            &loop->continue_capacity,
            (size_t)jump
        );
    }

	if (strcmp(command->name, "return") == 0)
	{
		if (current == NULL)
		{
			bc_error(command, "return outside a function");
			return -1;
		}

		if (count != 1)
		{
			bc_error(command, "return expects one value, got %zu", count);
			return -1;
		}

		/*
		 * RETURN_REG is reserved for CALL results and cannot be used
		 * as a compiler expression destination. Evaluate the return
		 * expression in an ordinary register first.
		 */
		if (bc_compile_argument(
				p,
				&a[0],
				0
			) != 0)
			return -1;

		if (bc_store_result(
			p,
			current->return_name,
			0,
			a[0].assignment
		) != 0)
			return -1;

		return bc_emit(
			p,
			USSR_BC_RET,
			0,
			0,
			0,
			0
		) < 0 ? -1 : 0;
	}

    /*
     * User-defined functions are resolved by the compiler, so CALL
     * contains a function-table index rather than a command string.
     */
    function = bc_find_function(p, command->name);

    if (function != NULL)
    {
        if (count != function->parameter_count)
        {
            bc_error(
                command,
                "function '%s' expects %zu parameter%s, got %zu",
                command->name,
                function->parameter_count,
                function->parameter_count == 1 ? "" : "s",
                count
            );
            return -1;
        }

        if (count >= USSR_BC_RETURN_REG)
        {
            bc_error(
                command,
                "'%s' has too many parameters (at most %d)",
                command->name,
                USSR_BC_RETURN_REG - 1
            );
            return -1;
        }

        for (i = 0; i < count; ++i)
        {
            if (bc_compile_argument(
                    p,
                    &a[i],
                    (uint8_t)i
                ) != 0)
                return -1;
        }

        if (bc_emit(
                p,
                USSR_BC_CALL,
                0,
                (uint8_t)count,
                USSR_BC_RETURN_REG,
                (uint32_t)(
                    function - p->functions
                )
            ) < 0)
            return -1;

        return bc_store_result(
            p,
            command->return_name,
            USSR_BC_RETURN_REG,
            0
        );
    }

    /* set(vector): index value */
    if (strcmp(command->name, "set") == 0 && count == 2)
    {
        int vector_name;

        if (command->return_name == NULL)
        {
            bc_error(command, "set(vector): index value needs a vector name");
            return -1;
        }

        vector_name = bc_add_string(p, command->return_name);
        if (vector_name < 0)
            return -1;

        if (bc_emit(p, USSR_BC_LOAD_VAR, 0, 0, 0,
                    (uint32_t)vector_name) < 0)
            return -1;

        if (bc_compile_argument(p, &a[0], 1) != 0 ||
            bc_compile_argument(p, &a[1], 2) != 0)
            return -1;

        if (bc_emit(p, USSR_BC_VECTOR_SET, 0, 1, 2, 0) < 0)
            return -1;

        /* The vector itself remains the command's return value/name. */
        return 0;
    }

    /*
     * set
     */
    if (strcmp(command->name, "set") == 0)
    {
        if (count != 1 ||
            command->return_name == NULL)
        {
            bc_error(
                command,
                "set expects one value (or an index and a value for a "
                "vector), got %zu",
                count
            );
            return -1;
        }

        if (bc_compile_argument(p, &a[0], 0) != 0)
            return -1;

        return bc_store_result(
            p,
            command->return_name,
            0,
            a[0].assignment
        );
    }

    /*
     * print
     */
    if (strcmp(command->name, "print") == 0)
    {
        if (count != 1 ||
            command->return_name == NULL)
        {
            bc_error(
                command,
                "print expects exactly one value, got %zu "
                "(join values with concat first)",
                count
            );
            return -1;
        }

        if (bc_compile_argument(p, &a[0], 0) != 0)
            return -1;

        if (bc_emit(
                p,
                USSR_BC_PRINT,
                0,
                0,
                0,
                0
            ) < 0)
            return -1;

        return bc_store_result(
            p,
            command->return_name,
            0,
            a[0].assignment
        );
    }

    /*
     * add/sub/mul/div/mod
     */
    if (strcmp(command->name, "add") == 0 ||
        strcmp(command->name, "sub") == 0 ||
        strcmp(command->name, "mul") == 0 ||
        strcmp(command->name, "div") == 0 ||
        strcmp(command->name, "mod") == 0)
    {
        if (count != 2 ||
            command->return_name == NULL)
        {
            bc_error(
                command,
                "%s expects two values, got %zu",
                command->name,
                count
            );
            return -1;
        }

        if (bc_compile_argument(p, &a[0], 0) != 0 ||
            bc_compile_argument(p, &a[1], 1) != 0)
            return -1;

        if (strcmp(command->name, "add") == 0)
            opcode = USSR_BC_ADD;
        else if (strcmp(command->name, "sub") == 0)
            opcode = USSR_BC_SUB;
        else if (strcmp(command->name, "mul") == 0)
            opcode = USSR_BC_MUL;
        else if (strcmp(command->name, "div") == 0)
            opcode = USSR_BC_DIV;
        else
            opcode = USSR_BC_MOD;

        if (bc_emit(
                p,
                opcode,
                2,
                0,
                1,
                0
            ) < 0)
            return -1;

        /*
         * The old command implementation applies ! after the command
         * succeeds.  Preserve that semantic at compile time.
         */
        if (bc_store_result(
                p,
                command->return_name,
                2,
                a[0].assignment || a[1].assignment
            ) != 0)
            return -1;

        return 0;
    }

    /*
     * concat
     */
    if (strcmp(command->name, "concat") == 0)
    {
        if (count < 2 ||
            command->return_name == NULL ||
            count >= USSR_BC_RETURN_REG)
        {
            bc_error(
                command,
                "concat expects 2 to %d values, got %zu",
                USSR_BC_RETURN_REG - 1,
                count
            );
            return -1;
        }

        for (i = 0; i < count; ++i)
        {
            if (bc_compile_argument(
                    p,
                    &a[i],
                    (uint8_t)i
                ) != 0)
                return -1;
        }

        if (bc_emit(
                p,
                USSR_BC_CONCAT,
                USSR_BC_RETURN_REG - 1,
                0,
                (uint8_t)count,
                0
            ) < 0)
            return -1;

        {
            int hash_assignment = 0;
            for (i = 0; i < count; ++i)
                hash_assignment |= a[i].assignment != 0;

            return bc_store_result(
                p,
                command->return_name,
                USSR_BC_RETURN_REG - 1,
                hash_assignment
            );
        }
    }

    /* get(vector): index output */
    if (strcmp(command->name, "get") == 0 && count == 2)
    {
        const ussr_expression_t *destination;
        int vector_name;
        int destination_name;

        if (command->return_name == NULL ||
            !bc_is_variable_argument(&a[1]))
        {
            bc_error(
                command,
                "get(vector): index destination needs a variable name as "
                "the destination"
            );
            return -1;
        }

        destination = a[1].data.expression;
        vector_name = bc_add_string(p, command->return_name);
        destination_name = bc_add_string(
            p, destination->data.variable
        );
        if (vector_name < 0 || destination_name < 0)
            return -1;

        if (bc_emit(p, USSR_BC_LOAD_VAR, 0, 0, 0,
                    (uint32_t)vector_name) < 0)
            return -1;

        if (bc_compile_argument(p, &a[0], 1) != 0)
            return -1;

        if (bc_emit(p, USSR_BC_VECTOR_GET, 2, 0, 1, 0) < 0)
            return -1;

        if (bc_emit(p, USSR_BC_STORE_VAR, 2, 0, 0,
                    (uint32_t)destination_name) < 0)
            return -1;

        return 0;
    }

    /*
     * get: get(a): b means b = a.
     */
    if (strcmp(command->name, "get") == 0)
    {
        const ussr_expression_t *expression;
        int source_index;
        int destination_index;

        if (count != 1 || command->return_name == NULL ||
            !bc_is_variable_argument(&a[0]))
        {
            bc_error(
                command,
                count == 1
                    ? "get(source): destination needs a variable name as "
                      "the destination"
                    : "get expects a destination (or an index and a "
                      "destination for a vector)"
            );
            return -1;
        }

        expression = a[0].data.expression;
        source_index = bc_add_string(p, command->return_name);
        destination_index = bc_add_string(p, expression->data.variable);
        if (source_index < 0 || destination_index < 0)
            return -1;

        if (bc_emit(p, USSR_BC_LOAD_VAR, 0, 0, 0,
                    (uint32_t)source_index) < 0)
            return -1;
        if (bc_emit(p, USSR_BC_STORE_VAR, 0, 0, 0,
                    (uint32_t)destination_index) < 0)
            return -1;

        return 0;
    }

    /* random64(i) returns the next xoshiro256** value. */
    if (strcmp(command->name, "random64") == 0)
    {
        if (count != 0 || command->return_name == NULL)
        {
            bc_error(command, "random64 takes no parameters");
            return -1;
        }
        if (bc_emit(p, USSR_BC_RANDOM64, USSR_BC_RETURN_REG,
                    0, 0, 0) < 0)
            return -1;
        return bc_store_result(p, command->return_name,
                               USSR_BC_RETURN_REG, 0);
    }

    /* seed_random64(res): seed */
    if (strcmp(command->name, "seed_random64") == 0)
    {
        if (count != 1 || command->return_name == NULL)
        {
            bc_error(command, "seed_random64 expects one seed, got %zu",
                     count);
            return -1;
        }
        if (bc_compile_argument(p, &a[0], 0) != 0)
            return -1;
        if (bc_emit(p, USSR_BC_SEED64, USSR_BC_RETURN_REG,
                    0, 0, 0) < 0)
            return -1;
        return bc_store_result(p, command->return_name,
                               USSR_BC_RETURN_REG, 0);
    }

    /* time(now) returns epoch seconds. */
    if (strcmp(command->name, "time") == 0)
    {
        if (count != 0 || command->return_name == NULL)
        {
            bc_error(command, "time takes no parameters");
            return -1;
        }
        if (bc_emit(p, USSR_BC_TIME, USSR_BC_RETURN_REG,
                    0, 0, 0) < 0)
            return -1;
        return bc_store_result(p, command->return_name,
                               USSR_BC_RETURN_REG, 0);
    }

    /* scan(return): "prompt TYPE;TYPE;..." destination... */
    if (strcmp(command->name, "scan") == 0)
    {
        int instruction;

        if (count < 2 || command->return_name == NULL ||
            a[0].type != USSR_ARGUMENT_VALUE ||
            a[0].data.value.type != USSR_STRING ||
            count - 1 > 255)
        {
            bc_error(
                command,
                "scan expects a format string and 1 to 255 variables"
            );
            return -1;
        }

        for (i = 1; i < count; ++i)
        {
            if (!bc_is_variable_argument(&a[i]))
            {
                bc_error(
                    command,
                    "scan destination %zu must be a variable name",
                    i
                );
                return -1;
            }
        }

        index = bc_add_string(p, a[0].data.value.data.string);
        if (index < 0)
            return -1;

        instruction = bc_emit(
            p, USSR_BC_SCAN, USSR_BC_RETURN_REG,
            (uint8_t)(count - 1), 0, (uint32_t)index
        );
        if (instruction < 0)
            return -1;

        if (bc_add_scan_site(p, (uint32_t)instruction, command) != 0)
            return -1;

        return bc_store_result(p, command->return_name,
                               USSR_BC_RETURN_REG, 0);
    }

    /* template(return): "TYPE;TYPE;..." value... */
    if (strcmp(command->name, "template") == 0)
    {
        if (count < 2 || command->return_name == NULL ||
            a[0].type != USSR_ARGUMENT_VALUE ||
            a[0].data.value.type != USSR_STRING ||
            count - 1 > 255)
        {
            bc_error(
                command,
                "template expects a format string and 1 to 255 values"
            );
            return -1;
        }

        for (i = 1; i < count; ++i)
        {
            if (a[i].type == USSR_ARGUMENT_COMMAND_LIST)
            {
                bc_error(command, "template cannot take a [ ] block");
                return -1;
            }

            if (bc_compile_argument(p, &a[i], (uint8_t)(i - 1)) != 0)
                return -1;
        }

        index = bc_add_string(p, a[0].data.value.data.string);
        if (index < 0)
            return -1;

        if (bc_emit(p, USSR_BC_TEMPLATE, USSR_BC_RETURN_REG,
                    (uint8_t)(count - 1), 0, (uint32_t)index) < 0)
            return -1;

        return bc_store_result(p, command->return_name,
                               USSR_BC_RETURN_REG, 0);
    }

    /* cd: change the interpreter working directory. */
    if (strcmp(command->name, "cd") == 0)
    {
        if (count != 1 || command->return_name == NULL)
        {
            bc_error(command, "cd expects one directory, got %zu", count);
            return -1;
        }

        if (bc_compile_argument(p, &a[0], 0) != 0)
            return -1;

        if (bc_emit(p, USSR_BC_CD, 0, 0, 0, 0) < 0)
            return -1;

        return bc_store_result(p, command->return_name, 0,
                               a[0].assignment);
    }

    /* chain: the preceding external command is already captured by the
     * compiler when it is immediately followed by chain.  This instruction
     * turns that captured output into the input of the next command. */
    if (strcmp(command->name, "chain") == 0)
    {
        if (count != 0 || command->return_name == NULL)
        {
            bc_error(command, "chain takes no parameters");
            return -1;
        }

        if (bc_emit(p, USSR_BC_CHAIN, 0, 0, 0, 0) < 0)
            return -1;

        return bc_store_result(p, command->return_name, 0, 0);
    }

    /* file: terminate a chain by writing its captured output to a file. */
    if (strcmp(command->name, "file") == 0)
    {
        if (count != 1 || command->return_name == NULL)
        {
            bc_error(command, "file expects one file name, got %zu", count);
            return -1;
        }
        if (bc_compile_argument(p, &a[0], 0) != 0)
            return -1;
        if (bc_emit(p, USSR_BC_FILE, 0, 0, 0, 0) < 0)
            return -1;
        return bc_store_result(p, command->return_name, 0,
                               a[0].assignment);
    }

    /*
     * eval is compiled as a dedicated dynamic-code instruction.
     * The runtime implementation must parse/compile its string into
     * another bytecode program; it must not call ussr_execute_program().
     */
    if (strcmp(command->name, "eval") == 0)
    {
        if (count != 1 ||
            command->return_name == NULL)
        {
            bc_error(command, "eval expects one string, got %zu", count);
            return -1;
        }

        if (bc_compile_argument(p, &a[0], 0) != 0)
            return -1;

        index = bc_add_string(
            p,
            command->return_name
        );

        if (index < 0)
            return -1;

        if (bc_emit(
                p,
                USSR_BC_EVAL,
                USSR_BC_RETURN_REG,
                0,
                0,
                (uint32_t)index
            ) < 0)
            return -1;

        return bc_store_result(
            p,
            command->return_name,
            USSR_BC_RETURN_REG,
            0
        );
    }

    /*
     * OOP/external commands.
     *
     * Their command-specific semantic work belongs to host/runtime
     * services, but the compiler still emits explicit bytecode.  There
     * is deliberately no generic "execute command AST" opcode.
     *
     * The runtime ABI distinguishes OOP from external command lookup.
     * At compile time a named unresolved operation is represented by
     * the OOP/native boundary and carries its argument registers.
     */
    if (command->return_name == NULL ||
        count >= USSR_BC_RETURN_REG)
    {
        bc_error(
            command,
            "'%s' has too many parameters (at most %d)",
            command->name,
            USSR_BC_RETURN_REG - 1
        );
        return -1;
    }

    for (i = 0; i < count; ++i)
    {
        /* Block arguments are carried by the OOP source-site metadata;
         * value arguments are materialized in the matching VM register. */
        if (a[i].type == USSR_ARGUMENT_COMMAND_LIST)
            continue;

        if (bc_compile_argument(
                p,
                &a[i],
                (uint8_t)i
            ) != 0)
            return -1;
    }

    index = bc_add_string(
        p,
        command->name
    );

    if (index < 0)
        return -1;

    {
        int instruction;

        instruction = bc_emit(
            p,
            USSR_BC_OOP,
            USSR_BC_RETURN_REG - 1,
            (uint8_t)count,
            (command->next != NULL &&
             strcmp(command->next->name, "chain") == 0) ? 1 : 0,
            (uint32_t)index
        );

        if (instruction < 0)
            return -1;

        if (bc_add_oop_site(
                p,
                (uint32_t)instruction,
                command
            ) != 0)
            return -1;
    }

    {
        int hash_assignment = 0;
        for (i = 0; i < count; ++i)
            hash_assignment |= a[i].assignment != 0;

        return bc_store_result(
            p,
            command->return_name,
            USSR_BC_RETURN_REG - 1,
            hash_assignment
        );
    }
}


static int bc_is_conditional_label(const ussr_command_t *command)
{
    return command != NULL &&
           (strcmp(command->name, "elif") == 0 ||
            strcmp(command->name, "else") == 0);
}

static int bc_is_choose_label(const ussr_command_t *command)
{
    return command != NULL &&
           (strcmp(command->name, "option") == 0 ||
            strcmp(command->name, "default") == 0);
}

static int bc_compile_until(
    ussr_bc_program_t *p,
    const ussr_command_t *start,
    const ussr_command_t *stop,
    bc_loop_t *loop,
    ussr_bc_function_t *current,
    const ussr_command_t **next_out
)
{
    const ussr_command_t *command = start;

    while (command != NULL && command != stop)
    {
        const ussr_command_t *next;
        const ussr_command_t *outer = bc_error_command;
        int failed;

        /* if/choose chains are compiled here rather than through
         * bc_compile_command: give their failures a location too. */
        bc_error_command = command;
        failed = 0;

        if (strcmp(command->name, "if") == 0 &&
            command->argument_count == 1)
        {
            if (bc_compile_conditional_chain(
                    p, command, loop, current, &next) != 0)
                failed = 1;
            else
            {
                bc_error_command = outer;
                command = next;
                continue;
            }
        }

        /*
         * Block-style if followed by elif/else block siblings:
         * the whole chain is compiled as one multi-branch
         * conditional. A lone block-if keeps the historical
         * single-construct path below.
         */
        if (strcmp(command->name, "if") == 0 &&
            command->argument_count == 2 &&
            command->arguments[1].type == USSR_ARGUMENT_COMMAND_LIST &&
            command->next != NULL &&
            bc_is_conditional_label(command->next))
        {
            if (bc_compile_block_chain(
                    p, command, loop, current, &next) != 0)
                failed = 1;
            else
            {
                bc_error_command = outer;
                command = next;
                continue;
            }
        }

        if (strcmp(command->name, "choose") == 0 &&
            command->argument_count == 1)
        {
            if (bc_compile_choose(
                    p, command, loop, current, &next) != 0)
                failed = 1;
            else
            {
                bc_error_command = outer;
                command = next;
                continue;
            }
        }

        if (failed)
        {
            bc_error(command, "cannot compile '%s'", command->name);
            bc_error_command = outer;
            return -1;
        }

        bc_error_command = outer;

        if (bc_is_conditional_label(command) ||
            bc_is_choose_label(command))
        {
            bc_error(
                command,
                "%s without a matching %s before it",
                command->name,
                bc_is_choose_label(command) ? "choose" : "if"
            );
            return -1;
        }

        if (bc_compile_command(p, command, loop, current) != 0)
            return -1;

        next = command->next;

        if (strcmp(command->name, "do") == 0)
        {
            if (next == NULL || strcmp(next->name, "loop") != 0)
            {
                bc_error(
                    command,
                    "do must be followed by loop(_): condition"
                );
                return -1;
            }
            command = next->next;
        }
        else
        {
            if (strcmp(command->name, "loop") == 0)
            {
                bc_error(command, "loop without a do before it");
                return -1;
            }
            command = next;
        }
    }

    if (next_out != NULL)
        *next_out = command;

    return 0;
}

static int bc_compile_conditional_chain(
    ussr_bc_program_t *p,
    const ussr_command_t *first,
    bc_loop_t *loop,
    ussr_bc_function_t *current,
    const ussr_command_t **next_out
)
{
    const ussr_command_t *branch = first;
    int pending_false = -1;
    int *end_jumps = NULL;
    size_t end_count = 0;
    size_t end_capacity = 0;
    int result = -1;

    if (first == NULL ||
        strcmp(first->name, "if") != 0 ||
        first->argument_count != 1)
        return -1;

    if (first->return_name != NULL &&
        bc_store_boolean(p, first->return_name, 0) != 0)
        goto cleanup;

    while (branch != NULL &&
           (branch == first || bc_is_conditional_label(branch)))
    {
        const ussr_command_t *stop = branch->next;
        int jump_end;

        while (stop != NULL && !bc_is_conditional_label(stop))
            stop = stop->next;

        if (strcmp(branch->name, "else") == 0)
        {
            if (branch->argument_count != 0)
            {
                bc_error(branch, "else takes no parameters here");
                goto cleanup;
            }

            if (pending_false >= 0)
            {
                if (bc_patch(p, (size_t)pending_false, p->code_count) != 0)
                    goto cleanup;
                pending_false = -1;
            }
        }
        else
        {
            if (branch->argument_count != 1)
            {
                bc_error(branch, "%s expects one condition", branch->name);
                goto cleanup;
            }

            if (bc_compile_argument(
                    p, &branch->arguments[0], 0) != 0)
                goto cleanup;

            pending_false = bc_emit(
                p, USSR_BC_JMP_FALSE, 0, 0, 0, 0
            );
            if (pending_false < 0)
                goto cleanup;
        }

        if (first->return_name != NULL &&
            bc_store_boolean(p, first->return_name, 1) != 0)
            goto cleanup;

        if (bc_compile_until(
                p, branch->next, stop, loop, current, NULL) != 0)
            goto cleanup;

        jump_end = bc_emit(p, USSR_BC_JMP, 0, 0, 0, 0);
        if (jump_end < 0)
            goto cleanup;

        if (end_count == end_capacity)
        {
            size_t n = end_capacity == 0 ? 4 : end_capacity * 2;
            int *q = realloc(end_jumps, n * sizeof(*q));
            if (q == NULL)
                goto cleanup;
            end_jumps = q;
            end_capacity = n;
        }
        end_jumps[end_count++] = jump_end;

        if (pending_false >= 0)
        {
            if (bc_patch(p, (size_t)pending_false, p->code_count) != 0)
                goto cleanup;
            pending_false = -1;
        }

        branch = stop;
    }

    if (pending_false >= 0)
    {
        if (bc_patch(p, (size_t)pending_false, p->code_count) != 0)
            goto cleanup;
    }

    for (size_t i = 0; i < end_count; ++i)
    {
        if (bc_patch(p, (size_t)end_jumps[i], p->code_count) != 0)
            goto cleanup;
    }

    if (next_out != NULL)
        *next_out = branch;

    result = 0;

cleanup:
    free(end_jumps);
    return result;
}

/*
 * Block-style conditional chain:
 *
 *     if(val): {c1} [ b1 ]
 *     elif(_): {c2} [ b2 ]
 *     else(_): [ b3 ]
 *
 * Unlike the flat chain form, each branch body is a nested command
 * list carried as the branch command's trailing block argument.
 * The `if` keeps its historical form (condition + block); following
 * `elif`/`else` siblings with blocks are consumed here. Returns the
 * first unconsumed command in next_out.
 */
static int bc_compile_block_chain(
    ussr_bc_program_t *p,
    const ussr_command_t *first,
    bc_loop_t *loop,
    ussr_bc_function_t *current,
    const ussr_command_t **next_out
)
{
    const ussr_command_t *branch = first;
    int pending_false = -1;
    int *end_jumps = NULL;
    size_t end_count = 0;
    size_t end_capacity = 0;
    int result = -1;

    if (first == NULL ||
        strcmp(first->name, "if") != 0 ||
        first->argument_count != 2 ||
        first->arguments[1].type != USSR_ARGUMENT_COMMAND_LIST)
        return -1;

    if (first->return_name != NULL &&
        bc_store_boolean(p, first->return_name, 0) != 0)
        goto cleanup;

    for (;;)
    {
        const ussr_argument_t *ba = branch->arguments;
        size_t bcount = branch->argument_count;
        int is_else = strcmp(branch->name, "else") == 0;
        const ussr_command_list_t *body;
        const ussr_command_t *next;
        int jump_end;

        if (is_else)
        {
            /* else(_): [body] — the block is the only argument. */
            if (bcount != 1 ||
                ba[0].type != USSR_ARGUMENT_COMMAND_LIST)
            {
                bc_error(branch, "else expects a single [ ] block");
                goto cleanup;
            }
            body = ba[0].data.command_list;

            if (pending_false >= 0)
            {
                if (bc_patch(p, (size_t)pending_false,
                             p->code_count) != 0)
                    goto cleanup;
                pending_false = -1;
            }
        }
        else
        {
            /* if/elif: (condition, body-block). */
            if (bcount != 2 ||
                ba[1].type != USSR_ARGUMENT_COMMAND_LIST)
            {
                bc_error(
                    branch,
                    "%s expects a condition and a [ ] block",
                    branch->name
                );
                goto cleanup;
            }
            body = ba[1].data.command_list;

            if (bc_compile_argument(p, &ba[0], 0) != 0)
                goto cleanup;

            pending_false = bc_emit(p, USSR_BC_JMP_FALSE, 0, 0, 0, 0);
            if (pending_false < 0)
                goto cleanup;
        }

        if (first->return_name != NULL &&
            bc_store_boolean(p, first->return_name, 1) != 0)
            goto cleanup;

        if (bc_compile_list(p, body, loop, current) != 0)
            goto cleanup;

        jump_end = bc_emit(p, USSR_BC_JMP, 0, 0, 0, 0);
        if (jump_end < 0)
            goto cleanup;

        if (end_count == end_capacity)
        {
            size_t n = end_capacity == 0 ? 4 : end_capacity * 2;
            int *q = realloc(end_jumps, n * sizeof(*q));
            if (q == NULL)
                goto cleanup;
            end_jumps = q;
            end_capacity = n;
        }
        end_jumps[end_count++] = jump_end;

        if (pending_false >= 0)
        {
            if (bc_patch(p, (size_t)pending_false, p->code_count) != 0)
                goto cleanup;
            pending_false = -1;
        }

        next = branch->next;

        if (next == NULL || !bc_is_conditional_label(next))
        {
            branch = next;
            break;
        }

        if (is_else)
        {
            bc_error(next, "%s after else", next->name);
            goto cleanup;
        }

        branch = next;
    }

    for (size_t i = 0; i < end_count; ++i)
    {
        if (bc_patch(p, (size_t)end_jumps[i], p->code_count) != 0)
            goto cleanup;
    }

    if (next_out != NULL)
        *next_out = branch;

    result = 0;

cleanup:
    free(end_jumps);
    return result;
}

typedef struct
{
    const ussr_command_t *label;
    const ussr_command_t *stop;
    int dispatch_jump;
} bc_choice_case_t;

static int bc_compile_choose(
    ussr_bc_program_t *p,
    const ussr_command_t *first,
    bc_loop_t *outer_loop,
    ussr_bc_function_t *current,
    const ussr_command_t **next_out
)
{
    const ussr_command_t *label;
    const ussr_command_t *after;
    bc_choice_case_t *cases = NULL;
    size_t case_count = 0;
    size_t case_capacity = 0;
    int no_match_jump = -1;
    int has_default = 0;
    bc_loop_t choice_loop;
    int result = -1;

    memset(&choice_loop, 0, sizeof(choice_loop));

    if (first == NULL ||
        strcmp(first->name, "choose") != 0 ||
        first->argument_count != 1)
        return -1;

    if (first->return_name != NULL &&
        bc_store_boolean(p, first->return_name, 0) != 0)
        goto cleanup;

    if (bc_compile_argument(p, &first->arguments[0], 0) != 0)
        goto cleanup;

    label = first->next;

    while (label != NULL && bc_is_choose_label(label))
    {
        const ussr_command_t *cursor = label->next;
        bc_choice_case_t *q;

        while (cursor != NULL && !bc_is_choose_label(cursor))
            cursor = cursor->next;

        if (case_count == case_capacity)
        {
            size_t n = case_capacity == 0 ? 4 : case_capacity * 2;
            q = realloc(cases, n * sizeof(*q));
            if (q == NULL)
                goto cleanup;
            cases = q;
            case_capacity = n;
        }

        cases[case_count].label = label;
        cases[case_count].stop = cursor;
        cases[case_count].dispatch_jump = -1;
        ++case_count;

        if (strcmp(label->name, "default") == 0)
        {
            if (label->argument_count != 0 || has_default)
            {
                bc_error(
                    label,
                    has_default ? "choose has a second default"
                                : "default takes no parameters"
                );
                goto cleanup;
            }
            has_default = 1;
        }
        else if (label->argument_count != 1)
        {
            bc_error(label, "option expects one value");
            goto cleanup;
        }

        label = cursor;
    }

    after = label;

    if (case_count == 0)
    {
        bc_error(first, "choose must be followed by option or default");
        goto cleanup;
    }

    for (size_t i = 0; i < case_count; ++i)
    {
        if (strcmp(cases[i].label->name, "option") == 0)
        {
            if (bc_compile_argument(
                    p, &cases[i].label->arguments[0], 1) != 0)
                goto cleanup;

            if (bc_emit(
                    p, USSR_BC_EQ, 2, 0, 1, 0
                ) < 0)
                goto cleanup;

            cases[i].dispatch_jump = bc_emit(
                p, USSR_BC_JMP_TRUE, 2, 0, 0, 0
            );

            if (cases[i].dispatch_jump < 0)
                goto cleanup;
        }
    }

    no_match_jump = bc_emit(
        p, USSR_BC_JMP, 0, 0, 0, 0
    );
    if (no_match_jump < 0)
        goto cleanup;

    for (size_t i = 0; i < case_count; ++i)
    {
        label = cases[i].label;

        if (cases[i].dispatch_jump >= 0)
        {
            if (bc_patch(
                    p, (size_t)cases[i].dispatch_jump, p->code_count) != 0)
                goto cleanup;
        }

        if (strcmp(label->name, "default") == 0)
        {
            if (bc_patch(
                    p, (size_t)no_match_jump, p->code_count) != 0)
                goto cleanup;
        }

        if (first->return_name != NULL &&
            bc_store_boolean(p, first->return_name, 1) != 0)
            goto cleanup;

        if (bc_compile_until(
                p,
                label->next,
                cases[i].stop,
                &choice_loop,
                current,
                NULL
            ) != 0)
            goto cleanup;
    }

    if (!has_default)
    {
        if (bc_patch(
                p, (size_t)no_match_jump, p->code_count) != 0)
            goto cleanup;
    }

    /*
     * choose is breakable, but it is not a loop.  continue() therefore
     * belongs to the enclosing loop.
     */
    for (size_t i = 0; i < choice_loop.continue_count; ++i)
    {
        if (outer_loop == NULL)
        {
            bc_error(first, "continue inside choose but outside a loop");
            goto cleanup;
        }

        if (bc_loop_add(
                &outer_loop->continues,
                &outer_loop->continue_count,
                &outer_loop->continue_capacity,
                choice_loop.continues[i]
            ) != 0)
            goto cleanup;
    }

    for (size_t i = 0; i < choice_loop.break_count; ++i)
    {
        if (bc_patch(
                p, choice_loop.breaks[i], p->code_count) != 0)
            goto cleanup;
    }

    if (next_out != NULL)
        *next_out = after;

    result = 0;

cleanup:
    bc_loop_free(&choice_loop);
    free(cases);
    return result;
}

static int bc_compile_list(
    ussr_bc_program_t *p,
    const ussr_command_list_t *list,
    bc_loop_t *loop,
    ussr_bc_function_t *current
)
{
    const ussr_command_t *next;

    if (list == NULL)
        return 0;

    if (bc_compile_until(
            p,
            list->head,
            NULL,
            loop,
            current,
            &next
        ) != 0)
        return -1;

    (void)next;
    return 0;
}

/*
 * Works out a compiled function's local names: its parameters and return
 * variable, plus every name its own code stores to (and scan
 * destinations), except names declared global in it or at the top level.
 */
static int bc_function_locals(
    ussr_bc_program_t *p,
    ussr_bc_function_t *function,
    const ussr_command_t *definition,
    const ussr_command_list_t *body
)
{
    bc_names_t locals;
    bc_names_t globals;
    int all_global = 0;
    size_t i;
    size_t k;
    int result = -1;

    memset(&locals, 0, sizeof(locals));
    memset(&globals, 0, sizeof(globals));

    if (bc_collect_globals(body, &globals, &all_global) != 0)
        goto done;

    for (i = 0; i < globals.count; ++i)
    {
        int is_parameter = strcmp(globals.items[i], function->return_name) == 0;

        for (k = 0; k < function->parameter_count; ++k)
            if (strcmp(globals.items[i], function->parameters[k]) == 0)
                is_parameter = 1;

        if (is_parameter)
        {
            bc_error(
                definition,
                "'%s' is a parameter or the return variable of '%s' and "
                "cannot be global",
                globals.items[i],
                function->name
            );
            goto done;
        }
    }

    for (k = 0; k < function->parameter_count; ++k)
        if (bc_names_add(&locals, function->parameters[k]) != 0)
            goto done;
    if (bc_names_add(&locals, function->return_name) != 0)
        goto done;

    if (!all_global && !bc_program_all_global)
    {
        for (k = function->entry; k < function->code_end; ++k)
        {
            const char *name;

            if (p->code[k].opcode != USSR_BC_STORE_VAR)
                continue;

            name = p->strings[p->code[k].immediate];

            if (!bc_names_contains(&globals, name) &&
                !bc_names_contains(&bc_program_globals, name) &&
                bc_names_add(&locals, name) != 0)
                goto done;
        }

        for (k = 0; k < p->scan_site_count; ++k)
        {
            const ussr_command_t *scan = p->scan_sites[k].command;

            if (p->scan_sites[k].instruction < function->entry ||
                p->scan_sites[k].instruction >= function->code_end)
                continue;

            for (i = 1; i < scan->argument_count; ++i)
            {
                const char *name =
                    scan->arguments[i].data.expression->data.variable;

                if (!bc_names_contains(&globals, name) &&
                    !bc_names_contains(&bc_program_globals, name) &&
                    bc_names_add(&locals, name) != 0)
                    goto done;
            }
        }
    }

    /* Resolve the function's own variable accesses now, so the VM does
     * not look names up at run time: b = local (slot in c) or global.
     * Slots past 255 do not fit c and stay resolved by name. */
    for (k = function->entry; k < function->code_end; ++k)
    {
        ussr_bc_instruction_t *ins = &p->code[k];
        size_t slot;

        if (ins->opcode != USSR_BC_LOAD_VAR &&
            ins->opcode != USSR_BC_STORE_VAR)
            continue;

        for (slot = 0; slot < locals.count; ++slot)
            if (strcmp(locals.items[slot], p->strings[ins->immediate]) == 0)
                break;

        if (slot == locals.count)
        {
            ins->b = USSR_VAR_GLOBAL;
            ins->c = 0;
        }
        else if (slot <= UINT8_MAX)
        {
            ins->b = USSR_VAR_LOCAL;
            ins->c = (uint8_t)slot;
        }
        else
        {
            ins->b = USSR_VAR_BY_NAME;
            ins->c = 0;
        }
    }

    function->locals = calloc(locals.count, sizeof(*function->locals));
    if (function->locals == NULL)
        goto done;

    for (i = 0; i < locals.count; ++i)
    {
        function->locals[i] = bc_strdup(locals.items[i]);
        if (function->locals[i] == NULL)
            goto done;
        function->local_count = i + 1;
    }

    result = 0;

done:
    bc_names_free(&locals);
    bc_names_free(&globals);
    return result;
}

static int bc_function_is_local(
    const ussr_bc_function_t *function,
    const char *name
)
{
    size_t i;

    for (i = 0; i < function->local_count; ++i)
        if (strcmp(function->locals[i], name) == 0)
            return 1;

    return 0;
}

static int bc_function_is_parameter(
    const ussr_bc_function_t *function,
    const char *name
)
{
    size_t i;

    for (i = 0; i < function->parameter_count; ++i)
        if (strcmp(function->parameters[i], name) == 0)
            return 1;

    return 0;
}

/* The string pool keeps duplicates: map each index to the first index
 * holding the same text. */
static const ussr_bc_program_t *bc_sort_program;

static int bc_compare_string_index(const void *a, const void *b)
{
    size_t x = *(const size_t *)a;
    size_t y = *(const size_t *)b;
    int order = strcmp(bc_sort_program->strings[x],
                       bc_sort_program->strings[y]);

    if (order != 0)
        return order;
    return x < y ? -1 : x > y;
}

/* canon[i] is the first index with the text of string i; *sorted gets
 * the indices in text order, for bc_lookup_string. */
static size_t *bc_canonical_strings(
    const ussr_bc_program_t *p,
    size_t **sorted
)
{
    size_t *order = malloc(p->string_count * sizeof(*order));
    size_t *canon = malloc(p->string_count * sizeof(*canon));
    size_t i;

    *sorted = NULL;

    if (order == NULL || canon == NULL)
    {
        free(order);
        free(canon);
        return NULL;
    }

    for (i = 0; i < p->string_count; ++i)
        order[i] = i;

    bc_sort_program = p;
    qsort(order, p->string_count, sizeof(*order), bc_compare_string_index);

    for (i = 0; i < p->string_count; ++i)
        canon[order[i]] =
            i > 0 && strcmp(p->strings[order[i]], p->strings[order[i - 1]]) == 0
                ? canon[order[i - 1]]
                : order[i];

    *sorted = order;
    return canon;
}

/* The canonical string index with this text, or (size_t)-1. */
static size_t bc_lookup_string(
    const ussr_bc_program_t *p,
    const size_t *sorted,
    const size_t *canon,
    const char *text
)
{
    size_t low = 0;
    size_t high = p->string_count;

    while (low < high)
    {
        size_t middle = low + (high - low) / 2;
        int order = strcmp(p->strings[sorted[middle]], text);

        if (order == 0)
            return canon[sorted[middle]];
        if (order < 0)
            low = middle + 1;
        else
            high = middle;
    }

    return (size_t)-1;
}

/*
 * Warns about the mistakes function locals make likely in code written
 * before them:
 *  - a function reads one of its locals before setting it;
 *  - a function sets a local it never reads while other code reads that
 *    name (a flag or result meant for the caller);
 *  - a name is read as a global but only ever set as a local, so the
 *    read sees nothing (state set in init but not declared global).
 * Warnings never stop compilation.
 */
static void bc_scope_warnings(const ussr_bc_program_t *p)
{
    size_t names = p->string_count;
    unsigned char *global_written;
    size_t *global_read;      /* first global read + 1, or 0 */
    size_t *local_store;      /* first local store + 1, or 0 */
    const ussr_bc_function_t **local_owner;
    unsigned char *seen;
    size_t *canon;
    size_t *sorted = NULL;
    size_t *reads;            /* LOAD_VAR count per name, whole program */
    size_t *local_reads;      /* ... inside the current function */
    size_t *first_set;        /* first store in the current function + 1 */
    size_t *touched;          /* names the current function uses */
    size_t touched_count;
    unsigned char *is_touched;
    const ussr_bc_function_t **owner;  /* per instruction, NULL = top level */
    size_t *scan_at;          /* per instruction: scan site + 1, or 0 */
    size_t f;
    size_t k;
    size_t n;

    if (bc_program_all_global || names == 0)
        return;

    global_written = calloc(names, 1);
    global_read = calloc(names, sizeof(*global_read));
    local_store = calloc(names, sizeof(*local_store));
    local_owner = calloc(names, sizeof(*local_owner));
    seen = calloc(names, 1);
    canon = bc_canonical_strings(p, &sorted);
    reads = calloc(names, sizeof(*reads));
    local_reads = calloc(names, sizeof(*local_reads));
    first_set = calloc(names, sizeof(*first_set));
    touched = malloc(names * sizeof(*touched));
    is_touched = calloc(names, 1);
    owner = calloc(p->code_count + 1, sizeof(*owner));
    scan_at = calloc(p->code_count + 1, sizeof(*scan_at));

    if (global_written == NULL || global_read == NULL ||
        local_store == NULL || local_owner == NULL || seen == NULL ||
        canon == NULL || reads == NULL || local_reads == NULL ||
        first_set == NULL || touched == NULL || is_touched == NULL ||
        owner == NULL || scan_at == NULL)
        goto done;

    for (f = 0; f < p->function_count; ++f)
        for (k = p->functions[f].entry; k < p->functions[f].code_end; ++k)
            owner[k] = &p->functions[f];
    for (k = 0; k < p->scan_site_count; ++k)
        if (p->scan_sites[k].instruction < p->code_count)
            scan_at[p->scan_sites[k].instruction] = k + 1;

    /* Reads of a function's own parameter say nothing about a variable
     * of the same name elsewhere. */
    for (k = 0; k < p->code_count; ++k)
    {
        if (p->code[k].opcode != USSR_BC_LOAD_VAR)
            continue;

        if (owner[k] == NULL || !bc_function_is_parameter(
                owner[k], p->strings[p->code[k].immediate]))
            ++reads[canon[p->code[k].immediate]];
    }

    /* Locals read before they are set, in code order. */
    for (f = 0; f < p->function_count; ++f)
    {
        const ussr_bc_function_t *function = &p->functions[f];

        touched_count = 0;

        for (k = function->entry; k < function->code_end; ++k)
        {
            const ussr_bc_instruction_t *ins = &p->code[k];
            const char *name;

            if (ins->opcode == USSR_BC_SCAN && scan_at[k] != 0)
            {
                const ussr_command_t *scan =
                    p->scan_sites[scan_at[k] - 1].command;
                size_t i;

                for (i = 1; i < scan->argument_count; ++i)
                {
                    n = bc_lookup_string(p, sorted, canon, scan->arguments[i]
                            .data.expression->data.variable);
                    if (n == (size_t)-1)
                        continue;
                    if (!is_touched[n])
                    {
                        is_touched[n] = 1;
                        touched[touched_count++] = n;
                    }
                    seen[n] = 2;
                }
                continue;
            }

            if (ins->opcode != USSR_BC_LOAD_VAR &&
                ins->opcode != USSR_BC_STORE_VAR)
                continue;

            n = canon[ins->immediate];
            name = p->strings[n];

            if (ins->b == USSR_VAR_GLOBAL ||
                (ins->b != USSR_VAR_LOCAL &&
                 !bc_function_is_local(function, name)))
                continue;

            if (!is_touched[n])
            {
                is_touched[n] = 1;
                touched[touched_count++] = n;
            }

            if (ins->opcode == USSR_BC_STORE_VAR)
            {
                if (first_set[n] == 0)
                    first_set[n] = k + 1;
                if (seen[n] == 0)
                    seen[n] = 2;
            }
            else if (local_reads[n]++ == 0 && seen[n] == 0)
            {
                seen[n] = 1;
                if (!bc_function_is_parameter(function, name))
                    bc_warning(
                        bc_code_commands[k],
                        "'%s' is read before it is set in '%s'; it is "
                        "local to '%s' (declare global(_): %s if it is "
                        "shared)",
                        name, function->name, function->name, name
                    );
            }
        }

        for (k = 0; k < touched_count; ++k)
        {
            const char *name;
            const ussr_command_t *setter;

            n = touched[k];
            name = p->strings[n];

            if (first_set[n] == 0 || local_reads[n] != 0 ||
                reads[n] == 0 || strcmp(name, "_") == 0 ||
                strcmp(name, function->return_name) == 0 ||
                bc_function_is_parameter(function, name))
                continue;

            /* Status results of control flow (if(found): ...) and struct
             * type names are routinely left unread. */
            setter = bc_code_commands[first_set[n] - 1];
            if (setter != NULL &&
                (strcmp(setter->name, "struct") == 0 ||
                 strcmp(setter->name, "if") == 0 ||
                 strcmp(setter->name, "elif") == 0 ||
                 strcmp(setter->name, "else") == 0 ||
                 strcmp(setter->name, "while") == 0 ||
                 strcmp(setter->name, "do") == 0 ||
                 strcmp(setter->name, "for") == 0 ||
                 strcmp(setter->name, "choose") == 0 ||
                 strcmp(setter->name, "break") == 0 ||
                 strcmp(setter->name, "continue") == 0))
                continue;

            bc_warning(
                bc_code_commands[first_set[n] - 1],
                "'%s' is set in '%s' but never read there, while other "
                "code reads it; it is local to '%s' (declare global(_): %s "
                "if it is shared)",
                name, function->name, function->name, name
            );
        }

        for (k = 0; k < touched_count; ++k)
        {
            n = touched[k];
            is_touched[n] = 0;
            seen[n] = 0;
            local_reads[n] = 0;
            first_set[n] = 0;
        }
    }

    /* Global reads and writes, and where each name is set as a local. */
    for (k = 0; k < p->code_count; ++k)
    {
        const ussr_bc_instruction_t *ins = &p->code[k];
        const ussr_bc_function_t *function;
        int local;

        if (ins->opcode != USSR_BC_LOAD_VAR &&
            ins->opcode != USSR_BC_STORE_VAR)
            continue;

        n = canon[ins->immediate];
        function = owner[k];
        local = function != NULL &&
                (ins->b == USSR_VAR_LOCAL ||
                 (ins->b != USSR_VAR_GLOBAL &&
                  bc_function_is_local(function, p->strings[n])));

        if (ins->opcode == USSR_BC_LOAD_VAR)
        {
            if (!local && global_read[n] == 0)
                global_read[n] = k + 1;
        }
        else if (!local)
            global_written[n] = 1;
        else if (local_store[n] == 0)
        {
            local_store[n] = k + 1;
            local_owner[n] = function;
        }
    }

    for (k = 0; k < p->scan_site_count; ++k)
    {
        const ussr_command_t *scan = p->scan_sites[k].command;
        const ussr_bc_function_t *function =
            p->scan_sites[k].instruction < p->code_count
                ? owner[p->scan_sites[k].instruction] : NULL;
        size_t i;

        for (i = 1; i < scan->argument_count; ++i)
        {
            const char *name =
                scan->arguments[i].data.expression->data.variable;

            n = bc_lookup_string(p, sorted, canon, name);
            if (n != (size_t)-1 &&
                (function == NULL || !bc_function_is_local(function, name)))
                global_written[n] = 1;
        }
    }

    /* Parameters are locals that are set by every call. */
    for (f = 0; f < p->function_count; ++f)
        for (k = 0; k < p->functions[f].parameter_count; ++k)
        {
            n = bc_lookup_string(
                p, sorted, canon, p->functions[f].parameters[k]);
            if (n != (size_t)-1 && local_owner[n] == NULL)
                local_owner[n] = &p->functions[f];
        }

    for (n = 0; n < names; ++n)
    {
        const ussr_command_t *where;

        if (canon[n] != n || global_read[n] == 0 || global_written[n] ||
            local_owner[n] == NULL)
            continue;

        where = local_store[n] != 0 ? bc_code_commands[local_store[n] - 1]
                                    : NULL;

        if (where != NULL && where->source_file != NULL)
            bc_warning(
                bc_code_commands[global_read[n] - 1],
                "'%s' is read as a global but only set as a local (in "
                "'%s', line %d); declare global(_): %s",
                p->strings[n], local_owner[n]->name, where->source_line,
                p->strings[n]
            );
        else
            bc_warning(
                bc_code_commands[global_read[n] - 1],
                "'%s' is read as a global but only set as a local (in "
                "'%s'); declare global(_): %s",
                p->strings[n], local_owner[n]->name, p->strings[n]
            );
    }

done:
    free(global_written);
    free(global_read);
    free(local_store);
    free((void *)local_owner);
    free(seen);
    free(canon);
    free(reads);
    free(local_reads);
    free(first_set);
    free(sorted);
    free(touched);
    free(is_touched);
    free((void *)owner);
    free(scan_at);
}

static int bc_compile_functions(
    ussr_bc_program_t *p,
    const ussr_command_list_t *list
)
{
    const ussr_command_t *command;
    size_t i;
    ussr_bc_function_t *function;

    if (list == NULL)
        return 0;

    for (command = list->head;
         command != NULL;
         command = command->next)
    {
        if (bc_is_definition(command))
        {
            const ussr_command_list_t *body;

            function = bc_find_function(
                p,
                command->name
            );

            if (function == NULL)
            {
                bc_error(command, "function '%s' was not collected",
                         command->name);
                return -1;
            }

            function->entry =
                (uint32_t)p->code_count;

            body = command->arguments[
                command->argument_count - 1
            ].data.command_list;

            if (bc_compile_list(
                    p,
                    body,
                    NULL,
                    function
                ) != 0)
            {
                bc_error(command, "cannot compile function '%s'",
                         function->name);
                return -1;
            }

            /*
             * Implicit function fall-through returns null.
             */
            {
                ussr_value_t null_value;
                int constant;

                null_value = ussr_null();
                constant = bc_add_constant(
                    p,
                    &null_value
                );
                ussr_value_free(&null_value);

                if (constant < 0)
                {
                    bc_error(command, "out of memory compiling '%s'",
                             function->name);
                    return -1;
                }

                if (bc_emit(
                        p,
                        USSR_BC_LOAD_CONST,
                        USSR_BC_RETURN_REG,
                        0,
                        0,
                        (uint32_t)constant
                    ) < 0)
                {
                    bc_error(command, "out of memory compiling '%s'",
                             function->name);
                    return -1;
                }
            }

            if (bc_emit(
                    p,
                    USSR_BC_RET,
                    0,
                    0,
                    0,
                    0
                ) < 0)
            {
                bc_error(command, "out of memory compiling '%s'",
                         function->name);
                return -1;
            }

            function->code_end = (uint32_t)p->code_count;

            if (bc_function_locals(p, function, command, body) != 0)
                return -1;

            if (bc_compile_functions(
                    p,
                    body
                ) != 0)
            {
                /* The nested function's failure is already reported. */
                return -1;
            }
        }
        else
        {
            for (i = 0;
                 i < command->argument_count;
                 ++i)
            {
                if (command->arguments[i].type ==
                    USSR_ARGUMENT_COMMAND_LIST)
                {
                    if (bc_compile_functions(
                            p,
                            command->arguments[i]
                                .data.command_list
                        ) != 0)
                        return -1;
                }
            }
        }
    }

    return 0;
}


static int uuasm_compile_source(
    const ussr_command_list_t *source,
    ussr_uuasm_program_t *program
)
{
    bc_loop_t loop;
    int skip_functions;

    if (source == NULL || program == NULL)
        return -1;

    memset(program, 0, sizeof(*program));
    memset(&loop, 0, sizeof(loop));
    bc_error_command = NULL;
    bc_error_reported = 0;
    bc_names_free(&bc_program_globals);
    bc_program_all_global = 0;

    if (bc_collect_globals(
            source, &bc_program_globals, &bc_program_all_global) != 0)
        return -1;

    /*
     * First pass over declarations creates stable function-table
     * indices.  Bodies are compiled afterward.
     */
    if (bc_collect_functions(
            program,
            source
        ) != 0)
    {
        ussr_bc_program_free(program);
        return -2;
    }

    /*
     * Main entry point jumps over all function bodies.
     */
    skip_functions = bc_emit(
        program,
        USSR_BC_JMP,
        0,
        0,
        0,
        0
    );

    if (skip_functions < 0)
    {
        ussr_bc_program_free(program);
        return -3;
    }

    if (bc_compile_functions(
            program,
            source
        ) != 0)
    {
        ussr_bc_program_free(program);
        return -4;
    }

    if (bc_patch(
            program,
            (size_t)skip_functions,
            program->code_count
        ) != 0)
    {
        ussr_bc_program_free(program);
        return -1;
    }

    /* The top level is not a loop: break/continue there is an error
     * (it used to jump back to the start of the program). */
    if (bc_compile_list(
            program,
            source,
            NULL,
            NULL
        ) != 0)
    {
        bc_loop_free(&loop);
        ussr_bc_program_free(program);
        return -1;
    }

    bc_loop_free(&loop);

    if (bc_emit(
            program,
            USSR_BC_HALT,
            0,
            0,
            0,
            0
        ) < 0)
    {
        ussr_bc_program_free(program);
        return -5;
    }

    bc_scope_warnings(program);
    bc_names_free(&bc_program_globals);

    return 0;
}

int ussr_uuasm_compile(
    const ussr_command_list_t *source,
    ussr_uuasm_program_t *program
)
{
    return uuasm_compile_source(source, program);
}

int ussr_uuasm_compile_to_bytecode(
    const ussr_command_list_t *source,
    ussr_bc_program_t *program
)
{
    ussr_uuasm_program_t uuasm;
    ussr_bc_instruction_t *vm_code;
    size_t i;

    if (source == NULL || program == NULL)
        return -1;

    memset(program, 0, sizeof(*program));
    if (ussr_uuasm_compile(source, &uuasm) != 0)
        return -1;

    vm_code = malloc(uuasm.code_count * sizeof(*vm_code));
    if (vm_code == NULL && uuasm.code_count != 0)
    {
        ussr_bc_program_free(&uuasm);
        return -1;
    }

    for (i = 0; i < uuasm.code_count; ++i)
    {
        uint8_t opcode;
        if (ussr_uuasm_to_vm_opcode(uuasm.code[i].opcode, &opcode) != 0)
        {
            bc_error(NULL, "UUASM opcode 0x%02x has no VM mapping",
                     uuasm.code[i].opcode);
            free(vm_code);
            ussr_bc_program_free(&uuasm);
            return -1;
        }
        vm_code[i] = uuasm.code[i];
        vm_code[i].opcode = opcode;
    }

    *program = uuasm;
    free(uuasm.code);
    program->code = vm_code;
    return 0;
}

int ussr_bc_compile(
    const ussr_command_list_t *source,
    ussr_bc_program_t *program
)
{
    return ussr_uuasm_compile_to_bytecode(source, program);
}

void ussr_bc_program_free(
    ussr_bc_program_t *program
)
{
    size_t i;

    if (program == NULL)
        return;

    free(program->code);

    for (i = 0; i < program->constant_count; ++i)
        ussr_value_free(&program->constants[i]);

    free(program->constants);

    for (i = 0; i < program->string_count; ++i)
        free(program->strings[i]);

    free(program->strings);

    for (i = 0; i < program->function_count; ++i)
    {
        free(program->functions[i].name);
        free(program->functions[i].return_name);

        if (program->functions[i].parameters != NULL)
        {
            for (size_t j = 0;
                 j < program->functions[i].parameter_count;
                 ++j)
                free(program->functions[i].parameters[j]);
        }

        free(program->functions[i].parameters);

        for (size_t j = 0; j < program->functions[i].local_count; ++j)
            free(program->functions[i].locals[j]);
        free(program->functions[i].locals);
    }

    free(program->functions);
    free(program->oop_sites);
    free(program->scan_sites);

    memset(program, 0, sizeof(*program));
}

int ussr_bc_function_default_locals(ussr_bc_function_t *function)
{
    size_t i;
    size_t count;

    if (function == NULL || function->return_name == NULL)
        return -1;

    count = function->parameter_count + 1;
    function->locals = calloc(count, sizeof(*function->locals));
    if (function->locals == NULL)
        return -1;

    for (i = 0; i < count; ++i)
    {
        function->locals[i] = bc_strdup(
            i < function->parameter_count
                ? function->parameters[i]
                : function->return_name);
        if (function->locals[i] == NULL)
            return -1;
        function->local_count = i + 1;
    }

    return 0;
}

static const char *bc_opcode_name(
    uint8_t opcode
)
{
    switch (opcode)
    {
        case USSR_BC_NOP: return "NOP";
        case USSR_BC_LOAD_CONST: return "LOAD_CONST";
        case USSR_BC_LOAD_VAR: return "LOAD_VAR";
        case USSR_BC_STORE_VAR: return "STORE_VAR";
        case USSR_BC_LOAD_HASH: return "LOAD_HASH";
        case USSR_BC_STORE_HASH: return "STORE_HASH";
        case USSR_BC_DECODE_UNO: return "DECODE_UNO";
        case USSR_BC_GET: return "GET";
        case USSR_BC_VECTOR_GET: return "VECTOR_GET";
        case USSR_BC_VECTOR_SET: return "VECTOR_SET";

        case USSR_BC_ADD: return "ADD";
        case USSR_BC_SUB: return "SUB";
        case USSR_BC_MUL: return "MUL";
        case USSR_BC_DIV: return "DIV";
        case USSR_BC_MOD: return "MOD";
        case USSR_BC_NEG: return "NEG";
        case USSR_BC_EQ: return "EQ";
        case USSR_BC_NE: return "NE";
        case USSR_BC_LT: return "LT";
        case USSR_BC_LE: return "LE";
        case USSR_BC_GT: return "GT";
        case USSR_BC_GE: return "GE";
        case USSR_BC_LAND: return "LAND";
        case USSR_BC_LOR: return "LOR";
        case USSR_BC_XOR: return "XOR";
        case USSR_BC_BAND: return "BAND";
        case USSR_BC_BOR: return "BOR";
        case USSR_BC_SHL: return "SHL";
        case USSR_BC_SHR: return "SHR";

        case USSR_BC_JMP: return "JMP";
        case USSR_BC_JMP_TRUE: return "JMP_TRUE";
        case USSR_BC_JMP_FALSE: return "JMP_FALSE";

        case USSR_BC_SET: return "SET";
        case USSR_BC_PRINT: return "PRINT";
        case USSR_BC_CONCAT: return "CONCAT";
        case USSR_BC_EXTERNAL: return "EXTERNAL";
        case USSR_BC_OOP: return "OOP";
        case USSR_BC_EVAL: return "EVAL";
        case USSR_BC_RANDOM64: return "RANDOM64";
        case USSR_BC_SEED64: return "SEED64";
        case USSR_BC_SCAN: return "SCAN";
        case USSR_BC_TIME: return "TIME";
        case USSR_BC_CHAIN: return "CHAIN";
        case USSR_BC_FILE: return "FILE";
        case USSR_BC_CD: return "CD";
        case USSR_BC_TEMPLATE: return "TEMPLATE";

        case USSR_BC_CALL: return "CALL";
        case USSR_BC_RET: return "RET";
        case USSR_BC_RETURN: return "RETURN";
        case USSR_BC_HALT: return "HALT";
        default: return "UNKNOWN";
    }
}

static void bc_print_value(
    const ussr_value_t *value
)
{
    if (value == NULL)
    {
        fputs("null", stdout);
        return;
    }

    switch (value->type)
    {
        case USSR_NULL:
            fputs("null", stdout);
            break;

        case USSR_INTEGER:
            printf(USSR_INT_FMT, value->data.integer);
            break;

        case USSR_REAL:
            printf("%.17g", value->data.real);
            break;

        case USSR_STRING:
            printf("\"%s\"", value->data.string);
            break;

        case USSR_BOOLEAN:
            fputs(
                value->data.boolean ? "true" : "false",
                stdout
            );
            break;

        case USSR_VECTOR:
            fputs("<vector>", stdout);
            break;

        case USSR_STRUCT:
            fputs("<struct>", stdout);
            break;

        default:
            fputs("<?>", stdout);
            break;
    }
}

void ussr_bc_dump(
    const ussr_bc_program_t *program
)
{
    size_t i;

    if (program == NULL)
        return;

    printf(
        "USSR bytecode\n"
        "instructions: %zu\n"
        "constants:    %zu\n"
        "strings:      %zu\n"
        "functions:    %zu\n\n",
        program->code_count,
        program->constant_count,
        program->string_count,
        program->function_count
    );

    for (i = 0; i < program->function_count; ++i)
    {
        const ussr_bc_function_t *f =
            &program->functions[i];

        printf(
            "function %s -> %s(",
            f->name,
            f->return_name
        );

        for (size_t j = 0;
             j < f->parameter_count;
             ++j)
        {
            if (j != 0)
                fputc(',', stdout);

            fputs(f->parameters[j], stdout);
        }

        printf(
            ") entry=%u\n",
            f->entry
        );
    }

    putchar('\n');

    for (i = 0; i < program->code_count; ++i)
    {
        const ussr_bc_instruction_t *ins =
            &program->code[i];

        printf(
            "%06zu  %-12s %u %u %u %u",
            i,
            bc_opcode_name(ins->opcode),
            (unsigned)ins->a,
            (unsigned)ins->b,
            (unsigned)ins->c,
            (unsigned)ins->immediate
        );

        if (ins->opcode == USSR_BC_LOAD_CONST &&
            ins->immediate < program->constant_count)
        {
            fputs("  ; ", stdout);
            bc_print_value(
                &program->constants[ins->immediate]
            );
        }
        else if ((ins->opcode == USSR_BC_LOAD_VAR ||
                  ins->opcode == USSR_BC_STORE_VAR ||
                  ins->opcode == USSR_BC_LOAD_HASH ||
                  ins->opcode == USSR_BC_STORE_HASH ||
                  ins->opcode == USSR_BC_EVAL ||
                  ins->opcode == USSR_BC_OOP) &&
                 ins->immediate < program->string_count)
        {
            printf(
                "  ; \"%s\"",
                program->strings[ins->immediate]
            );
        }

        putchar('\n');
    }
}

/*
 * Simple portable file container.
 *
 * Header:
 *   magic[8] = "USSRBC01"
 *   version  = 1
 *   instruction_count
 *   constant_count
 *   string_count
 *   function_count
 *
 * Values are encoded without pointers.  Vector/struct values are not
 * serialized by this first container version because their binary
 * layout belongs to UNO/runtime and is not a stable compiler ABI yet.
 */
static int bc_write_u32(
    FILE *fp,
    uint32_t value
)
{
    unsigned char b[4];

    b[0] = (unsigned char)(value & 0xffU);
    b[1] = (unsigned char)((value >> 8) & 0xffU);
    b[2] = (unsigned char)((value >> 16) & 0xffU);
    b[3] = (unsigned char)((value >> 24) & 0xffU);

    return fwrite(b, 1, 4, fp) == 4 ? 0 : -1;
}

static int bc_write_string(
    FILE *fp,
    const char *s
)
{
    size_t n;

    if (s == NULL)
        return bc_write_u32(fp, 0);

    n = strlen(s);

    if (n > UINT32_MAX)
        return -1;

    if (bc_write_u32(fp, (uint32_t)n) != 0)
        return -1;

    return fwrite(s, 1, n, fp) == n ? 0 : -1;
}

static int bc_write_value(
    FILE *fp,
    const ussr_value_t *value
)
{
    uint8_t type;

    if (value == NULL)
        return -1;

    type = (uint8_t)value->type;

    if (fwrite(&type, 1, 1, fp) != 1)
        return -1;

    switch (value->type)
    {
        case USSR_NULL:
            return 0;

        case USSR_INTEGER:
        {
            int64_t x = (int64_t)value->data.integer;

            return fwrite(&x, sizeof(x), 1, fp) == 1 ?
                0 : -1;
        }

        case USSR_REAL:
            return fwrite(
                &value->data.real,
                sizeof(value->data.real),
                1,
                fp
            ) == 1 ? 0 : -1;

        case USSR_STRING:
            return bc_write_string(
                fp,
                value->data.string
            );

        case USSR_BOOLEAN:
        {
            uint8_t x =
                value->data.boolean ? 1U : 0U;

            return fwrite(&x, 1, 1, fp) == 1 ?
                0 : -1;
        }

        case USSR_VECTOR:
        case USSR_STRUCT:
            /*
             * Host objects are intentionally excluded from this stable
             * container until UNO defines a portable serialization ABI.
             */
            return -1;

        default:
            return -1;
    }
}

int ussr_bc_write_file(
    const ussr_bc_program_t *program,
    const char *path
)
{
    FILE *fp;
    size_t i;

    if (program == NULL || path == NULL)
        return -1;

    fp = fopen(path, "wb");

    if (fp == NULL)
        return -1;

    if (fwrite(
            "USSRBC01",
            1,
            8,
            fp
        ) != 8 ||
        bc_write_u32(fp, 1) != 0 ||
        bc_write_u32(fp, (uint32_t)program->code_count) != 0 ||
        bc_write_u32(fp, (uint32_t)program->constant_count) != 0 ||
        bc_write_u32(fp, (uint32_t)program->string_count) != 0 ||
        bc_write_u32(fp, (uint32_t)program->function_count) != 0)
    {
        fclose(fp);
        return -1;
    }

    for (i = 0; i < program->code_count; ++i)
    {
        if (fwrite(
                &program->code[i],
                sizeof(program->code[i]),
                1,
                fp
            ) != 1)
        {
            fclose(fp);
            return -1;
        }
    }

    for (i = 0; i < program->constant_count; ++i)
    {
        if (bc_write_value(
                fp,
                &program->constants[i]
            ) != 0)
        {
            fclose(fp);
            return -1;
        }
    }

    for (i = 0; i < program->string_count; ++i)
    {
        if (bc_write_string(
                fp,
                program->strings[i]
            ) != 0)
        {
            fclose(fp);
            return -1;
        }
    }

    for (i = 0; i < program->function_count; ++i)
    {
        const ussr_bc_function_t *f =
            &program->functions[i];

        if (bc_write_string(fp, f->name) != 0 ||
            bc_write_string(fp, f->return_name) != 0 ||
            bc_write_u32(
                fp,
                (uint32_t)f->parameter_count
            ) != 0 ||
            bc_write_u32(fp, f->entry) != 0)
        {
            fclose(fp);
            return -1;
        }

        for (size_t j = 0;
             j < f->parameter_count;
             ++j)
        {
            if (bc_write_string(
                    fp,
                    f->parameters[j]
                ) != 0)
            {
                fclose(fp);
                return -1;
            }
        }
    }

    if (fclose(fp) != 0)
        return -1;

    return 0;
}
