#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <time.h>
#include <errno.h>

#ifdef _WIN32
#include <direct.h>
#define ussr_chdir _chdir
#else
#include <unistd.h>
#define ussr_chdir chdir
#endif

#ifdef _WIN32
#include "win/worstline.h"
#include "win/getopt.h"
#else
#include "bestline.h"
#include <getopt.h>
#endif
#include "autocomplete.h"
#include "ussr.h"
#include "pp.h"
#include "ussr_version.h"
#include "prng64_xrp32.h"
#include "autocomplete.h"
#include "completion_fs.h"

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

#include "ussr_bytecode.h"
#include "uno.h"
#include "ussr_oop_builtins.h"

/* VM runtime state lives here. Bytecode generation lives in ussr_bytecode.c. */

#define USSR_VM_MAX_STEPS 10000000UL
#define USSR_VM_MAX_CALLS 1024U
#define USSR_VM_RETURN_REG USSR_BC_RETURN_REG
#define USSR_VM_REGISTER_COUNT USSR_BC_MAX_REGS

typedef struct
{
    ussr_value_t registers[USSR_VM_REGISTER_COUNT];
    uint32_t ip;
    int running;
    int exit_code;
    unsigned long steps;
    int chain_active;
    char *chain_buffer;
    size_t chain_length;
    int entry_function;
} ussr_vm_t;

typedef struct
{
    uint32_t return_ip;
    uint8_t return_register;
    uint32_t function_index;
    ussr_value_t saved_registers[USSR_VM_REGISTER_COUNT];
    ussr_value_t *saved_variables;
    unsigned char *saved_variable_existed;
    size_t saved_variable_count;
} ussr_vm_frame_t;

static int vm_truthy(const ussr_value_t *value)
{
    if (value == NULL)
        return 0;
    switch (value->type)
    {
        case USSR_NULL: return 0;
        case USSR_BOOLEAN: return value->data.boolean != 0;
        case USSR_INTEGER: return value->data.integer != 0;
        case USSR_REAL: return value->data.real != 0.0;
        case USSR_STRING: return value->data.string != NULL && value->data.string[0] != '\0';
        case USSR_VECTOR: return value->data.vector != NULL && ussr_vector_length(value->data.vector) != 0;
        case USSR_STRUCT: return value->data.instance != NULL;
    }
    return 0;
}

static int vm_numeric(const ussr_value_t *v)
{
    return v != NULL && (v->type == USSR_INTEGER || v->type == USSR_REAL);
}

static double vm_real(const ussr_value_t *v)
{
    return v->type == USSR_REAL ? v->data.real : (double)v->data.integer;
}

static int vm_equal(const ussr_value_t *a, const ussr_value_t *b)
{
    if (a == NULL || b == NULL)
        return 0;
    if (vm_numeric(a) && vm_numeric(b))
        return vm_real(a) == vm_real(b);
    if (a->type != b->type)
        return 0;
    switch (a->type)
    {
        case USSR_NULL: return 1;
        case USSR_INTEGER: return a->data.integer == b->data.integer;
        case USSR_REAL: return a->data.real == b->data.real;
        case USSR_BOOLEAN: return a->data.boolean == b->data.boolean;
        case USSR_STRING: return strcmp(a->data.string, b->data.string) == 0;
        case USSR_VECTOR: return a->data.vector == b->data.vector;
        case USSR_STRUCT: return a->data.instance == b->data.instance;
    }
    return 0;
}

static int vm_binary(
    uint8_t opcode,
    const ussr_value_t *left,
    const ussr_value_t *right,
    ussr_value_t *out)
{
    if (left == NULL || right == NULL || out == NULL)
        return -1;
    *out = ussr_null();

    switch (opcode)
    {
        case USSR_BC_ADD:
        case USSR_BC_SUB:
        case USSR_BC_MUL:
        case USSR_BC_DIV:
        case USSR_BC_MOD:
            if (!vm_numeric(left) || !vm_numeric(right))
                return -1;
            if (opcode == USSR_BC_DIV && vm_real(right) == 0.0)
                return -1;
            if (opcode == USSR_BC_MOD &&
                (left->type != USSR_INTEGER || right->type != USSR_INTEGER || right->data.integer == 0))
                return -1;
            if (left->type == USSR_INTEGER && right->type == USSR_INTEGER && opcode != USSR_BC_DIV)
            {
                long a = left->data.integer;
                long b = right->data.integer;
                if (opcode == USSR_BC_ADD) *out = ussr_integer(a + b);
                else if (opcode == USSR_BC_SUB) *out = ussr_integer(a - b);
                else if (opcode == USSR_BC_MUL) *out = ussr_integer(a * b);
                else *out = ussr_integer(a % b);
            }
            else
            {
                double a = vm_real(left), b = vm_real(right);
                if (opcode == USSR_BC_ADD) *out = ussr_real(a + b);
                else if (opcode == USSR_BC_SUB) *out = ussr_real(a - b);
                else if (opcode == USSR_BC_MUL) *out = ussr_real(a * b);
                else *out = ussr_real(a / b);
            }
            return 0;

        case USSR_BC_EQ: *out = ussr_boolean(vm_equal(left, right)); return 0;
        case USSR_BC_NE: *out = ussr_boolean(!vm_equal(left, right)); return 0;
        case USSR_BC_LT:
        case USSR_BC_LE:
        case USSR_BC_GT:
        case USSR_BC_GE:
            if (!vm_numeric(left) || !vm_numeric(right)) return -1;
            if (opcode == USSR_BC_LT) *out = ussr_boolean(vm_real(left) < vm_real(right));
            else if (opcode == USSR_BC_LE) *out = ussr_boolean(vm_real(left) <= vm_real(right));
            else if (opcode == USSR_BC_GT) *out = ussr_boolean(vm_real(left) > vm_real(right));
            else *out = ussr_boolean(vm_real(left) >= vm_real(right));
            return 0;

        case USSR_BC_LAND: *out = ussr_boolean(vm_truthy(left) && vm_truthy(right)); return 0;
        case USSR_BC_LOR: *out = ussr_boolean(vm_truthy(left) || vm_truthy(right)); return 0;
        case USSR_BC_XOR:
        case USSR_BC_BAND:
        case USSR_BC_BOR:
        case USSR_BC_SHL:
        case USSR_BC_SHR:
            if (left->type != USSR_INTEGER || right->type != USSR_INTEGER) return -1;
            if (opcode == USSR_BC_XOR) *out = ussr_integer(left->data.integer ^ right->data.integer);
            else if (opcode == USSR_BC_BAND) *out = ussr_integer(left->data.integer & right->data.integer);
            else if (opcode == USSR_BC_BOR) *out = ussr_integer(left->data.integer | right->data.integer);
            else if (opcode == USSR_BC_SHL) *out = ussr_integer(left->data.integer << right->data.integer);
            else *out = ussr_integer(left->data.integer >> right->data.integer);
            return 0;
    }
    return -1;
}

