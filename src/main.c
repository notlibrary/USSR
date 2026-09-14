#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include "bestline.h"
#include "ussr.h"
#include "pp.h"

int yyparse(void);

typedef struct yy_buffer_state *YY_BUFFER_STATE;
extern YY_BUFFER_STATE yy_scan_string(const char *str);
extern void yy_delete_buffer(YY_BUFFER_STATE buffer);
extern ussr_command_list_t *ussr_parsed_program;

/* ussr_argument_evaluate() is already implemented by the runtime. */
extern int ussr_argument_evaluate(
    const ussr_argument_t *argument,
    ussr_value_t *result
);

/* ------------------------------------------------------------------------- */
/* USSR VIRTUAL MACHINE                                                      */
/*                                                                           */
/* The parser creates the temporary AST. The compiler consumes it once and   */
/* produces a flat instruction stream. The VM never executes an AST/list.   */
/* User definitions are compiled into VM functions; there is deliberately   */
/* no call to ussr_execute_user_definition().                                */
/* ------------------------------------------------------------------------- */

#define USSR_VM_REGISTER_COUNT 32U
#define USSR_VM_MAX_CODE       1000000U
#define USSR_VM_MAX_STEPS      10000000UL
#define USSR_VM_MAX_CALLS      1024U

#define USSR_VM_RETURN_REG     31U
#define USSR_VM_ARG_BASE       0U

#define USSR_VM_FLAG_FUNCTION  0x01U

/* Bytecode opcodes. Keep these stable: they become part of the VM ABI. */
typedef enum
{
    USSR_VM_NOP = 0x00,
    USSR_VM_COMMAND = 0x01,
    USSR_VM_EVAL_ARG = 0x02,
    USSR_VM_STORE_VAR = 0x03,
    USSR_VM_SET_BOOL = 0x04,
    USSR_VM_CALL = 0x05,
    USSR_VM_RET = 0x06,

    USSR_VM_JMP = 0x10,
    USSR_VM_JMP_TRUE = 0x11,
    USSR_VM_JMP_FALSE = 0x12,

    USSR_VM_BREAK = 0x20,
    USSR_VM_CONTINUE = 0x21,
    USSR_VM_RETURN = 0x22,

    USSR_VM_HALT = 0xf0
} ussr_vm_opcode_t;

typedef struct
{
    uint8_t opcode;
    uint8_t a;
    uint8_t b;
    uint8_t c;
    uint32_t immediate;
} ussr_vm_instruction_t;

typedef struct
{
    char *name;
    char *return_name;
    char **parameter_names;
    size_t parameter_count;
    size_t entry;
} ussr_vm_function_t;

typedef struct
{
    ussr_vm_instruction_t *code;
    size_t code_count;
    size_t code_capacity;

    const void **refs;
    size_t ref_count;
    size_t ref_capacity;

    ussr_vm_function_t *functions;
    size_t function_count;
    size_t function_capacity;
} ussr_vm_program_t;

typedef struct
{
    ussr_value_t registers[USSR_VM_REGISTER_COUNT];
    uint32_t ip;
    uint32_t sp;
    uint32_t fp;
    int running;
    int exit_code;
    unsigned long steps;
} ussr_vm_t;

typedef struct
{
    uint32_t return_ip;
    uint32_t return_register;
    uint32_t function_index;
    ussr_value_t *saved_registers;
    ussr_value_t *saved_variables;
    unsigned char *saved_variable_existed;
    size_t saved_variable_count;
} ussr_vm_frame_t;

typedef struct
{
    size_t *breaks;
    size_t break_count;
    size_t break_capacity;
    size_t *continues;
    size_t continue_count;
    size_t continue_capacity;
} ussr_vm_loop_t;

static char *vm_strdup(const char *text)
{
    size_t length;
    char *copy;

    if (text == NULL)
        return NULL;

    length = strlen(text);
    copy = malloc(length + 1);
    if (copy == NULL)
        return NULL;

    memcpy(copy, text, length + 1);
    return copy;
}

static void vm_function_free(ussr_vm_function_t *function)
{
    size_t i;

    if (function == NULL)
        return;

    free(function->name);
    free(function->return_name);

    for (i = 0; i < function->parameter_count; ++i)
        free(function->parameter_names[i]);

    free(function->parameter_names);
    memset(function, 0, sizeof(*function));
}

static void vm_program_free(ussr_vm_program_t *program)
{
    size_t i;

    if (program == NULL)
        return;

    free(program->code);
    free((void *)program->refs);

    for (i = 0; i < program->function_count; ++i)
        vm_function_free(&program->functions[i]);

    free(program->functions);
    memset(program, 0, sizeof(*program));
}

static int vm_ref(
    ussr_vm_program_t *program,
    const void *object)
{
    const void **new_refs;
    size_t new_capacity;

    if (program == NULL || object == NULL)
        return -1;

    if (program->ref_count == program->ref_capacity)
    {
        new_capacity = program->ref_capacity == 0 ? 64 :
                       program->ref_capacity * 2;
        new_refs = realloc(
            (void *)program->refs,
            new_capacity * sizeof(*new_refs)
        );
        if (new_refs == NULL)
            return -1;

        program->refs = new_refs;
        program->ref_capacity = new_capacity;
    }

    program->refs[program->ref_count] = object;
    return (int)program->ref_count++;
}

