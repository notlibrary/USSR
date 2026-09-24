#include "ussr_bytecode.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <limits.h>


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
    if (strcmp(name, "if") == 0 ||
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
                fprintf(
                    stderr,
                    "USSR compiler: duplicate function '%s'\n",
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
                        fprintf(
                            stderr,
                            "USSR compiler: function '%s' "
                            "parameter %zu must be an identifier\n",
                            command->name,
                            j
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

static int bc_emit_binary(
    ussr_bc_program_t *p,
    ussr_operator_t op,
    uint8_t dst,
    uint8_t left,
    uint8_t right
)
{
    uint8_t opcode;

    switch (op)
    {
        case USSR_OP_ADD:          opcode = USSR_BC_ADD;  break;
        case USSR_OP_SUB:          opcode = USSR_BC_SUB;  break;
        case USSR_OP_MUL:          opcode = USSR_BC_MUL;  break;
        case USSR_OP_DIV:          opcode = USSR_BC_DIV;  break;
        case USSR_OP_MOD:          opcode = USSR_BC_MOD;  break;
        case USSR_OP_EQ:           opcode = USSR_BC_EQ;   break;
        case USSR_OP_NE:           opcode = USSR_BC_NE;   break;
        case USSR_OP_LT:           opcode = USSR_BC_LT;   break;
        case USSR_OP_LE:           opcode = USSR_BC_LE;   break;
        case USSR_OP_GT:           opcode = USSR_BC_GT;   break;
        case USSR_OP_GE:           opcode = USSR_BC_GE;   break;
        case USSR_OP_LOGICAL_AND:  opcode = USSR_BC_LAND; break;
        case USSR_OP_LOGICAL_OR:   opcode = USSR_BC_LOR;  break;
        case USSR_OP_SHIFT_LEFT:   opcode = USSR_BC_SHL;  break;
        case USSR_OP_SHIFT_RIGHT:  opcode = USSR_BC_SHR;  break;
        case USSR_OP_BITWISE_XOR:  opcode = USSR_BC_XOR;  break;
        case USSR_OP_BITWISE_AND:  opcode = USSR_BC_BAND; break;
        case USSR_OP_BITWISE_OR:   opcode = USSR_BC_BOR;  break;
        default:
            return -1;
    }

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

            fprintf(
                stderr,
                "USSR compiler: unsupported unary operator\n"
            );
            return -1;

        case USSR_EXPR_BINARY:
            left_reg = (uint8_t)(dst + 1);
            right_reg = (uint8_t)(dst + 2);

            if (right_reg >= USSR_BC_RETURN_REG)
            {
                fprintf(
                    stderr,
                    "USSR compiler: expression register pressure exceeded\n"
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
                fprintf(
                    stderr,
                    "USSR compiler: hash lookup requires an identifier\n"
                );
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
            fprintf(
                stderr,
                "USSR compiler: command block cannot be used as a value\n"
            );
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

static int bc_compile_command(
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
            return -1;

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
if (strcmp(command->name, "do") == 0) {
    bc_loop_t child;
    size_t body_start;
    size_t cond_start;
    size_t end;
    size_t n;
    int jump_false;
    const ussr_argument_t *block_arg = NULL;
    const ussr_argument_t *loop_arg = NULL;

    memset(&child, 0, sizeof(child));
    if (count != 2) return -1;

    // Корректно определяем, где блок, а где команда loop
    if (a[0].type == USSR_ARGUMENT_COMMAND_LIST && a[0].data.command_list != NULL &&
        a[0].data.command_list->head != NULL && strcmp(a[0].data.command_list->head->name, "loop") == 0) {
        loop_arg = &a[0];
        block_arg = &a[1];
    } else if (a[1].type == USSR_ARGUMENT_COMMAND_LIST && a[1].data.command_list != NULL &&
               a[1].data.command_list->head != NULL && strcmp(a[1].data.command_list->head->name, "loop") == 0) {
        block_arg = &a[0];
        loop_arg = &a[1];
    } else {
        return -1;
    }

    if (block_arg->type != USSR_ARGUMENT_COMMAND_LIST) return -1;
    if (loop_arg->data.command_list->head->argument_count < 1) return -1; // Защита от отсутствия условия

    // 1. Точка начала тела цикла
    body_start = p->code_count;
    if (bc_compile_list(p, block_arg->data.command_list, &child, current) != 0) goto do_error;

    // 2. Точка вычисления условия (сюда прыгают все 'continue')
    cond_start = p->code_count;
    if (bc_compile_argument(p, &loop_arg->data.command_list->head->arguments[0], 0) != 0) goto do_error;

    // 3. Проверка условия. Если FALSE -> прыгаем на end.
    // Если ваша ВМ требует очистки значения из стека, убедитесь, что JMP_FALSE "съедает" его.
    jump_false = bc_emit(p, USSR_BC_JMP_FALSE, 0, 0, 0, 0);
    if (jump_false < 0) goto do_error;

    // 4. Если TRUE -> прыгаем обратно на body_start
    if (bc_emit(p, USSR_BC_JMP, 0, 0, 0, (uint32_t)body_start) < 0) goto do_error;

    // 5. Точка выхода из цикла (сюда прыгают 'break' и JMP_FALSE)
    end = p->code_count;

    // Патчим все 'continue' на cond_start (вычисление условия)
    for (n = 0; n < child.continue_count; ++n) {
        if (bc_patch(p, child.continues[n], cond_start) != 0) goto do_error;
    }

    // Патчим выход по несовпадению условия
    if (bc_patch(p, (size_t)jump_false, end) != 0) goto do_error;

    // Патчим все 'break' на end
    for (n = 0; n < child.break_count; ++n) {
        if (bc_patch(p, child.breaks[n], end) != 0) goto do_error;
    }

    bc_loop_free(&child);

    // Запись возвращаемого значения, если необходимо
    if (command->return_name != NULL) {
        if (bc_store_boolean(p, command->return_name, 1) != 0) return -1;
    }

    return 0;

do_error:
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
            return -1;

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

        if (loop == NULL || count != 1)
            return -1;

        if (bc_compile_argument(p, &a[0], 0) != 0)
            return -1;

        if (command->return_name != NULL &&
            bc_store_result(p, command->return_name, 0, a[0].assignment) != 0)
            return -1;

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
		if (current == NULL || count != 1)
			return -1;

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
            fprintf(
                stderr,
                "USSR compiler: %s expects %zu parameter%s\n",
                command->name,
                function->parameter_count,
                function->parameter_count == 1 ? "" : "s"
            );
            return -1;
        }

        if (count >= USSR_BC_RETURN_REG)
            return -1;

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

    /*
     * set
     */
    if (strcmp(command->name, "set") == 0)
    {
        if (count != 1 ||
            command->return_name == NULL)
            return -1;

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
            return -1;

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
            return -1;

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
            return -1;

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

    /*
     * get: get(a): b means b = a.
     */
    if (strcmp(command->name, "get") == 0)
    {
        const ussr_expression_t *expression;
        int source_index;
        int destination_index;

        if (count != 1 || command->return_name == NULL ||
            a[0].type != USSR_ARGUMENT_EXPRESSION ||
            a[0].data.expression == NULL ||
            a[0].data.expression->type != USSR_EXPR_VARIABLE)
            return -1;

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
            return -1;
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
            return -1;
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
            return -1;
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
            return -1;

        for (i = 1; i < count; ++i)
        {
            if (a[i].type != USSR_ARGUMENT_EXPRESSION ||
                a[i].data.expression == NULL ||
                a[i].data.expression->type != USSR_EXPR_VARIABLE)
                return -1;
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
            return -1;

        for (i = 1; i < count; ++i)
        {
            if (a[i].type == USSR_ARGUMENT_COMMAND_LIST)
                return -1;

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
            return -1;

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
            return -1;

        if (bc_emit(p, USSR_BC_CHAIN, 0, 0, 0, 0) < 0)
            return -1;

        return bc_store_result(p, command->return_name, 0, 0);
    }

    /* file: terminate a chain by writing its captured output to a file. */
    if (strcmp(command->name, "file") == 0)
    {
        if (count != 1 || command->return_name == NULL)
            return -1;
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
            return -1;

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
        return -1;

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

static int bc_compile_list(
    ussr_bc_program_t *p,
    const ussr_command_list_t *list,
    bc_loop_t *loop,
    ussr_bc_function_t *current
)
{
    const ussr_command_t *command;

    if (list == NULL)
        return 0;

    for (command = list->head;
         command != NULL;
         command = command->next)
    {
        if (bc_compile_command(
                p,
                command,
                loop,
                current
            ) != 0)
            return -1;
    }

    return 0;
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
                fprintf(stderr, "USSR compiler error: could not find function definition for '%s'\n", command->name);
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
                fprintf(stderr, "USSR compiler error: failed to compile body of function '%s'\n", function->name);
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
                    fprintf(stderr, "USSR compiler error: failed to add null constant in function '%s'\n", function->name);
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
                    fprintf(stderr, "USSR compiler error: failed to emit implicit return value for function '%s'\n", function->name);
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
                fprintf(stderr, "USSR compiler error: failed to emit RET instruction for function '%s'\n", function->name);
                return -1;
            }

            if (bc_compile_functions(
                    p,
                    body
                ) != 0)
            {
                // Ошибка во вложенных функциях уже распечатает свое имя ниже, 
                // но можно также пометить, внутри какой функции это произошло:
                fprintf(stderr, "USSR compiler error: failed to compile nested functions inside '%s'\n", function->name);
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


int ussr_bc_compile(
    const ussr_command_list_t *source,
    ussr_bc_program_t *program
)
{
    bc_loop_t loop;
    int skip_functions;

    if (source == NULL || program == NULL)
        return -1;

    memset(program, 0, sizeof(*program));
    memset(&loop, 0, sizeof(loop));

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

    if (bc_compile_list(
            program,
            source,
            &loop,
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

    return 0;
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

        for (size_t j = 0;
             j < program->functions[i].parameter_count;
             ++j)
            free(program->functions[i].parameters[j]);

        free(program->functions[i].parameters);
    }

    free(program->functions);
    free(program->oop_sites);
    free(program->scan_sites);

    memset(program, 0, sizeof(*program));
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
            printf("%ld", value->data.integer);
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