static void vm_init(ussr_vm_t *vm)
{
    size_t i;
    memset(vm, 0, sizeof(*vm));
    for (i = 0; i < USSR_VM_REGISTER_COUNT; ++i)
        vm->registers[i] = ussr_null();

    /* No script entry function unless init(...) is found. */
    vm->entry_function = -1;
}

static void vm_cleanup(ussr_vm_t *vm)
{
    size_t i;
    for (i = 0; i < USSR_VM_REGISTER_COUNT; ++i)
        ussr_value_free(&vm->registers[i]);
    free(vm->chain_buffer);
    vm->chain_buffer = NULL;
    vm->chain_length = 0;
    vm->chain_active = 0;
    vm->entry_function = -1;
}

static void vm_frame_free(ussr_vm_frame_t *frame)
{
    size_t i;
    if (frame == NULL) return;
    for (i = 0; i < USSR_VM_REGISTER_COUNT; ++i)
        ussr_value_free(&frame->saved_registers[i]);
    for (i = 0; i < frame->saved_variable_count; ++i)
        ussr_value_free(&frame->saved_variables[i]);
    free(frame->saved_variables);
    free(frame->saved_variable_existed);
    memset(frame, 0, sizeof(*frame));
}

static int vm_push_frame(
    ussr_vm_t *vm,
    ussr_vm_frame_t **frames,
    size_t *count,
    size_t *capacity,
    const ussr_bc_function_t *function,
    const ussr_bc_instruction_t *ins)
{
    ussr_vm_frame_t *frame;
    size_t i;
    const ussr_value_t *v;

    if (*count >= USSR_VM_MAX_CALLS) return -1;
    if (*count == *capacity)
    {
        size_t n = *capacity == 0 ? 16 : *capacity * 2;
        ussr_vm_frame_t *q = realloc(*frames, n * sizeof(*q));
        if (q == NULL) return -1;
        *frames = q; *capacity = n;
    }
    frame = &(*frames)[*count];
    memset(frame, 0, sizeof(*frame));
    frame->return_ip = vm->ip;
    frame->return_register = ins->c;
    frame->function_index = ins->immediate;
    for (i = 0; i < USSR_VM_REGISTER_COUNT; ++i)
        frame->saved_registers[i] = ussr_value_copy(&vm->registers[i]);

    frame->saved_variable_count = function->parameter_count + 1;
    frame->saved_variables = calloc(frame->saved_variable_count, sizeof(*frame->saved_variables));
    frame->saved_variable_existed = calloc(frame->saved_variable_count, sizeof(*frame->saved_variable_existed));
    if (frame->saved_variables == NULL || frame->saved_variable_existed == NULL)
    { vm_frame_free(frame); return -1; }
    for (i = 0; i < function->parameter_count; ++i)
    {
        v = ussr_get_variable(function->parameters[i]);
        if (v != NULL) { frame->saved_variable_existed[i] = 1; frame->saved_variables[i] = ussr_value_copy(v); }
    }
    v = ussr_get_variable(function->return_name);
    if (v != NULL) { frame->saved_variable_existed[function->parameter_count] = 1; frame->saved_variables[function->parameter_count] = ussr_value_copy(v); }
    ++*count;
    return 0;
}

static int vm_call(
    ussr_vm_t *vm,
    const ussr_bc_program_t *program,
    ussr_vm_frame_t **frames,
    size_t *frame_count,
    size_t *frame_capacity,
    const ussr_bc_instruction_t *ins)
{
    const ussr_bc_function_t *f;
    size_t i;
    if (ins->immediate >= program->function_count || ins->b != program->functions[ins->immediate].parameter_count)
        return -1;
    if ((unsigned)ins->a + (unsigned)ins->b > USSR_VM_RETURN_REG)
        return -1;
    f = &program->functions[ins->immediate];
    if (vm_push_frame(vm, frames, frame_count, frame_capacity, f, ins) != 0)
        return -1;
    for (i = 0; i < f->parameter_count; ++i)
    {
        ussr_value_t v = ussr_value_copy(&vm->registers[ins->a + i]);
        if (ussr_set_variable(f->parameters[i], &v) != 0) { ussr_value_free(&v); return -1; }
        ussr_value_free(&v);
    }
    for (i = 0; i < USSR_VM_REGISTER_COUNT; ++i) { ussr_value_free(&vm->registers[i]); vm->registers[i] = ussr_null(); }
    vm->ip = f->entry;
    return 0;
}

static int vm_return(
    ussr_vm_t *vm,
    const ussr_bc_program_t *program,
    ussr_vm_frame_t *frames,
    size_t *frame_count)
{
    ussr_vm_frame_t *frame;
    const ussr_bc_function_t *f;
    ussr_value_t result;
    size_t i;
    const ussr_value_t *v;
    if (*frame_count == 0) return -1;
    frame = &frames[*frame_count - 1];
    if (frame->function_index >= program->function_count) return -1;
    f = &program->functions[frame->function_index];
    v = ussr_get_variable(f->return_name);
    result = v != NULL ? ussr_value_copy(v) : ussr_null();
    for (i = 0; i < f->parameter_count + 1; ++i)
    {
        const char *name = i < f->parameter_count ? f->parameters[i] : f->return_name;
        ussr_value_t restore = frame->saved_variable_existed[i] ? ussr_value_copy(&frame->saved_variables[i]) : ussr_null();
        if (ussr_set_variable(name, &restore) != 0) { ussr_value_free(&restore); ussr_value_free(&result); return -1; }
        ussr_value_free(&restore);
    }
    for (i = 0; i < USSR_VM_REGISTER_COUNT; ++i) { ussr_value_free(&vm->registers[i]); vm->registers[i] = frame->saved_registers[i]; frame->saved_registers[i] = ussr_null(); }
    vm->ip = frame->return_ip;
    ussr_value_free(&vm->registers[frame->return_register]);
    vm->registers[frame->return_register] = result;
    vm_frame_free(frame);
    --*frame_count;
    return 0;
}

static int vm_values_from_registers(
    const ussr_vm_t *vm,
    uint8_t base,
    uint8_t count,
    ussr_value_t **out)
{
    ussr_value_t *values;
    size_t i;
    if ((unsigned)base + (unsigned)count > USSR_VM_REGISTER_COUNT) return -1;
    values = calloc(count, sizeof(*values));
    if (values == NULL && count != 0) return -1;
    for (i = 0; i < count; ++i) values[i] = ussr_value_copy(&vm->registers[base + i]);
    *out = values;
    return 0;
}