static const void *vm_ref_get(
    const ussr_vm_program_t *program,
    uint32_t index)
{
    if (program == NULL || index >= program->ref_count)
        return NULL;

    return program->refs[index];
}

static int vm_emit(
    ussr_vm_program_t *program,
    uint8_t opcode,
    uint8_t a,
    uint8_t b,
    uint8_t c,
    uint32_t immediate)
{
    ussr_vm_instruction_t *new_code;
    size_t new_capacity;

    if (program == NULL || program->code_count >= USSR_VM_MAX_CODE)
        return -1;

    if (program->code_count == program->code_capacity)
    {
        new_capacity = program->code_capacity == 0 ? 256 :
                       program->code_capacity * 2;

        if (new_capacity > USSR_VM_MAX_CODE)
            new_capacity = USSR_VM_MAX_CODE;

        new_code = realloc(
            program->code,
            new_capacity * sizeof(*new_code)
        );
        if (new_code == NULL)
            return -1;

        program->code = new_code;
        program->code_capacity = new_capacity;
    }

    program->code[program->code_count].opcode = opcode;
    program->code[program->code_count].a = a;
    program->code[program->code_count].b = b;
    program->code[program->code_count].c = c;
    program->code[program->code_count].immediate = immediate;

    return (int)program->code_count++;
}

static int vm_emit_ref(
    ussr_vm_program_t *program,
    uint8_t opcode,
    uint8_t a,
    const void *object)
{
    int ref;

    ref = vm_ref(program, object);
    if (ref < 0)
        return -1;

    return vm_emit(program, opcode, a, 0, 0, (uint32_t)ref);
}

static int vm_patch_jump(
    ussr_vm_program_t *program,
    size_t instruction,
    size_t target)
{
    if (program == NULL || instruction >= program->code_count ||
        target > UINT32_MAX)
        return -1;

    program->code[instruction].immediate = (uint32_t)target;
    return 0;
}

static int vm_loop_push(
    size_t **items,
    size_t *count,
    size_t *capacity,
    size_t value)
{
    size_t *new_items;
    size_t new_capacity;

    if (*count == *capacity)
    {
        new_capacity = *capacity == 0 ? 8 : *capacity * 2;
        new_items = realloc(*items, new_capacity * sizeof(*new_items));
        if (new_items == NULL)
            return -1;

        *items = new_items;
        *capacity = new_capacity;
    }

    (*items)[(*count)++] = value;
    return 0;
}

static void vm_loop_free(ussr_vm_loop_t *loop)
{
    if (loop == NULL)
        return;

    free(loop->breaks);
    free(loop->continues);
    memset(loop, 0, sizeof(*loop));
}

static int vm_is_definition(const ussr_command_t *command)
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

static int vm_add_function(
    ussr_vm_program_t *program,
    const ussr_command_t *command)
{
    ussr_vm_function_t *function;
    ussr_vm_function_t *new_functions;
    size_t parameter_count;
    size_t i;
    size_t new_capacity;

    if (program == NULL || command == NULL || !vm_is_definition(command))
        return -1;

    for (i = 0; i < program->function_count; ++i)
    {
        if (strcmp(program->functions[i].name, command->name) == 0)
        {
            fprintf(stderr,
                    "USSR VM: function '%s' already defined\n",
                    command->name);
            return -1;
        }
    }

    if (program->function_count == program->function_capacity)
    {
        new_capacity = program->function_capacity == 0 ? 16 :
                       program->function_capacity * 2;

        new_functions = realloc(
            program->functions,
            new_capacity * sizeof(*new_functions)
        );
        if (new_functions == NULL)
            return -1;

        program->functions = new_functions;
        program->function_capacity = new_capacity;
    }

    function = &program->functions[program->function_count];
    memset(function, 0, sizeof(*function));

    parameter_count = command->argument_count - 1;
    function->name = vm_strdup(command->name);
    function->return_name = vm_strdup(command->return_name);
    function->parameter_count = parameter_count;

    if (function->name == NULL || function->return_name == NULL)
        goto error;

    if (parameter_count != 0)
    {
        function->parameter_names = calloc(
            parameter_count,
            sizeof(*function->parameter_names)
        );
        if (function->parameter_names == NULL)
            goto error;

        for (i = 0; i < parameter_count; ++i)
        {
            const ussr_argument_t *argument = &command->arguments[i];

            if (argument->type != USSR_ARGUMENT_EXPRESSION ||
                argument->data.expression == NULL ||
                argument->data.expression->type != USSR_EXPR_VARIABLE)
            {
                fprintf(stderr,
                        "USSR VM: function '%s' parameter must be identifier\n",
                        command->name);
                goto error;
            }

            function->parameter_names[i] = vm_strdup(
                argument->data.expression->data.variable
            );
            if (function->parameter_names[i] == NULL)
                goto error;
        }
    }

    ++program->function_count;
    return 0;

error:
    vm_function_free(function);
    return -1;
}

