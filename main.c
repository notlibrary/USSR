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
/* The parser still produces the existing AST.  The AST is consumed once by */
/* the compiler below and converted into a flat instruction stream.  The VM */
/* never walks a command linked list while executing the program.            */
/* ------------------------------------------------------------------------- */

#define USSR_VM_REGISTER_COUNT 32U
#define USSR_VM_MAX_CODE       1000000U
#define USSR_VM_MAX_STEPS      10000000UL
#define USSR_VM_MAX_LOOP       1000000UL

typedef enum
{
    USSR_VM_NOP = 0x00,
    USSR_VM_COMMAND = 0x01,
    USSR_VM_EVAL_ARG = 0x02,
    USSR_VM_DEFINE = 0x03,
    USSR_VM_SET_BOOL = 0x04,

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
    ussr_vm_instruction_t *code;
    size_t code_count;
    size_t code_capacity;

    /* Compile-time references are not part of the portable bytecode. */
    const void **refs;
    size_t ref_count;
    size_t ref_capacity;
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
    unsigned long loop_steps;
} ussr_vm_t;

typedef struct
{
    size_t *breaks;
    size_t break_count;
    size_t break_capacity;
    size_t *continues;
    size_t continue_count;
    size_t continue_capacity;
} ussr_vm_loop_t;

static void vm_program_free(ussr_vm_program_t *program)
{
    if (program == NULL)
        return;

    free(program->code);
    free((void *)program->refs);
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
    int ref = vm_ref(program, object);
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

static int vm_compile_list(
    ussr_vm_program_t *program,
    ussr_command_list_t *list,
    ussr_vm_loop_t *loop);

static int vm_compile_command(
    ussr_vm_program_t *program,
    ussr_command_t *command,
    ussr_vm_loop_t *loop)
{
    ussr_argument_t *args;
    size_t count;
    int jump_false;
    int jump_end;
    int loop_start;
    size_t i;

    if (program == NULL || command == NULL)
        return -1;

    args = command->arguments;
    count = command->argument_count;

    /* if(condition): [then] [else] */
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

        jump_false = vm_emit(program, USSR_VM_JMP_FALSE, 0, 0, 0, 0);
        if (jump_false < 0)
            return -1;

        if (vm_compile_list(program, args[1].data.command_list, loop) != 0)
            return -1;

        if (count == 3)
        {
            jump_end = vm_emit(program, USSR_VM_JMP, 0, 0, 0, 0);
            if (jump_end < 0)
                return -1;

            if (vm_patch_jump(program, (size_t)jump_false,
                              program->code_count) != 0)
                return -1;

            if (vm_compile_list(program, args[2].data.command_list, loop) != 0)
                return -1;

            if (vm_patch_jump(program, (size_t)jump_end,
                              program->code_count) != 0)
                return -1;
        }
        else
        {
            if (vm_patch_jump(program, (size_t)jump_false,
                              program->code_count) != 0)
                return -1;
        }

        if (command->return_name != NULL)
        {
            if (vm_emit_ref(program, USSR_VM_SET_BOOL, 0, command) < 0)
                return -1;
        }

        return 0;
    }

    /* while(condition): [body] */
    if (strcmp(command->name, "while") == 0)
    {
        size_t condition_ip;
        size_t jump_end;
        ussr_vm_loop_t child_loop;

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

        if ((int)jump_end < 0)
        {
            vm_loop_free(&child_loop);
            return -1;
        }

        if (vm_compile_list(
                program,
                args[1].data.command_list,
                &child_loop) != 0)
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
            if (vm_emit_ref(program, USSR_VM_SET_BOOL, 0, command) < 0)
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
        if (vm_emit_ref(program, USSR_VM_RETURN, 0, command) < 0)
            return -1;

        return 0;
    }

    /* A command whose final argument is a block is a definition. */
    if (count > 0 &&
        args[count - 1].type == USSR_ARGUMENT_COMMAND_LIST &&
        strcmp(command->name, "if") != 0 &&
        strcmp(command->name, "while") != 0)
    {
        if (vm_emit_ref(program, USSR_VM_DEFINE, 0, command) < 0)
            return -1;

        return 0;
    }

    if (vm_emit_ref(program, USSR_VM_COMMAND, 0, command) < 0)
        return -1;

    return 0;
}

static int vm_compile_list(
    ussr_vm_program_t *program,
    ussr_command_list_t *list,
    ussr_vm_loop_t *loop)
{
    ussr_command_t *command;

    if (program == NULL || list == NULL)
        return 0;

    /* This is compile-time AST traversal, never VM execution. */
    command = list->head;

    while (command != NULL)
    {
        if (vm_compile_command(program, command, loop) != 0)
            return -1;

        command = command->next;
    }

    return 0;
}