static void vm_free_values(ussr_value_t *values, size_t count)
{
    size_t i;
    for (i = 0; i < count; ++i) ussr_value_free(&values[i]);
    free(values);
}

static const ussr_command_t *vm_find_oop_site(
    const ussr_bc_program_t *program,
    uint32_t instruction
)
{
    size_t i;

    if (program == NULL)
        return NULL;

    for (i = 0; i < program->oop_site_count; ++i)
    {
        if (program->oop_sites[i].instruction == instruction)
            return program->oop_sites[i].command;
    }

    return NULL;
}

static const ussr_command_t *vm_find_scan_site(
    const ussr_bc_program_t *program,
    uint32_t instruction)
{
    size_t i;

    if (program == NULL)
        return NULL;

    for (i = 0; i < program->scan_site_count; ++i)
    {
        if (program->scan_sites[i].instruction == instruction)
            return program->scan_sites[i].command;
    }

    return NULL;
}

static const char *vm_scan_type_end(const char *p)
{
    if (strncmp(p, "STR", 3) == 0)
        return p + 3;
    if (strncmp(p, "INT", 3) == 0)
        return p + 3;
    if (strncmp(p, "REAL", 4) == 0)
        return p + 4;
    if (strncmp(p, "BOOL", 4) == 0)
        return p + 4;
    return NULL;
}

static int vm_scan_type_valid(const char *p, const char **end,
                              const char **type)
{
    const char *q;

    if (p == NULL || end == NULL || type == NULL)
        return -1;

    q = vm_scan_type_end(p);
    if (q == NULL)
        return -1;

    if ((q[0] != '\0') && q[0] != ';' &&
        q[0] != ' ' && q[0] != '\t')
        return -1;

    *end = q;
    *type = p;
    return 0;
}

static char *vm_scan_trim_copy(const char *start, const char *end)
{
    size_t length;
    char *copy;

    while (start < end && (*start == ' ' || *start == '\t'))
        ++start;
    while (end > start && (end[-1] == ' ' || end[-1] == '\t'))
        --end;

    length = (size_t)(end - start);
    copy = malloc(length + 1);
    if (copy == NULL)
        return NULL;

    memcpy(copy, start, length);
    copy[length] = '\0';
    return copy;
}

static int vm_scan_parse_field(
    const char *type,
    const char *start,
    const char *end,
    ussr_value_t *value)
{
    char *text;
    char *tail;

    if (type == NULL || start == NULL || end == NULL || value == NULL)
        return -1;

    text = vm_scan_trim_copy(start, end);
    if (text == NULL)
        return -1;

    *value = ussr_null();

    if (strncmp(type, "STR", 3) == 0)
    {
        *value = ussr_string(text);
        free(text);
        return 0;
    }

    if (strncmp(type, "INT", 3) == 0)
    {
        long number = strtol(text, &tail, 10);
        if (tail == text || *tail != '\0')
        {
            free(text);
            return -1;
        }
        *value = ussr_integer(number);
        free(text);
        return 0;
    }

    if (strncmp(type, "REAL", 4) == 0)
    {
        double number = strtod(text, &tail);
        if (tail == text || *tail != '\0')
        {
            free(text);
            return -1;
        }
        *value = ussr_real(number);
        free(text);
        return 0;
    }

    if (strncmp(type, "BOOL", 4) == 0)
    {
        if (strcmp(text, "true") == 0 || strcmp(text, "1") == 0)
            *value = ussr_boolean(1);
        else if (strcmp(text, "false") == 0 || strcmp(text, "0") == 0)
            *value = ussr_boolean(0);
        else
        {
            free(text);
            return -1;
        }
        free(text);
        return 0;
    }

    free(text);
    return -1;
}

static int vm_scan_execute(
    const char *format,
    const ussr_command_t *command)
{
    const char *spec;
    const char *cursor;
    const char *end;
    const char *type;
    const char *token;
    char line[4096];
    const char *field_start;
    const char *field_end;
    ussr_value_t values[256];
    size_t value_count = 0;
    size_t i;
    int have_spec = 0;

    if (format == NULL || command == NULL || command->argument_count < 2)
        return -1;

    for (i = 0; i < 256; ++i)
        values[i] = ussr_null();

    /* Find the first format token. Everything before it is the prompt. */
    spec = NULL;
    for (cursor = format; *cursor != '\0'; ++cursor)
    {
        if ((cursor == format || cursor[-1] == ' ' || cursor[-1] == '\t') &&
            vm_scan_type_valid(cursor, &end, &type) == 0)
        {
            spec = cursor;
            have_spec = 1;
            break;
        }
    }

    if (!have_spec)
    {
        fprintf(stderr, "USSR: scan format has no type token\n");
        return -1;
    }

    if (spec > format)
        fwrite(format, 1, (size_t)(spec - format), stdout);
    fflush(stdout);

    /* Parse the semicolon-delimited format tokens. */
    cursor = spec;
    while (*cursor != '\0')
    {
        if (value_count >= 256 ||
            vm_scan_type_valid(cursor, &end, &type) != 0)
            goto fail;

        ++value_count;
        cursor = end;
        while (*cursor == ' ' || *cursor == '\t')
            ++cursor;

        if (*cursor == '\0')
            break;
        if (*cursor != ';')
            goto fail;
        ++cursor;
        while (*cursor == ' ' || *cursor == '\t')
            ++cursor;
    }

    if (value_count != command->argument_count - 1)
    {
        fprintf(stderr,
                "USSR: scan format expects %zu values, got %zu destinations\n",
                value_count, command->argument_count - 1);
        goto fail;
    }

    if (fgets(line, sizeof(line), stdin) == NULL)
        goto fail;

    /* Remove the physical line ending. */
    line[strcspn(line, "\r\n")] = '\0';

    /*
     * Accept either semicolon-delimited fields or ordinary whitespace
     * fields.  The former mirrors the format notation; the latter makes
     * a line such as "alice 42 bob 7" natural to enter interactively.
     */
    field_start = line;
    {
        int semicolon_input = strchr(line, ';') != NULL;

        for (i = 0; i < value_count; ++i)
        {
            const char *next;
            size_t j;

            token = spec;
            for (j = 0; j < i; ++j)
            {
                token = strchr(token, ';');
                if (token == NULL)
                    goto fail;
                ++token;
                while (*token == ' ' || *token == '\t')
                    ++token;
            }

            if (semicolon_input)
            {
                next = strchr(field_start, ';');
                field_end = next != NULL ? next :
                    field_start + strlen(field_start);
            }
            else
            {
                while (*field_start == ' ' || *field_start == '\t')
                    ++field_start;
                field_end = field_start;
                while (*field_end != '\0' &&
                       *field_end != ' ' && *field_end != '\t')
                    ++field_end;
                next = *field_end != '\0' ? field_end : NULL;
            }

            if (vm_scan_type_valid(token, &end, &type) != 0 ||
                vm_scan_parse_field(type, field_start, field_end, &values[i]) != 0)
            {
                fprintf(stderr, "USSR: scan input does not match format\n");
                goto fail;
            }

            if (next == NULL)
            {
                if (i + 1 != value_count)
                    goto fail;
                field_start = field_end;
                break;
            }

            field_start = next + 1;
        }
    }

    for (i = 0; i < value_count; ++i)
    {
        const ussr_argument_t *destination = &command->arguments[i + 1];
        const char *name;

        if (destination->type != USSR_ARGUMENT_EXPRESSION ||
            destination->data.expression == NULL ||
            destination->data.expression->type != USSR_EXPR_VARIABLE)
            goto fail;

        name = destination->data.expression->data.variable;
        if (ussr_set_variable(name, &values[i]) != 0)
            goto fail;
    }

    for (i = 0; i < value_count; ++i)
        ussr_value_free(&values[i]);

    return 0;

fail:
    for (i = 0; i < value_count && i < 256; ++i)
        ussr_value_free(&values[i]);
    return -1;
}