static ussr_vm_function_t *vm_find_function(
    ussr_vm_program_t *program,
    const char *name)
{
    size_t i;

    if (program == NULL || name == NULL)
        return NULL;

    for (i = 0; i < program->function_count; ++i)
    {
        if (strcmp(program->functions[i].name, name) == 0)
            return &program->functions[i];
    }

    return NULL;
}

static int vm_collect_functions(
    ussr_vm_program_t *program,
    ussr_command_list_t *list)
{
    ussr_command_t *command;

    if (program == NULL || list == NULL)
        return 0;

    command = list->head;
    while (command != NULL)
    {
        size_t i;

        if (vm_is_definition(command))
        {
            if (vm_add_function(program, command) != 0)
                return -1;

            if (vm_collect_functions(
                    program,
                    command->arguments[
                        command->argument_count - 1
                    ].data.command_list) != 0)
                return -1;
        }
        else
        {
            for (i = 0; i < command->argument_count; ++i)
            {
                if (command->arguments[i].type == USSR_ARGUMENT_COMMAND_LIST &&
                    vm_collect_functions(
                        program,
                        command->arguments[i].data.command_list) != 0)
                    return -1;
            }
        }

        command = command->next;
    }

    return 0;
}

static int vm_compile_list(
    ussr_vm_program_t *program,
    ussr_command_list_t *list,
    ussr_vm_loop_t *loop,
    ussr_vm_function_t *current_function);