static int vm_compile(
    ussr_command_list_t *program,
    ussr_vm_program_t *bytecode)
{
    ussr_vm_loop_t top_loop;

    if (program == NULL || bytecode == NULL)
        return -1;

    memset(bytecode, 0, sizeof(*bytecode));
    memset(&top_loop, 0, sizeof(top_loop));

    if (vm_compile_list(bytecode, program, &top_loop) != 0)
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

static int vm_execute(
    ussr_vm_t *vm,
    const ussr_vm_program_t *program)
{
    while (vm->running)
    {
        ussr_vm_instruction_t instruction;
        ussr_value_t value;
        ussr_command_t *command;
        int result;

        if (vm->ip >= program->code_count)
        {
            fprintf(stderr, "USSR VM: instruction pointer out of range\n");
            return -1;
        }

        if (++vm->steps > USSR_VM_MAX_STEPS)
        {
            fprintf(stderr, "USSR VM: execution step limit exceeded\n");
            return -1;
        }

        instruction = program->code[vm->ip++];

        switch (instruction.opcode)
        {
            case USSR_VM_NOP:
                break;

            case USSR_VM_EVAL_ARG:
                if (instruction.a >= USSR_VM_REGISTER_COUNT)
                    return -1;

                value = ussr_null();
                if (ussr_argument_evaluate(
                        (const ussr_argument_t *)
                        vm_ref_get(program, instruction.immediate),
                        &value) != 0)
                {
                    ussr_value_free(&value);
                    return -1;
                }

                ussr_value_free(&vm->registers[instruction.a]);
                vm->registers[instruction.a] = value;
                break;

            case USSR_VM_COMMAND:
                command = (ussr_command_t *)vm_ref_get(program, instruction.immediate);
                result = ussr_execute_command(
                    command->name,
                    command->return_name,
                    command->arguments,
                    command->argument_count
                );

                if (result != 0)
                    return result;
                break;

            case USSR_VM_DEFINE:
            {
                size_t parameter_count;
                char **parameter_names;
                size_t i;

                command = (ussr_command_t *)vm_ref_get(program, instruction.immediate);

                if (command->argument_count == 0 ||
                    command->arguments[
                        command->argument_count - 1
                    ].type != USSR_ARGUMENT_COMMAND_LIST)
                {
                    fprintf(stderr, "USSR VM: invalid definition\n");
                    return -1;
                }

                parameter_count = command->argument_count - 1;
                parameter_names = calloc(
                    parameter_count,
                    sizeof(*parameter_names)
                );

                if (parameter_names == NULL && parameter_count != 0)
                    return -1;

                for (i = 0; i < parameter_count; ++i)
                {
                    if (command->arguments[i].type != USSR_ARGUMENT_EXPRESSION ||
                        command->arguments[i].data.expression == NULL ||
                        command->arguments[i].data.expression->type != USSR_EXPR_VARIABLE)
                    {
                        fprintf(
                            stderr,
                            "USSR VM: definition '%s' parameter must be identifier\n",
                            command->name
                        );
                        free(parameter_names);
                        return -1;
                    }

                    parameter_names[i] =
                        command->arguments[i].data.expression->data.variable;
                }

                result = ussr_define_command(
                    command->name,
                    command->return_name,
                    parameter_names,
                    parameter_count,
                    command->arguments[
                        command->argument_count - 1
                    ].data.command_list
                );

                free(parameter_names);

                if (result != 0)
                    return result;
                break;
            }

            case USSR_VM_SET_BOOL:
                command = (ussr_command_t *)vm_ref_get(
                    program, instruction.immediate
                );
                if (command == NULL)
                    return -1;
                value = ussr_boolean(
                    vm_truthy(&vm->registers[instruction.a])
                );
                if (command->return_name != NULL &&
                    ussr_set_variable(command->return_name, &value) != 0)
                {
                    ussr_value_free(&value);
                    return -1;
                }
                ussr_value_free(&value);
                break;

            case USSR_VM_JMP:
                if (instruction.immediate >= program->code_count)
                    return -1;
                vm->ip = instruction.immediate;
                break;

            case USSR_VM_JMP_TRUE:
                if (instruction.a >= USSR_VM_REGISTER_COUNT ||
                    instruction.immediate >= program->code_count)
                    return -1;

                if (vm_truthy(&vm->registers[instruction.a]))
                    vm->ip = instruction.immediate;
                break;

            case USSR_VM_JMP_FALSE:
                if (instruction.a >= USSR_VM_REGISTER_COUNT ||
                    instruction.immediate >= program->code_count)
                    return -1;

                if (!vm_truthy(&vm->registers[instruction.a]))
                    vm->ip = instruction.immediate;
                break;

            case USSR_VM_BREAK:
            case USSR_VM_CONTINUE:
                /* Patched into an ordinary JMP during compilation. */
                if (instruction.immediate >= program->code_count)
                    return -1;
                vm->ip = instruction.immediate;
                break;

            case USSR_VM_RETURN:
                command = (ussr_command_t *)vm_ref_get(program, instruction.immediate);
                result = ussr_execute_command(
                    command->name,
                    command->return_name,
                    command->arguments,
                    command->argument_count
                );

                if (result != 3)
                    return result;

                vm->running = 0;
                vm->exit_code = 0;
                break;

            case USSR_VM_HALT:
                vm->running = 0;
                vm->exit_code = 0;
                break;

            default:
                fprintf(
                    stderr,
                    "USSR VM: unknown opcode 0x%02x\n",
                    instruction.opcode
                );
                return -1;
        }
    }

    return vm->exit_code;
}

static void vm_dump(const ussr_vm_program_t *program)
{
    size_t i;

    if (program == NULL)
        return;

    fprintf(stderr, "USSR VM: %zu instructions\n", program->code_count);

    for (i = 0; i < program->code_count; ++i)
    {
        fprintf(
            stderr,
            "  %04zu  OP=0x%02x A=%u B=%u C=%u IMM=%u\n",
            i,
            program->code[i].opcode,
            program->code[i].a,
            program->code[i].b,
            program->code[i].c,
            program->code[i].immediate
        );
    }
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

#ifdef USSR_VM_DEBUG
    vm_dump(&bytecode);
#endif

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