static int vm_chain_set_buffer(ussr_vm_t *vm, char *text)
{
    size_t length;

    if (vm == NULL)
    {
        free(text);
        return -1;
    }
    if (text == NULL)
    {
        text = malloc(1);
        if (text != NULL) text[0] = '\0';
    }
    if (text == NULL) return -1;
    length = strlen(text);
    free(vm->chain_buffer);
    vm->chain_buffer = text;
    vm->chain_length = length;
    return 0;
}

static int vm_chain_write_file(const ussr_vm_t *vm, const char *filename)
{
    FILE *fp;
    size_t written;
    if (vm == NULL || filename == NULL || vm->chain_buffer == NULL) return -1;
    fp = fopen(filename, "wb");
    if (fp == NULL)
    {
        fprintf(stderr, "USSR: cannot open chain output '%s': %s\n", filename, strerror(errno));
        return -1;
    }
    written = fwrite(vm->chain_buffer, 1, vm->chain_length, fp);
    if (written != vm->chain_length)
    {
        fclose(fp);
        return -1;
    }
    if (fclose(fp) != 0)
        return -1;
    return 0;
}

static int vm_template_type(
    const char *p,
    const char **end,
    const char **type)
{
    static const char *types[] = { "STR", "INT", "REAL", "BOOL" };
    size_t i;

    if (p == NULL || end == NULL || type == NULL)
        return -1;

    for (i = 0; i < sizeof(types) / sizeof(types[0]); ++i)
    {
        size_t length = strlen(types[i]);

        if (strncmp(p, types[i], length) != 0)
            continue;

        if (p[length] != '\0' &&
            p[length] != ';' &&
            p[length] != ' ' &&
            p[length] != '\t')
            continue;

        *end = p + length;
        *type = types[i];
        return 0;
    }

    return -1;
}

static int vm_template_emit_value(
    const char *type,
    const ussr_value_t *value)
{
    if (type == NULL || value == NULL)
        return -1;

    if (strcmp(type, "STR") == 0)
    {
        if (value->type != USSR_STRING)
            return -1;
        fputs(value->data.string, stdout);
        return 0;
    }

    if (strcmp(type, "INT") == 0)
    {
        if (value->type != USSR_INTEGER)
            return -1;
        printf("%ld", value->data.integer);
        return 0;
    }

    if (strcmp(type, "REAL") == 0)
    {
        if (value->type != USSR_REAL)
            return -1;
        printf("%.17g", value->data.real);
        return 0;
    }

    if (strcmp(type, "BOOL") == 0)
    {
        if (value->type != USSR_BOOLEAN)
            return -1;
        fputs(value->data.boolean ? "true" : "false", stdout);
        return 0;
    }

    return -1;
}

static int vm_template_execute(
    const char *format,
    const ussr_value_t *values,
    size_t value_count)
{
    const char *p;
    size_t value_index = 0;

    if (format == NULL || values == NULL || value_count > 255)
        return -1;

    for (p = format; *p != '\0'; )
    {
        const char *end;
        const char *type;

        if (p[0] == ';' && p[1] == ';')
        {
            fputc(';', stdout);
            p += 2;
            continue;
        }

        if ((p == format || p[-1] == ' ' || p[-1] == '\t' || p[-1] == ';') &&
            vm_template_type(p, &end, &type) == 0)
        {
            if (value_index >= value_count ||
                vm_template_emit_value(type, &values[value_index]) != 0)
                return -1;

            ++value_index;
            p = end;

            if (*p == ';')
                ++p;
            continue;
        }

        fputc((unsigned char)*p, stdout);
        ++p;
    }

    if (value_index != value_count)
        return -1;

    fflush(stdout);
    return 0;
}