static int vm_compile_command(
    ussr_vm_program_t *program,
    ussr_command_t *command,
    ussr_vm_loop_t *loop,
    ussr_vm_function_t *current_function)
{
    ussr_argument_t *args;
    size_t count;
    int jump_false;
    int jump_end;
    size_t i;

    if (program == NULL || command == NULL)
        return -1;

    args = command->arguments;
    count = command->argument_count;

    if (vm_is_definition(command))
        return 0;

    if (strcmp(command->name, "if") == 0)
    {
        if (count < 2 || count > 3 ||
            args[1].type != USSR_ARGUMENT_COMMAND_LIST ||
            (count == 3 && args[2].type != USSR_ARGUMENT_COMMAND_LIST))
        {
            fprintf(stderr, "USSR VM: invalid if command\n");
            return -1;
        }

        if (vm_emit_ref(program, USSR_VM_EVAL_ARG, 0, &args[0]) < 0)
            return -1;

        jump_false = vm_emit(
            program, USSR_VM_JMP_FALSE, 0, 0, 0, 0
        );
        if (jump_false < 0)
            return -1;

        if (vm_compile_list(
                program,
                args[1].data.command_list,
                loop,
                current_function) != 0)
            return -1;

        if (count == 3)
        {
            jump_end = vm_emit(program, USSR_VM_JMP, 0, 0, 0, 0);
            if (jump_end < 0)
                return -1;

            if (vm_patch_jump(
                    program, (size_t)jump_false, program->code_count) != 0)
                return -1;

            if (vm_compile_list(
                    program,
                    args[2].data.command_list,
                    loop,
                    current_function) != 0)
                return -1;

            if (vm_patch_jump(
                    program, (size_t)jump_end, program->code_count) != 0)
                return -1;
        }
        else if (vm_patch_jump(
                    program, (size_t)jump_false, program->code_count) != 0)
        {
            return -1;
        }

        if (command->return_name != NULL)
        {
            if (vm_emit_ref(
                    program, USSR_VM_SET_BOOL, 0, command) < 0)
                return -1;
        }

        return 0;
    }

    if (strcmp(command->name, "while") == 0)
    {
        ussr_vm_loop_t child_loop;
        size_t condition_ip;
        size_t jump_end;

        if (count != 2 || args[1].type != USSR_ARGUMENT_COMMAND_LIST)
        {
            fprintf(stderr, "USSR VM: invalid while command\n");
            return -1;
        }

        memset(&child_loop, 0, sizeof(child_loop));
        condition_ip = program->code_count;

        if (vm_emit_ref(program, USSR_VM_EVAL_ARG, 0, &args[0]) < 0)
        {
            vm_loop_free(&child_loop);
            return -1;
        }

        jump_end = (size_t)vm_emit(
            program, USSR_VM_JMP_FALSE, 0, 0, 0, 0
        );
        if (jump_end >= program->code_count)
        {
            vm_loop_free(&child_loop);
            return -1;
        }

        if (vm_compile_list(
                program,
                args[1].data.command_list,
                &child_loop,
                current_function) != 0)
        {
            vm_loop_free(&child_loop);
            return -1;
        }

        for (i = 0; i < child_loop.continue_count; ++i)
        {
            if (vm_patch_jump(
                    program,
                    child_loop.continues[i],
                    condition_ip) != 0)
            {
                vm_loop_free(&child_loop);
                return -1;
            }
        }

        if (vm_emit(
                program, USSR_VM_JMP, 0, 0, 0,
                (uint32_t)condition_ip) < 0)
        {
            vm_loop_free(&child_loop);
            return -1;
        }

        if (vm_patch_jump(
                program, jump_end, program->code_count) != 0)
        {
            vm_loop_free(&child_loop);
            return -1;
        }

        for (i = 0; i < child_loop.break_count; ++i)
        {
            if (vm_patch_jump(
                    program,
                    child_loop.breaks[i],
                    program->code_count) != 0)
            {
                vm_loop_free(&child_loop);
                return -1;
            }
        }

        vm_loop_free(&child_loop);

        if (command->return_name != NULL)
        {
            if (vm_emit_ref(
                    program, USSR_VM_SET_BOOL, 0, command) < 0)
                return -1;
        }

        return 0;
    }

    if (strcmp(command->name, "break") == 0)
    {
        int instruction;

        if (loop == NULL)
        {
            fprintf(stderr, "USSR VM: break outside loop\n");
            return -1;
        }

        instruction = vm_emit(program, USSR_VM_BREAK, 0, 0, 0, 0);
        if (instruction < 0)
            return -1;

        return vm_loop_push(
            &loop->breaks,
            &loop->break_count,
            &loop->break_capacity,
            (size_t)instruction
        );
    }

    if (strcmp(command->name, "continue") == 0)
    {
        int instruction;

        if (loop == NULL)
        {
            fprintf(stderr, "USSR VM: continue outside loop\n");
            return -1;
        }

        instruction = vm_emit(program, USSR_VM_CONTINUE, 0, 0, 0, 0);
        if (instruction < 0)
            return -1;

        return vm_loop_push(
            &loop->continues,
            &loop->continue_count,
            &loop->continue_capacity,
            (size_t)instruction
        );
    }

    if (strcmp(command->name, "return") == 0)
    {
        if (current_function == NULL || count != 1)
        {
            fprintf(stderr, "USSR VM: return outside function\n");
            return -1;
        }

        if (vm_emit_ref(program, USSR_VM_EVAL_ARG, USSR_VM_RETURN_REG,
                        &args[0]) < 0)
            return -1;

        if (vm_emit_ref(program, USSR_VM_STORE_VAR, USSR_VM_RETURN_REG,
                        current_function->return_name) < 0)
            return -1;

        if (vm_emit(program, USSR_VM_RET, 0, 0, 0, 0) < 0)
            return -1;

        return 0;
    }

    {
        ussr_vm_function_t *function = vm_find_function(program, command->name);

        if (function != NULL)
        {
            int assignment_index = -1;

            if (count != function->parameter_count)
            {
                fprintf(stderr,
                        "USSR VM: %s expects %zu parameter%s\n",
                        command->name,
                        function->parameter_count,
                        function->parameter_count == 1 ? "" : "s");
                return -1;
            }

            for (i = 0; i < count; ++i)
            {
                if (args[i].assignment)
                {
                    if (assignment_index >= 0)
                    {
                        fprintf(stderr,
                                "USSR VM: a command may have only one ! assignment\n");
                        return -1;
                    }
                    assignment_index = (int)i;
                }

                if (i >= USSR_VM_REGISTER_COUNT)
                {
                    fprintf(stderr, "USSR VM: too many function arguments\n");
                    return -1;
                }

                if (vm_emit_ref(
                        program,
                        USSR_VM_EVAL_ARG,
                        (uint8_t)i,
                        &args[i]) < 0)
                    return -1;
            }

            if (vm_emit(
                    program,
                    USSR_VM_CALL,
                    USSR_VM_ARG_BASE,
                    (uint8_t)count,
                    USSR_VM_RETURN_REG,
                    (uint32_t)(function - program->functions)) < 0)
                return -1;

            if (vm_emit_ref(
                    program,
                    USSR_VM_STORE_VAR,
                    USSR_VM_RETURN_REG,
                    command->return_name) < 0)
                return -1;

            /* ! hash assignment is handled by the runtime command ABI. */
            (void)assignment_index;
            return 0;
        }
    }

    if (vm_emit_ref(program, USSR_VM_COMMAND, 0, command) < 0)
        return -1;

    return 0;
}

static int vm_compile_list(
    ussr_vm_program_t *program,
    ussr_command_list_t *list,
    ussr_vm_loop_t *loop,
    ussr_vm_function_t *current_function)
{
    ussr_command_t *command;

    if (program == NULL || list == NULL)
        return 0;

    command = list->head;
    while (command != NULL)
    {
        if (vm_compile_command(
                program, command, loop, current_function) != 0)
            return -1;

        command = command->next;
    }

    return 0;
}

static int vm_compile_functions(
    ussr_vm_program_t *program,
    ussr_command_list_t *list)
{
    ussr_command_t *command;

    if (program == NULL || list == NULL)
        return 0;

    command = list->head;
    while (command != NULL)
    {
        if (vm_is_definition(command))
        {
            ussr_vm_function_t *function =
                vm_find_function(program, command->name);

            if (function == NULL)
                return -1;

            function->entry = program->code_count;

            if (vm_compile_list(
                    program,
                    command->arguments[
                        command->argument_count - 1
                    ].data.command_list,
                    NULL,
                    function) != 0)
                return -1;

            if (vm_emit(program, USSR_VM_RET, 0, 0, 0, 0) < 0)
                return -1;

            /* Nested definitions are compiled as independent VM functions. */
            if (vm_compile_functions(
                    program,
                    command->arguments[
                        command->argument_count - 1
                    ].data.command_list) != 0)
                return -1;
        }
        else
        {
            size_t i;

            for (i = 0; i < command->argument_count; ++i)
            {
                if (command->arguments[i].type == USSR_ARGUMENT_COMMAND_LIST &&
                    vm_compile_functions(
                        program,
                        command->arguments[i].data.command_list) != 0)
                    return -1;
            }
        }

        command = command->next;
    }

    return 0;
}

static int vm_compile(
    ussr_command_list_t *source,
    ussr_vm_program_t *bytecode)
{
    ussr_vm_loop_t top_loop;

    if (source == NULL || bytecode == NULL)
        return -1;

    memset(bytecode, 0, sizeof(*bytecode));
    memset(&top_loop, 0, sizeof(top_loop));

    /* Pass 1: discover every function so calls can be forward references. */
    if (vm_collect_functions(bytecode, source) != 0)
    {
        vm_loop_free(&top_loop);
        vm_program_free(bytecode);
        return -1;
    }

    /* Pass 2: compile executable top-level code. Definitions emit nothing. */
    if (vm_compile_list(bytecode, source, &top_loop, NULL) != 0)
    {
        vm_loop_free(&top_loop);
        vm_program_free(bytecode);
        return -1;
    }

    vm_loop_free(&top_loop);

    if (vm_emit(bytecode, USSR_VM_HALT, 0, 0, 0, 0) < 0)
    {
        vm_program_free(bytecode);
        return -1;
    }

    /* Pass 3: append function bodies after HALT. */
    if (vm_compile_functions(bytecode, source) != 0)
    {
        vm_program_free(bytecode);
        return -1;
    }

    return 0;
}

static int vm_truthy(const ussr_value_t *value)
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
        default:
            return 1;
    }
}

static void vm_init(ussr_vm_t *vm)
{
    size_t i;

    memset(vm, 0, sizeof(*vm));
    for (i = 0; i < USSR_VM_REGISTER_COUNT; ++i)
        vm->registers[i] = ussr_null();
}

static void vm_cleanup(ussr_vm_t *vm)
{
    size_t i;

    if (vm == NULL)
        return;

    for (i = 0; i < USSR_VM_REGISTER_COUNT; ++i)
        ussr_value_free(&vm->registers[i]);
}

static void vm_frame_free(ussr_vm_frame_t *frame)
{
    size_t i;

    if (frame == NULL)
        return;

    if (frame->saved_registers != NULL)
    {
        for (i = 0; i < USSR_VM_REGISTER_COUNT; ++i)
            ussr_value_free(&frame->saved_registers[i]);
    }

    if (frame->saved_variables != NULL)
    {
        for (i = 0; i < frame->saved_variable_count; ++i)
            ussr_value_free(&frame->saved_variables[i]);
    }

    free(frame->saved_registers);
    free(frame->saved_variables);
    free(frame->saved_variable_existed);
    memset(frame, 0, sizeof(*frame));
}

static int vm_push_frame(
    ussr_vm_t *vm,
    ussr_vm_frame_t **frames,
    size_t *frame_count,
    size_t *frame_capacity,
    const ussr_vm_function_t *function,
    const ussr_vm_instruction_t *instruction,
    uint32_t function_index)
{
    ussr_vm_frame_t *frame;
    ussr_vm_frame_t *new_frames;
    size_t i;
    size_t variable_count;
    size_t new_capacity;
    const ussr_value_t *value;

    if (*frame_count >= USSR_VM_MAX_CALLS)
    {
        fprintf(stderr, "USSR VM: call stack overflow\n");
        return -1;
    }

    if (*frame_count == *frame_capacity)
    {
        new_capacity = *frame_capacity == 0 ? 16 : *frame_capacity * 2;
        if (new_capacity > USSR_VM_MAX_CALLS)
            new_capacity = USSR_VM_MAX_CALLS;

        new_frames = realloc(
            *frames,
            new_capacity * sizeof(*new_frames)
        );
        if (new_frames == NULL)
            return -1;

        *frames = new_frames;
        *frame_capacity = new_capacity;
    }

    frame = &(*frames)[*frame_count];
    memset(frame, 0, sizeof(*frame));

    frame->return_ip = vm->ip;
    frame->return_register = instruction->c;
    frame->function_index = function_index;

    frame->saved_registers = calloc(
        USSR_VM_REGISTER_COUNT,
        sizeof(*frame->saved_registers)
    );
    if (frame->saved_registers == NULL)
        goto error;

    for (i = 0; i < USSR_VM_REGISTER_COUNT; ++i)
        frame->saved_registers[i] = ussr_value_copy(&vm->registers[i]);

    variable_count = function->parameter_count + 1;
    frame->saved_variable_count = variable_count;
    frame->saved_variables = calloc(
        variable_count,
        sizeof(*frame->saved_variables)
    );
    frame->saved_variable_existed = calloc(
        variable_count,
        sizeof(*frame->saved_variable_existed)
    );
    if (frame->saved_variables == NULL ||
        frame->saved_variable_existed == NULL)
        goto error;

    for (i = 0; i < function->parameter_count; ++i)
    {
        value = ussr_get_variable(function->parameter_names[i]);
        if (value != NULL)
        {
            frame->saved_variable_existed[i] = 1;
            frame->saved_variables[i] = ussr_value_copy(value);
        }
    }

    value = ussr_get_variable(function->return_name);
    if (value != NULL)
    {
        frame->saved_variable_existed[function->parameter_count] = 1;
        frame->saved_variables[function->parameter_count] =
            ussr_value_copy(value);
    }

    ++*frame_count;
    return 0;

error:
    vm_frame_free(frame);
    return -1;
}