static int vm_execute(
    ussr_vm_t *vm,
    const ussr_bc_program_t *program)
{
    ussr_vm_frame_t *frames = NULL;
    size_t frame_count = 0, frame_capacity = 0;
    int result = 0;

    if (vm->entry_function >= 0)
    {
        ussr_bc_instruction_t entry_call;

        if ((size_t)vm->entry_function >= program->function_count ||
            program->functions[vm->entry_function].parameter_count != 2 ||
            program->code_count == 0)
        {
            result = -1;
            goto done;
        }

        memset(&entry_call, 0, sizeof(entry_call));
        entry_call.opcode = USSR_BC_CALL;
        entry_call.a = 0;
        entry_call.b = 2;
        entry_call.c = USSR_VM_RETURN_REG;
        entry_call.immediate = (uint32_t)vm->entry_function;

        /* Return from init directly to the program HALT instruction. */
        vm->ip = (uint32_t)(program->code_count - 1);
        if (vm_call(vm, program, &frames, &frame_count,
                    &frame_capacity, &entry_call) != 0)
        {
            result = -1;
            goto done;
        }
    }

    while (vm->running)
    {
        ussr_bc_instruction_t ins;
        ussr_value_t value, temp;
        const char *name;
        ussr_value_t *args = NULL;
        size_t i;

        if (vm->ip >= program->code_count) { result = -1; goto done; }
        if (++vm->steps > USSR_VM_MAX_STEPS) { fprintf(stderr, "USSR VM: execution step limit exceeded\n"); result = -1; goto done; }
        ins = program->code[vm->ip++];

        switch (ins.opcode)
        {
            case USSR_BC_NOP: break;
            case USSR_BC_LOAD_CONST:
                if (ins.a >= USSR_VM_REGISTER_COUNT || ins.immediate >= program->constant_count) { result=-1; goto done; }
                ussr_value_free(&vm->registers[ins.a]); vm->registers[ins.a] = ussr_value_copy(&program->constants[ins.immediate]); break;
            case USSR_BC_LOAD_VAR:
                if (ins.a >= USSR_VM_REGISTER_COUNT || ins.immediate >= program->string_count) { result=-1; goto done; }
                name=program->strings[ins.immediate];
                { const ussr_value_t *v=ussr_get_variable(name); vm->registers[ins.a]=v?ussr_value_copy(v):ussr_null(); }
                break;
            case USSR_BC_STORE_VAR:
                if (ins.a >= USSR_VM_REGISTER_COUNT || ins.immediate >= program->string_count) { result=-1; goto done; }
                if (ussr_set_variable(program->strings[ins.immediate], &vm->registers[ins.a]) != 0) { result=-1; goto done; } break;
            case USSR_BC_LOAD_HASH:
                if (ins.a >= USSR_VM_REGISTER_COUNT || ins.immediate >= program->string_count) { result=-1; goto done; }
                { const ussr_value_t *v=ussr_hash_get_value(program->strings[ins.immediate]); if (!v) { fprintf(stderr,"USSR: undefined hash key '%s'\n",program->strings[ins.immediate]); result=-1; goto done; } ussr_value_free(&vm->registers[ins.a]); vm->registers[ins.a]=ussr_value_copy(v); } break;
            case USSR_BC_STORE_HASH:
                if (ins.a >= USSR_VM_REGISTER_COUNT || ins.immediate >= program->string_count) { result=-1; goto done; }
                if (ussr_hash_set_value(program->strings[ins.immediate], &vm->registers[ins.a]) != 0) { result=-1; goto done; } break;
            case USSR_BC_DECODE_UNO:
                if (ins.a >= USSR_VM_REGISTER_COUNT || ins.immediate >= program->string_count) { result=-1; goto done; }
                value=ussr_null(); if (ussr_uno_decode(program->strings[ins.immediate],&value)!=0) { result=-1; goto done; } ussr_value_free(&vm->registers[ins.a]); vm->registers[ins.a]=value; break;
            case USSR_BC_GET:
                if (ins.immediate >= program->string_count ||
                    ins.a >= USSR_VM_REGISTER_COUNT)
                { result=-1; goto done; }
                {
                    const ussr_value_t *source =
                        ussr_get_variable(program->strings[ins.immediate]);
                    if (source == NULL) { result=-1; goto done; }
                    ussr_value_free(&vm->registers[ins.a]);
                    vm->registers[ins.a] = ussr_value_copy(source);
                }
                break;
            case USSR_BC_VECTOR_GET:
                if (ins.a >= USSR_VM_REGISTER_COUNT ||
                    ins.b >= USSR_VM_REGISTER_COUNT ||
                    ins.c >= USSR_VM_REGISTER_COUNT ||
                    vm->registers[ins.b].type != USSR_VECTOR ||
                    vm->registers[ins.c].type != USSR_INTEGER)
                {
                    fprintf(stderr, "USSR VM: vector get requires vector and integer index\n");
                    result = -1;
                    goto done;
                }
                {
                    ussr_value_t vector_value = ussr_null();
                    if (vm->registers[ins.c].data.integer < 0 ||
                        !ussr_vector_get(
                            vm->registers[ins.b].data.vector,
                            (size_t)vm->registers[ins.c].data.integer,
                            &vector_value))
                    {
                        fprintf(stderr, "USSR VM: vector index out of range\n");
                        result = -1;
                        goto done;
                    }
                    ussr_value_free(&vm->registers[ins.a]);
                    vm->registers[ins.a] = vector_value;
                }
                break;

            case USSR_BC_VECTOR_SET:
                if (ins.a >= USSR_VM_REGISTER_COUNT ||
                    ins.b >= USSR_VM_REGISTER_COUNT ||
                    ins.c >= USSR_VM_REGISTER_COUNT ||
                    vm->registers[ins.a].type != USSR_VECTOR ||
                    vm->registers[ins.b].type != USSR_INTEGER ||
                    vm->registers[ins.b].data.integer < 0 ||
                    !ussr_vector_set(
                        vm->registers[ins.a].data.vector,
                        (size_t)vm->registers[ins.b].data.integer,
                        vm->registers[ins.c]
                    ))
                {
                    fprintf(stderr, "USSR VM: vector index out of range or incompatible value\n");
                    result = -1;
                    goto done;
                }
                break;

            case USSR_BC_NEG:
                if (ins.a>=USSR_VM_REGISTER_COUNT || ins.b>=USSR_VM_REGISTER_COUNT) { result=-1; goto done; }
                if (!vm_numeric(&vm->registers[ins.b])) { result=-1; goto done; }
                value=vm->registers[ins.b].type==USSR_INTEGER?ussr_integer(-vm->registers[ins.b].data.integer):ussr_real(-vm->registers[ins.b].data.real); ussr_value_free(&vm->registers[ins.a]); vm->registers[ins.a]=value; break;
            case USSR_BC_ADD: case USSR_BC_SUB: case USSR_BC_MUL: case USSR_BC_DIV: case USSR_BC_MOD:
            case USSR_BC_EQ: case USSR_BC_NE: case USSR_BC_LT: case USSR_BC_LE: case USSR_BC_GT: case USSR_BC_GE:
            case USSR_BC_LAND: case USSR_BC_LOR: case USSR_BC_XOR: case USSR_BC_BAND: case USSR_BC_BOR: case USSR_BC_SHL: case USSR_BC_SHR:
                if (ins.a>=USSR_VM_REGISTER_COUNT || ins.b>=USSR_VM_REGISTER_COUNT || ins.c>=USSR_VM_REGISTER_COUNT || vm_binary(ins.opcode,&vm->registers[ins.b],&vm->registers[ins.c],&temp)!=0) { result=-1; goto done; }
                ussr_value_free(&vm->registers[ins.a]); vm->registers[ins.a]=temp; break;
            case USSR_BC_JMP: case USSR_BC_JMP_TRUE: case USSR_BC_JMP_FALSE:
                if (ins.immediate >= program->code_count || (ins.opcode != USSR_BC_JMP && ins.a >= USSR_VM_REGISTER_COUNT)) { result=-1; goto done; }
                if (ins.opcode==USSR_BC_JMP || (ins.opcode==USSR_BC_JMP_TRUE && vm_truthy(&vm->registers[ins.a])) || (ins.opcode==USSR_BC_JMP_FALSE && !vm_truthy(&vm->registers[ins.a]))) vm->ip=ins.immediate;
                break;
            case USSR_BC_RANDOM64:
                if (ins.a >= USSR_VM_REGISTER_COUNT) { result=-1; goto done; }
                value = ussr_integer((long)prng64_xrp32());
                ussr_value_free(&vm->registers[ins.a]);
                vm->registers[ins.a] = value;
                break;
            case USSR_BC_SEED64:
                if (ins.a >= USSR_VM_REGISTER_COUNT ||
                    vm->registers[0].type != USSR_INTEGER)
                { fprintf(stderr, "USSR VM: seed_random64 requires an integer seed\n"); result=-1; goto done; }
                seed_xrp32((uint64_t)vm->registers[0].data.integer);
                value = ussr_value_copy(&vm->registers[0]);
                ussr_value_free(&vm->registers[ins.a]);
                vm->registers[ins.a] = value;
                break;
            case USSR_BC_TIME:
                if (ins.a >= USSR_VM_REGISTER_COUNT) { result=-1; goto done; }
                value = ussr_integer((long)time(NULL));
                ussr_value_free(&vm->registers[ins.a]);
                vm->registers[ins.a] = value;
                break;
            case USSR_BC_CHAIN:
                vm->chain_active = 1;
                value = ussr_integer(0);
                ussr_value_free(&vm->registers[0]);
                vm->registers[0] = value;
                break;
            case USSR_BC_FILE:
                if (vm->registers[0].type != USSR_STRING || !vm->chain_active ||
                    vm_chain_write_file(vm, vm->registers[0].data.string) != 0)
                { result=-1; goto done; }
                vm->chain_active = 0;
                free(vm->chain_buffer); vm->chain_buffer = NULL; vm->chain_length = 0;
                value = ussr_integer(0);
                ussr_value_free(&vm->registers[0]); vm->registers[0] = value;
                break;
            case USSR_BC_CD:
                if (vm->registers[0].type != USSR_STRING ||
                    ussr_chdir(vm->registers[0].data.string) != 0)
                {
                    if (vm->registers[0].type == USSR_STRING)
                        fprintf(stderr, "USSR: cd '%s': %s\n", vm->registers[0].data.string, strerror(errno));
                    result=-1; goto done;
                }
                value = ussr_integer(0);
                ussr_value_free(&vm->registers[0]); vm->registers[0] = value;
                break;
            case USSR_BC_SCAN:
            {
                const ussr_command_t *scan_command =
                    vm_find_scan_site(program, vm->ip - 1);
                if (ins.immediate >= program->string_count ||
                    ins.a >= USSR_VM_REGISTER_COUNT ||
                    scan_command == NULL ||
                    ins.b != scan_command->argument_count - 1 ||
                    vm_scan_execute(program->strings[ins.immediate],
                                    scan_command) != 0)
                { result=-1; goto done; }
                value = ussr_boolean(1);
                ussr_value_free(&vm->registers[ins.a]);
                vm->registers[ins.a] = value;
                break;
            }
            case USSR_BC_TEMPLATE:
            {
                ussr_value_t *template_values;

                if (ins.immediate >= program->string_count ||
                    ins.a >= USSR_VM_REGISTER_COUNT ||
                    ins.b > USSR_BC_RETURN_REG)
                {
                    result = -1;
                    goto done;
                }

                template_values = &vm->registers[0];
                if (vm_template_execute(
                        program->strings[ins.immediate],
                        template_values,
                        ins.b) != 0)
                {
                    fprintf(stderr, "USSR: template arguments do not match format\n");
                    result = -1;
                    goto done;
                }

                value = ussr_boolean(1);
                ussr_value_free(&vm->registers[ins.a]);
                vm->registers[ins.a] = value;
                break;
            }
            case USSR_BC_SET:
                if (ins.a>=USSR_VM_REGISTER_COUNT || ins.immediate>=program->string_count) { result=-1; goto done; }
                if (ussr_set_variable(program->strings[ins.immediate],&vm->registers[ins.a])!=0) { result=-1; goto done; } break;
            case USSR_BC_PRINT:
                if (ins.a>=USSR_VM_REGISTER_COUNT) { result=-1; goto done; } ussr_print_value(&vm->registers[ins.a]); break;
            case USSR_BC_CONCAT:
                if ((unsigned)ins.a + 1 >= USSR_VM_RETURN_REG || (unsigned)ins.a + (unsigned)ins.c > USSR_VM_RETURN_REG) { result=-1; goto done; }
                { size_t total=0; char *str,*cur; for(i=0;i<ins.c;++i){ if(vm->registers[i].type!=USSR_STRING){result=-1;goto done;} total+=strlen(vm->registers[i].data.string); } str=malloc(total+1); if(!str){result=-1;goto done;} cur=str; for(i=0;i<ins.c;++i){size_t n=strlen(vm->registers[i].data.string);memcpy(cur,vm->registers[i].data.string,n);cur+=n;}*cur='\0';value=ussr_string(str);free(str);ussr_value_free(&vm->registers[ins.a]);vm->registers[ins.a]=value; } break;
            case USSR_BC_CALL:
                if (vm_call(vm,program,&frames,&frame_count,&frame_capacity,&ins)!=0) {result=-1;goto done;} break;
            case USSR_BC_RET:
                if (vm_return(vm,program,frames,&frame_count)!=0) {result=-1;goto done;} break;
            case USSR_BC_RETURN:
                if (frame_count==0) {result=-1;goto done;} if(vm_return(vm,program,frames,&frame_count)!=0){result=-1;goto done;} break;
            case USSR_BC_EXTERNAL:
                if (ins.immediate>=program->string_count || ins.b>USSR_VM_RETURN_REG || vm_values_from_registers(vm,0,ins.b,&args)!=0) {result=-1;goto done;}
                value=ussr_null(); result=ussr_external_execute_values(program->strings[ins.immediate],args,ins.b,&value); vm_free_values(args,ins.b);args=NULL; if(result!=0)goto done; ussr_value_free(&vm->registers[ins.a]);vm->registers[ins.a]=value; break;
            case USSR_BC_OOP:
            {
                const ussr_command_t *source_command;
                ussr_argument_t *av;
                size_t argument_count;

                if (ins.immediate >= program->string_count ||
                    ins.b >= USSR_VM_RETURN_REG)
                {
                    fprintf(stderr, "USSR VM: invalid OOP instruction\n");
                    result = -1;
                    goto done;
                }

                argument_count = ins.b;
                source_command = vm_find_oop_site(
                    program,
                    vm->ip - 1
                );

                av = calloc(argument_count, sizeof(*av));
                if (av == NULL && argument_count != 0)
                {
                    result = -1;
                    goto done;
                }

                for (i = 0; i < argument_count; ++i)
                {
                    av[i].assignment = 0;

                    if (source_command != NULL &&
                        i < source_command->argument_count &&
                        source_command->arguments[i].type ==
                            USSR_ARGUMENT_COMMAND_LIST)
                    {
                        av[i].type = USSR_ARGUMENT_COMMAND_LIST;
                        av[i].assignment =
                            source_command->arguments[i].assignment;
                        av[i].data.command_list =
                            source_command->arguments[i].data.command_list;
                    }
                    else
                    {
                        av[i].type = USSR_ARGUMENT_VALUE;
                        if (source_command != NULL &&
                            i < source_command->argument_count)
                            av[i].assignment =
                                source_command->arguments[i].assignment;
                        av[i].data.value =
                            ussr_value_copy(&vm->registers[i]);
                    }
                }

                value = ussr_null();
                result = ussr_oop_dispatch(
                    program->strings[ins.immediate],
                    source_command != NULL ? source_command->return_name : NULL,
                    av,
                    argument_count,
                    &value
                );

                for (i = 0; i < argument_count; ++i)
                {
                    if (av[i].type == USSR_ARGUMENT_VALUE)
                        ussr_value_free(&av[i].data.value);
                }
                free(av);

                if (result < 0)
                {
                    ussr_value_free(&value);
                    goto done;
                }

                if (result == 0)
                {
                    /* Not an OOP builtin: execute it as a host command. */
                    ussr_value_t *external_values;

                    if (source_command != NULL)
                    {
                        if (argument_count !=
                            source_command->argument_count)
                        {
                            ussr_value_free(&value);
                            result = -1;
                            goto done;
                        }
                    }

                    if (vm_values_from_registers(
                            vm, 0, (uint8_t)argument_count,
                            &external_values) != 0)
                    {
                        ussr_value_free(&value);
                        result = -1;
                        goto done;
                    }

                    ussr_value_free(&value);
                    value = ussr_null();
                    if (vm->chain_active || ins.c != 0)
                    {
                        char *output = NULL;
                        result = ussr_external_execute_values_io(
                            program->strings[ins.immediate], external_values,
                            argument_count, vm->chain_active ? vm->chain_buffer : NULL,
                            &output, &value);
                        if (result == 0 && vm_chain_set_buffer(vm, output) != 0)
                            result = -1;
                    }
                    else
                    {
                        result = ussr_external_execute_values(
                            program->strings[ins.immediate], external_values,
                            argument_count, &value);
                    }
                    vm_free_values(external_values, argument_count);
                    if (result != 0) goto done;
                }

                ussr_value_free(&vm->registers[ins.a]);
                vm->registers[ins.a] = value;
                break;
            }
            case USSR_BC_EVAL:
                /* eval is implemented by the VM boundary: source is parsed, compiled, then executed as bytecode. */
                if(ins.a>=USSR_VM_REGISTER_COUNT || program->strings[ins.immediate]==NULL || vm->registers[0].type!=USSR_STRING){result=-1;goto done;}
                { YY_BUFFER_STATE b=yy_scan_string(vm->registers[0].data.string); ussr_command_list_t *old=ussr_parsed_program; ussr_parsed_program=NULL; if(!b){result=-1;goto done;} if(yyparse()!=0||ussr_parsed_program==NULL){yy_delete_buffer(b);ussr_parsed_program=old;result=-1;goto done;} yy_delete_buffer(b); ussr_bc_program_t nested; if(ussr_bc_compile(ussr_parsed_program,&nested)!=0){ussr_command_list_free(ussr_parsed_program);ussr_parsed_program=old;result=-1;goto done;} ussr_vm_t nested_vm;vm_init(&nested_vm);nested_vm.running=1;result=vm_execute(&nested_vm,&nested); if(result==0){const ussr_value_t *v=ussr_get_variable(program->strings[ins.immediate]); value=v?ussr_value_copy(v):ussr_null();} vm_cleanup(&nested_vm);ussr_bc_program_free(&nested);ussr_command_list_free(ussr_parsed_program);ussr_parsed_program=old;if(result!=0)goto done;ussr_value_free(&vm->registers[ins.a]);vm->registers[ins.a]=value; } break;
            case USSR_BC_HALT: vm->running=0; vm->exit_code=0; break;
            default: fprintf(stderr,"USSR VM: unknown opcode 0x%02x\n",ins.opcode);result=-1;goto done;
        }
    }
    result=vm->exit_code;
done:
    while(frame_count>0){vm_frame_free(&frames[frame_count-1]);--frame_count;}
    free(frames); return result;
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

static int parse_and_execute(const char *source, int argc, char **argv)
{
    YY_BUFFER_STATE buffer;
    int result;
    ussr_bc_program_t bytecode;
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
        fprintf(stderr, "USSR parser: syntax error");
        ussr_parsed_program = NULL;
        return -1;
    }

    if (ussr_parsed_program == NULL)
        return 0;

    /* AST -> bytecode happens once. Execution starts only after compilation. */
    if (ussr_bc_compile(ussr_parsed_program, &bytecode) != 0)
    {
        fprintf(stderr, "USSR compiler: compilation failed");
        ussr_command_list_free(ussr_parsed_program);
        ussr_parsed_program = NULL;
        return -1;
    }

    vm_init(&vm);
    vm.running = 1;

    {
        size_t fi;
        for (fi = 0; fi < bytecode.function_count; ++fi)
        {
            if (strcmp(bytecode.functions[fi].name, "init") == 0)
            {
                ussr_bc_function_t *init = &bytecode.functions[fi];
                ussr_vector_t *args_vector;
                ussr_value_t arg_count_value;
                ussr_value_t vector_value;
                size_t i;

                if (init->parameter_count != 2)
                {
                    fprintf(stderr,
                            "USSR: init must have parameters arg_cnt and arg_vec\n");
                    result = -1;
                    goto parse_execute_after_vm;
                }

                args_vector = ussr_vector_create("string");
                if (args_vector == NULL)
                {
                    result = -1;
                    goto parse_execute_after_vm;
                }

                for (i = 1; i < (size_t)argc; ++i)
                {
                    ussr_value_t item = ussr_string(argv[i]);
                    if (ussr_vector_push(args_vector, item) != 0)
                    {
                        ussr_value_free(&item);
                        ussr_vector_release(args_vector);
                        result = -1;
                        goto parse_execute_after_vm;
                    }
                    ussr_value_free(&item);
                }

                arg_count_value = ussr_integer(
                    argc > 0 ? (long)(argc - 1) : 0
                );
                vector_value = ussr_vector_value(args_vector);

                vm.registers[0] = arg_count_value;
                vm.registers[1] = vector_value;
                vm.entry_function = (int)fi;
                break;
            }
        }
    }

    result = vm_execute(&vm, &bytecode);
    if (result == 0 && vm.entry_function >= 0 &&
        vm.registers[USSR_VM_RETURN_REG].type == USSR_INTEGER)
    {
        long exit_value = vm.registers[USSR_VM_RETURN_REG].data.integer;
        if (exit_value >= 0 && exit_value <= 255)
            result = (int)exit_value;
    }
parse_execute_after_vm:
    if (result != 0)
        fprintf(stderr, "USSR VM: execution failed (status %d)", result);
    vm_cleanup(&vm);
    ussr_bc_program_free(&bytecode);

    ussr_command_list_free(ussr_parsed_program);
    ussr_parsed_program = NULL;

    return result;
}