static int vm_call(
    ussr_vm_t *vm,
    const ussr_vm_program_t *program,
    ussr_vm_frame_t **frames,
    size_t *frame_count,
    size_t *frame_capacity,
    const ussr_vm_instruction_t *instruction)
{
    const ussr_vm_function_t *function;
    size_t function_index;
    size_t i;
    ussr_value_t value;

    function_index = instruction->immediate;
    if (function_index >= program->function_count ||
        instruction->a + instruction->b > USSR_VM_REGISTER_COUNT)
        return -1;

    function = &program->functions[function_index];

    if (instruction->b != function->parameter_count)
    {
        fprintf(stderr,
                "USSR VM: %s expects %zu parameter%s\n",
                function->name,
                function->parameter_count,
                function->parameter_count == 1 ? "" : "s");
        return -1;
    }

    /* Save caller state before binding the callee. */
    if (vm_push_frame(
            vm,
            frames,
            frame_count,
            frame_capacity,
            function,
            instruction,
            (uint32_t)function_index) != 0)
        return -1;

    for (i = 0; i < function->parameter_count; ++i)
    {
        value = ussr_value_copy(
            &vm->registers[instruction->a + i]
        );

        if (ussr_set_variable(
                function->parameter_names[i],
                &value) != 0)
        {
            ussr_value_free(&value);
            return -1;
        }

        ussr_value_free(&value);
    }

    /* The callee gets a fresh register file. */
    for (i = 0; i < USSR_VM_REGISTER_COUNT; ++i)
    {
        ussr_value_free(&vm->registers[i]);
        vm->registers[i] = ussr_null();
    }

    vm->ip = (uint32_t)function->entry;
    return 0;
}

static int vm_return(
    ussr_vm_t *vm,
    const ussr_vm_program_t *program,
    ussr_vm_frame_t *frames,
    size_t *frame_count)
{
    ussr_vm_frame_t *frame;
    const ussr_vm_function_t *function;
    ussr_value_t result;
    size_t i;

    if (*frame_count == 0)
    {
        fprintf(stderr, "USSR VM: return without call frame\n");
        return -1;
    }

    frame = &frames[*frame_count - 1];
    if (frame->function_index >= program->function_count)
        return -1;

    function = &program->functions[frame->function_index];

    {
        const ussr_value_t *value =
            ussr_get_variable(function->return_name);

        if (value == NULL)
            result = ussr_null();
        else
            result = ussr_value_copy(value);
    }

    /* Restore the caller's variables. */
    for (i = 0; i < function->parameter_count + 1; ++i)
    {
        const char *name =
            i < function->parameter_count ?
                function->parameter_names[i] :
                function->return_name;

        if (frame->saved_variable_existed[i])
        {
            if (ussr_set_variable(name, &frame->saved_variables[i]) != 0)
            {
                ussr_value_free(&result);
                return -1;
            }
        }
        else
        {
            /* No public variable-delete API exists yet. Reset to null. */
            ussr_value_t null_value = ussr_null();
            if (ussr_set_variable(name, &null_value) != 0)
            {
                ussr_value_free(&result);
                return -1;
            }
        }
    }

    /* Restore caller registers. */
    for (i = 0; i < USSR_VM_REGISTER_COUNT; ++i)
    {
        ussr_value_free(&vm->registers[i]);
        vm->registers[i] = frame->saved_registers[i];
        frame->saved_registers[i] = ussr_null();
    }

    vm->ip = frame->return_ip;

    if (frame->return_register < USSR_VM_REGISTER_COUNT)
    {
        ussr_value_free(&vm->registers[frame->return_register]);
        vm->registers[frame->return_register] = result;
    }
    else
    {
        ussr_value_free(&result);
    }

    vm_frame_free(frame);
    --*frame_count;
    return 0;
}