static int run_eval(const char *source, int argc, char **argv)
{
    int result;

    if (source == NULL || source[0] == '\0')
        return 0;

    result = parse_and_execute(source, argc, argv);

    return result;
}

static int run_file(const char *filename, int argc, char **argv)
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

    result = parse_and_execute(ussr_pp_output(&pp), argc, argv);
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
    printf("USSR v%d.%d\n",USSR_VERSION_MAJOR, USSR_VERSION_MINOR);
    printf("Enter a command list or press Ctrl-D to exit.\n\n");

    ussr_autocomplete_init();
#ifdef _WIN32	
	worstlineSetCompletionCallback(ussr_autocomplete_callback);	
#else
	bestlineSetCompletionCallback(ussr_autocomplete_callback);
#endif

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
        ussr_autocomplete_record_history(line);

        result = ussr_pp_process_line(&pp, line);
        bestlineFree(line);

        if (result != 0)
        {
            source_length = 0;
            bracket_depth = 0;

            if (source != NULL)
                source[0] = '\0';

            ussr_autocomplete_set_source(NULL);
            ussr_pp_clear_output(&pp);
            continue;
        }

        processed = ussr_pp_output(&pp);

        if (processed == NULL || processed[0] == '\0')
        {
            ussr_autocomplete_set_source(NULL);
            ussr_pp_clear_output(&pp);
            continue;
        }

        if (append_source(
                &source,
                &source_length,
                &capacity,
                processed) != 0)
        {
            ussr_autocomplete_cleanup();
            ussr_pp_clear_output(&pp);
            free(source);
            ussr_pp_cleanup(&pp);
            return EXIT_FAILURE;
        }

        bracket_depth = count_brackets(processed, bracket_depth);
        ussr_autocomplete_set_source(source);
        ussr_pp_clear_output(&pp);

        if (bracket_depth > 0)
            continue;

        result = parse_and_execute(source, 0, NULL);

        source_length = 0;
        if (source != NULL)
            source[0] = '\0';

        ussr_autocomplete_set_source(NULL);

        if (result != 0)
            continue;
    }

    ussr_autocomplete_cleanup();
    free(source);
    ussr_pp_cleanup(&pp);

    printf("\n");
    return EXIT_SUCCESS;
}