static int vm_execute(
    ussr_vm_t *vm,
    const ussr_vm_program_t *program)
{
    ussr_vm_frame_t *frames = NULL;
    size_t frame_count = 0;
    size_t frame_capacity = 0;
    int result;

    while (vm->running)
    {
        ussr_vm_instruction_t instruction;
        ussr_value_t value;
        ussr_command_t *command;

        if (vm->ip >= program->code_count)
        {
            fprintf(stderr, "USSR VM: instruction pointer out of range\n");
            result = -1;
            goto done;
        }

        if (++vm->steps > USSR_VM_MAX_STEPS)
        {
            fprintf(stderr, "USSR VM: execution step limit exceeded\n");
            result = -1;
            goto done;
        }

        instruction = program->code[vm->ip++];

        switch (instruction.opcode)
        {
            case USSR_VM_NOP:
                break;

            case USSR_VM_EVAL_ARG:
                if (instruction.a >= USSR_VM_REGISTER_COUNT)
                {
                    result = -1;
                    goto done;
                }

                value = ussr_null();
                if (ussr_argument_evaluate(
                        (const ussr_argument_t *)vm_ref_get(
                            program, instruction.immediate),
                        &value) != 0)
                {
                    ussr_value_free(&value);
                    result = -1;
                    goto done;
                }

                ussr_value_free(&vm->registers[instruction.a]);
                vm->registers[instruction.a] = value;
                break;

            case USSR_VM_STORE_VAR:
            {
                const char *name =
                    (const char *)vm_ref_get(program, instruction.immediate);

                if (name == NULL || instruction.a >= USSR_VM_REGISTER_COUNT)
                {
                    result = -1;
                    goto done;
                }

                if (ussr_set_variable(
                        name,
                        &vm->registers[instruction.a]) != 0)
                {
                    result = -1;
                    goto done;
                }
                break;
            }

            case USSR_VM_COMMAND:
                command = (ussr_command_t *)vm_ref_get(
                    program, instruction.immediate);
                if (command == NULL)
                {
                    result = -1;
                    goto done;
                }

                result = ussr_execute_command(
                    command->name,
                    command->return_name,
                    command->arguments,
                    command->argument_count
                );
                if (result != 0)
                    goto done;
                break;

            case USSR_VM_CALL:
                if (vm_call(
                        vm,
                        program,
                        &frames,
                        &frame_count,
                        &frame_capacity,
                        &instruction) != 0)
                {
                    result = -1;
                    goto done;
                }
                break;

            case USSR_VM_RET:
                if (frame_count == 0)
                {
                    result = -1;
                    goto done;
                }

                if (vm_return(
                        vm,
                        program,
                        frames,
                        &frame_count) != 0)
                {
                    result = -1;
                    goto done;
                }
                break;

            case USSR_VM_SET_BOOL:
                command = (ussr_command_t *)vm_ref_get(
                    program, instruction.immediate);
                if (command == NULL)
                {
                    result = -1;
                    goto done;
                }

                value = ussr_boolean(
                    vm_truthy(&vm->registers[instruction.a])
                );
                if (command->return_name != NULL &&
                    ussr_set_variable(command->return_name, &value) != 0)
                {
                    ussr_value_free(&value);
                    result = -1;
                    goto done;
                }
                ussr_value_free(&value);
                break;

            case USSR_VM_JMP:
                if (instruction.immediate >= program->code_count)
                {
                    result = -1;
                    goto done;
                }
                vm->ip = instruction.immediate;
                break;

            case USSR_VM_JMP_TRUE:
                if (instruction.a >= USSR_VM_REGISTER_COUNT ||
                    instruction.immediate >= program->code_count)
                {
                    result = -1;
                    goto done;
                }
                if (vm_truthy(&vm->registers[instruction.a]))
                    vm->ip = instruction.immediate;
                break;

            case USSR_VM_JMP_FALSE:
                if (instruction.a >= USSR_VM_REGISTER_COUNT ||
                    instruction.immediate >= program->code_count)
                {
                    result = -1;
                    goto done;
                }
                if (!vm_truthy(&vm->registers[instruction.a]))
                    vm->ip = instruction.immediate;
                break;

            case USSR_VM_BREAK:
            case USSR_VM_CONTINUE:
                /* These are patched to jumps before execution. */
                if (instruction.immediate >= program->code_count)
                {
                    result = -1;
                    goto done;
                }
                vm->ip = instruction.immediate;
                break;

            case USSR_VM_RETURN:
                /* Kept only for bytecode compatibility; compiler emits RET. */
                if (frame_count == 0)
                {
                    result = -1;
                    goto done;
                }
                if (vm_return(
                        vm, program, frames, &frame_count) != 0)
                {
                    result = -1;
                    goto done;
                }
                break;

            case USSR_VM_HALT:
                vm->running = 0;
                vm->exit_code = 0;
                break;

            default:
                fprintf(stderr,
                        "USSR VM: unknown opcode 0x%02x\n",
                        instruction.opcode);
                result = -1;
                goto done;
        }
    }

    result = vm->exit_code;

done:
    while (frame_count > 0)
    {
        vm_frame_free(&frames[frame_count - 1]);
        --frame_count;
    }
    free(frames);
    return result;
}

/* ------------------------------------------------------------------------- */
/* SOURCE INPUT                                                              */
/* ------------------------------------------------------------------------- */

static int append_source(
    char **source,
    size_t *source_length,
    size_t *capacity,
    const char *text)
{
    size_t text_length;
    size_t required;
    size_t new_capacity;
    char *new_source;

    if (text == NULL)
        return 0;

    text_length = strlen(text);
    required = *source_length + text_length + 1;

    if (required <= *capacity)
    {
        memcpy(*source + *source_length, text, text_length);
        *source_length += text_length;
        (*source)[*source_length] = '\0';
        return 0;
    }

    new_capacity = *capacity == 0 ? 256 : *capacity;

    while (new_capacity < required)
        new_capacity *= 2;

    new_source = realloc(*source, new_capacity);

    if (new_source == NULL)
    {
        fprintf(stderr, "USSR: out of memory\n");
        return -1;
    }

    *source = new_source;
    *capacity = new_capacity;

    memcpy(*source + *source_length, text, text_length);
    *source_length += text_length;
    (*source)[*source_length] = '\0';

    return 0;
}