static void
version_short(void)
{
    printf(
        "%s %u.%u.%u\n",
        _USSR_STRING,
        USSR_VERSION_MAJOR,
        USSR_VERSION_MINOR,
        USSR_VERSION_PATCH
    );
    exit(EXIT_SUCCESS);
}

static void
version_long(void)
{
    printf(
        "%s %u.%u.%u\n",
        _USSR_STRING,
        USSR_VERSION_MAJOR,
        USSR_VERSION_MINOR,
        USSR_VERSION_PATCH
    );
    printf("Compiled %s at %s\n", __DATE__, __TIME__);

#if defined(__clang__) && !defined(__EMSCRIPTEN__)
    printf("Compiler: Clang/LLVM %d.%d\n", __clang_major__, __clang_minor__);
#elif defined(__GNUC__) || defined(__GNUG__)
    printf("Compiler: GCC %d.%d\n", __GNUC__, __GNUC_MINOR__);
#elif defined(_MSC_VER)
    printf("Compiler: Microsoft Visual Studio %d\n", _MSC_VER);
#elif defined(__INTEL_COMPILER)
    printf("Compiler: Intel ICC %d\n", __INTEL_COMPILER);
#elif defined(__TINYC__)
    printf("Compiler: Tiny CC %d\n", __TINYC__);
#elif defined(__EMSCRIPTEN__)
    printf(
        "Compiler: Emscripten %d.%d\n",
        __EMSCRIPTEN_major__,
        __EMSCRIPTEN_minor__
    );
#else
    printf("Compiler: unknown\n");
#endif

    exit(EXIT_SUCCESS);
}

static void
usage(char* prog_name)
{
	fprintf(stderr, "Usage: %s [-v] [-e code] script.su \n", prog_name);
	exit(EXIT_SUCCESS);
	return;
}

int main(int argc, char **argv)
{
    /*
     * --version and -v deliberately map to two different internal
     * codes ('V' vs 'v') even though both are "the version flag" --
     * that's the whole point: getopt_long lets a long option report
     * whatever val it likes, so the short and long forms can trigger
     * genuinely different output (short report vs. long report)
     * instead of being forced to behave identically the way most
     * -x/--xxx pairs do.
     */
    static const struct option long_options[] = {
        { "version", no_argument, NULL, 'V' },
        { "eval", required_argument, NULL, 'e' },
        { NULL, 0, NULL, 0 }
    };

    int result;
    int opt;
    const char *eval_source = NULL;
	
	srand(time(NULL));
	seed_xrp32(rand());
	
    while ((opt = getopt_long(argc, argv, "ve:", long_options, NULL)) != -1)
    {
        switch (opt)
        {
        case 'v':
            version_short();
            break; /* unreachable -- version_short() calls exit() */

        case 'V':
            version_long();
            break; /* unreachable -- version_long() calls exit() */

        case 'e':
            eval_source = optarg;
            break;

        case '?':
			usage(argv[0]);
		break;
		default:
		break;
        }
    }

    ussr_init();

    /* moscow.su is the USSR shell startup file, analogous to a shell rc.
     * It is optional and is loaded from the current working directory. */
    {
        FILE *moscow = fopen("moscow.su", "rb");
        if (moscow != NULL)
        {
            char *moscow_argv[] = { (char *)"moscow.su", NULL };
            fclose(moscow);
            if (run_file("moscow.su", 1, moscow_argv) != EXIT_SUCCESS)
            {
                ussr_cleanup();
                return EXIT_FAILURE;
            }
        }
    }

    if (eval_source != NULL)
    {
        result = run_eval(
            eval_source,
            argc - optind,
            argv + optind
        );
    }
    else if (optind < argc)
        result = run_file(argv[optind], argc - optind, argv + optind);
    else
        result = run_repl();

    ussr_cleanup();
    return result;
}