static int count_brackets(
    const char *text,
    int bracket_depth)
{
    size_t i;
    int in_string = 0;
    int escaped = 0;

    if (text == NULL)
        return bracket_depth;

    for (i = 0; text[i] != '\0'; ++i)
    {
        char c = text[i];

        if (in_string)
        {
            if (escaped)
            {
                escaped = 0;
                continue;
            }

            if (c == '\\')
            {
                escaped = 1;
                continue;
            }

            if (c == '"')
                in_string = 0;

            continue;
        }

        if (c == '"')
        {
            in_string = 1;
            continue;
        }

        if (c == '[')
            ++bracket_depth;
        else if (c == ']' && bracket_depth > 0)
            --bracket_depth;
    }

    return bracket_depth;
}

static int parse_and_execute(const char *source)
{
    YY_BUFFER_STATE buffer;
    int result;
    ussr_vm_program_t bytecode;
    ussr_vm_t vm;

    if (source == NULL || source[0] == '\0')
        return 0;

    buffer = yy_scan_string(source);

    if (buffer == NULL)
    {
        fprintf(stderr,
                "USSR: could not create parser buffer\n");
        return -1;
    }

    ussr_parsed_program = NULL;
    result = yyparse();
    yy_delete_buffer(buffer);

    if (result != 0)
    {
        ussr_parsed_program = NULL;
        return -1;
    }

    if (ussr_parsed_program == NULL)
        return 0;

    /* AST -> bytecode happens once. Execution starts only after compilation. */
    if (vm_compile(ussr_parsed_program, &bytecode) != 0)
    {
        ussr_command_list_free(ussr_parsed_program);
        ussr_parsed_program = NULL;
        return -1;
    }

    vm_init(&vm);
    vm.running = 1;
    result = vm_execute(&vm, &bytecode);
    vm_cleanup(&vm);
    vm_program_free(&bytecode);

    ussr_command_list_free(ussr_parsed_program);
    ussr_parsed_program = NULL;

    return result;
}

static int run_file(const char *filename)
{
    ussr_preprocessor_t pp;
    int result;

    if (ussr_pp_init(&pp) != 0)
    {
        fprintf(stderr,
                "USSR: could not initialize preprocessor\n");
        return EXIT_FAILURE;
    }

    result = ussr_pp_process_file(&pp, filename);

    if (result != 0)
    {
        ussr_pp_cleanup(&pp);
        return EXIT_FAILURE;
    }

    result = parse_and_execute(ussr_pp_output(&pp));
    ussr_pp_cleanup(&pp);

    return result == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}

static int run_repl(void)
{
    ussr_preprocessor_t pp;
    char *line;
    char *source;
    size_t source_length;
    size_t capacity;
    size_t line_length;
    int bracket_depth;
    int result;

    printf("USSR Unified Shell Script REPL\n");
    printf("USSR v0.1\n");
    printf("Enter a command list or press Ctrl-D to exit.\n\n");

    if (ussr_pp_init(&pp) != 0)
    {
        fprintf(stderr,
                "USSR: could not initialize preprocessor\n");
        return EXIT_FAILURE;
    }

    source = NULL;
    source_length = 0;
    capacity = 0;
    bracket_depth = 0;

    while ((line = bestline(
                bracket_depth > 0 ? "... " : "ussr> ")) != NULL)
    {
        const char *processed;

        line_length = strlen(line);

        if (line_length == 0)
        {
            bestlineFree(line);
            continue;
        }

        bestlineHistoryAdd(line);

        result = ussr_pp_process_line(&pp, line);
        bestlineFree(line);

        if (result != 0)
        {
            source_length = 0;
            bracket_depth = 0;

            if (source != NULL)
                source[0] = '\0';

            ussr_pp_clear_output(&pp);
            continue;
        }

        processed = ussr_pp_output(&pp);

        if (processed == NULL || processed[0] == '\0')
        {
            ussr_pp_clear_output(&pp);
            continue;
        }

        if (append_source(
                &source,
                &source_length,
                &capacity,
                processed) != 0)
        {
            ussr_pp_clear_output(&pp);
            free(source);
            ussr_pp_cleanup(&pp);
            return EXIT_FAILURE;
        }

        bracket_depth = count_brackets(processed, bracket_depth);
        ussr_pp_clear_output(&pp);

        if (bracket_depth > 0)
            continue;

        result = parse_and_execute(source);

        source_length = 0;
        if (source != NULL)
            source[0] = '\0';

        if (result != 0)
            continue;
    }

    free(source);
    ussr_pp_cleanup(&pp);

    printf("\n");
    return EXIT_SUCCESS;
}

int main(int argc, char **argv)
{
    int result;

    ussr_init();

    if (argc > 1)
        result = run_file(argv[1]);
    else
        result = run_repl();

    ussr_cleanup();
    return result;
}
