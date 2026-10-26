#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <time.h>
#include <errno.h>

#ifdef _WIN32
#include <direct.h>
#include <io.h>
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
#include "ussr_ru.h"
#include "uno.h"
#include "uno.h"
#include "ussr_oop_builtins.h"
#include "keybind.h"
#include "alias.h"
#include "history.h"
#include "vm.h"
#include "scheduler.h"
#include "eventq.h"
#include "gc.h"

/* VM runtime state lives here. Bytecode generation lives in ussr_bytecode.c.
 * The ussr_vm_t/ussr_vm_frame_t types and the vm_execute() contract moved
 * to vm.h so the scheduler (scheduler.c) can own and switch VM contexts. */

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

/* 2 or 3 for a Vec2/Vec3 struct instance, otherwise 0. */
static int vm_vector_dim(const ussr_value_t *v)
{
    if (v == NULL || v->type != USSR_STRUCT || v->data.instance == NULL ||
        v->data.instance->type == NULL)
        return 0;
    if (strcmp(v->data.instance->type->name, "Vec2") == 0)
        return 2;
    if (strcmp(v->data.instance->type->name, "Vec3") == 0)
        return 3;
    return 0;
}

static const char *vm_vector_field[3] = { "x", "y", "z" };

/* Component i as a number; -1 with a message if it is null or not numeric. */
static int vm_vector_component(
    const ussr_value_t *v,
    int i,
    ussr_value_t *out)
{
    if (!ussr_struct_get_field(v->data.instance, vm_vector_field[i], out))
        return -1;
    if (out->type != USSR_INTEGER && out->type != USSR_REAL)
    {
        fprintf(stderr, "USSR VM: %s component %s is not a number\n",
                v->data.instance->type->name, vm_vector_field[i]);
        ussr_value_free(out);
        return -1;
    }
    return 0;
}

static const char *vm_type_label(const ussr_value_t *v)
{
    if (vm_vector_dim(v) != 0)
        return v->data.instance->type->name;
    switch (v->type)
    {
        case USSR_NULL: return "null";
        case USSR_INTEGER: return "integer";
        case USSR_REAL: return "real";
        case USSR_STRING: return "string";
        case USSR_BOOLEAN: return "boolean";
        case USSR_VECTOR: return "vector";
        case USSR_STRUCT: return "struct";
    }
    return "value";
}

static int vm_binary(
    uint8_t opcode,
    const ussr_value_t *left,
    const ussr_value_t *right,
    ussr_value_t *out);

/*
 * Vec2/Vec3 operators: + and - between vectors of one dimension, * and /
 * by a number, = and != comparing components. Returns 0 when handled
 * (*out set), -1 on an error (already reported), 1 when neither operand is
 * a math vector.
 */
static int vm_vector_binary(
    uint8_t opcode,
    const ussr_value_t *left,
    const ussr_value_t *right,
    ussr_value_t *out)
{
    int dl = vm_vector_dim(left);
    int dr = vm_vector_dim(right);
    int dim = dl != 0 ? dl : dr;
    const ussr_value_t *vector = dl != 0 ? left : right;
    const ussr_value_t *other = dl != 0 ? right : left;
    ussr_value_t parts[3];
    const char *verb;
    int i;

    if (dl == 0 && dr == 0)
        return 1;

    if (opcode == USSR_BC_EQ || opcode == USSR_BC_NE)
    {
        int same = dl == dr;
        for (i = 0; same && i < dim; ++i)
        {
            ussr_value_t a, b, eq;
            if (vm_vector_component(left, i, &a) != 0)
                return -1;
            if (vm_vector_component(right, i, &b) != 0)
            {
                ussr_value_free(&a);
                return -1;
            }
            same = vm_binary(USSR_BC_EQ, &a, &b, &eq) == 0 &&
                   eq.data.boolean;
            ussr_value_free(&a);
            ussr_value_free(&b);
        }
        *out = ussr_boolean(opcode == USSR_BC_EQ ? same : !same);
        return 0;
    }

    verb = opcode == USSR_BC_ADD ? "add" :
           opcode == USSR_BC_SUB ? "subtract" :
           opcode == USSR_BC_MUL ? "multiply" :
           opcode == USSR_BC_DIV ? "divide" : NULL;
    if (verb == NULL)
    {
        fprintf(stderr,
                "USSR VM: this operator is not defined for %s; vectors support + - * / = !=\n",
                vector->data.instance->type->name);
        return -1;
    }
    if (opcode == USSR_BC_ADD || opcode == USSR_BC_SUB)
    {
        if (dl == 0 || dr == 0 || dl != dr)
        {
            fprintf(stderr, "USSR VM: cannot %s %s and %s\n", verb,
                    vm_type_label(left), vm_type_label(right));
            return -1;
        }
    }
    else
    {
        /* vector * number, number * vector, vector / number */
        int number_left = left->type == USSR_INTEGER || left->type == USSR_REAL;
        int number_right = right->type == USSR_INTEGER || right->type == USSR_REAL;
        int ok = (dl != 0 && dr == 0 && number_right) ||
                 (opcode == USSR_BC_MUL && dl == 0 && dr != 0 && number_left);
        if (!ok)
        {
            fprintf(stderr,
                    "USSR VM: cannot %s %s by %s (vectors %s by a number; use vdot/vcross for vector products)\n",
                    verb, vm_type_label(left), vm_type_label(right),
                    opcode == USSR_BC_MUL ? "multiply" : "divide");
            return -1;
        }
    }
    if (opcode == USSR_BC_DIV &&
        ((right->type == USSR_INTEGER && right->data.integer == 0) ||
         (right->type == USSR_REAL && right->data.real == 0.0)))
    {
        fprintf(stderr, "USSR VM: cannot divide %s by zero\n",
                vector->data.instance->type->name);
        return -1;
    }

    for (i = 0; i < dim; ++i)
    {
        ussr_value_t a, b;
        int status;

        if (vm_vector_component(vector, i, &a) != 0)
        {
            while (i-- > 0)
                ussr_value_free(&parts[i]);
            return -1;
        }
        if (opcode == USSR_BC_ADD || opcode == USSR_BC_SUB)
        {
            if (vm_vector_component(other, i, &b) != 0)
            {
                ussr_value_free(&a);
                while (i-- > 0)
                    ussr_value_free(&parts[i]);
                return -1;
            }
            status = vm_binary(opcode, &a, &b, &parts[i]);
            ussr_value_free(&b);
        }
        else
            status = vm_binary(opcode, &a, other, &parts[i]);
        ussr_value_free(&a);
        if (status != 0)
        {
            while (i-- > 0)
                ussr_value_free(&parts[i]);
            return -1;
        }
    }
    {
        ussr_struct_instance_t *instance = ussr_struct_instantiate(
            vector->data.instance->type->name, parts, (size_t)dim);
        for (i = 0; i < dim; ++i)
            ussr_value_free(&parts[i]);
        if (instance == NULL)
            return -1;
        *out = ussr_struct_value(instance);
    }
    return 0;
}

static int vm_equal(const ussr_value_t *a, const ussr_value_t *b)
{
    if (a == NULL || b == NULL)
        return 0;
    if (a->type == USSR_INTEGER && b->type == USSR_INTEGER)
        return a->data.integer == b->data.integer;
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

    if (left->type == USSR_STRUCT || right->type == USSR_STRUCT)
    {
        int vector_status = vm_vector_binary(opcode, left, right, out);
        if (vector_status != 1)
            return vector_status;
    }

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
                ussr_int_t a = left->data.integer;
                ussr_int_t b = right->data.integer;
                if (opcode == USSR_BC_ADD) *out = ussr_integer(ussr_int_add(a, b));
                else if (opcode == USSR_BC_SUB) *out = ussr_integer(ussr_int_sub(a, b));
                else if (opcode == USSR_BC_MUL) *out = ussr_integer(ussr_int_mul(a, b));
                else *out = ussr_integer(ussr_int_mod(a, b));
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
            if (left->type == USSR_INTEGER && right->type == USSR_INTEGER)
            {
                ussr_int_t a = left->data.integer, b = right->data.integer;
                *out = ussr_boolean(opcode == USSR_BC_LT ? a < b :
                                    opcode == USSR_BC_LE ? a <= b :
                                    opcode == USSR_BC_GT ? a > b : a >= b);
            }
            else if (opcode == USSR_BC_LT) *out = ussr_boolean(vm_real(left) < vm_real(right));
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
            if ((opcode == USSR_BC_SHL || opcode == USSR_BC_SHR) &&
                (right->data.integer < 0 || right->data.integer > 63))
            {
                fprintf(stderr, "USSR VM: shift count " USSR_INT_FMT " is outside 0..63\n",
                        right->data.integer);
                return -1;
            }
            if (opcode == USSR_BC_XOR) *out = ussr_integer(left->data.integer ^ right->data.integer);
            else if (opcode == USSR_BC_BAND) *out = ussr_integer(left->data.integer & right->data.integer);
            else if (opcode == USSR_BC_BOR) *out = ussr_integer(left->data.integer | right->data.integer);
            /* << shifts the two's complement bits; >> keeps the sign. */
            else if (opcode == USSR_BC_SHL) *out = ussr_integer((ussr_int_t)((uint64_t)left->data.integer << right->data.integer));
            else *out = ussr_integer(left->data.integer >> right->data.integer);
            return 0;
    }
    return -1;
}

int vm_fold_binary(
    uint8_t opcode,
    const ussr_value_t *left,
    const ussr_value_t *right,
    ussr_value_t *out)
{
    if (left->type == USSR_STRUCT || left->type == USSR_VECTOR ||
        right->type == USSR_STRUCT || right->type == USSR_VECTOR)
        return -1;
    /* vm_binary reports a bad shift count; the VM should, at run time. */
    if ((opcode == USSR_BC_SHL || opcode == USSR_BC_SHR) &&
        right->type == USSR_INTEGER &&
        (right->data.integer < 0 || right->data.integer > 63))
        return -1;
    return vm_binary(opcode, left, right, out);
}

static void vm_frame_free(ussr_vm_frame_t *frame);

void vm_init(ussr_vm_t *vm)
{
    size_t i;
    memset(vm, 0, sizeof(*vm));
    for (i = 0; i < USSR_VM_REGISTER_COUNT; ++i)
        vm->registers[i] = ussr_null();

    vm->resume_value = ussr_null();

    /* No script entry function unless init(...) is found. */
    vm->entry_function = -1;
}

void vm_cleanup(ussr_vm_t *vm)
{
    size_t i;
    for (i = 0; i < USSR_VM_REGISTER_COUNT; ++i)
        ussr_value_free(&vm->registers[i]);
    while (vm->frame_count > 0)
    {
        vm_frame_free(&vm->frames[vm->frame_count - 1]);
        --vm->frame_count;
    }
    free(vm->frames);
    vm->frames = NULL;
    vm->frame_capacity = 0;
    ussr_value_free(&vm->resume_value);
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
    memset(frame, 0, sizeof(*frame));
}

static int vm_push_frame(
    ussr_vm_t *vm,
    const ussr_bc_function_t *function,
    const ussr_bc_instruction_t *ins)
{
    ussr_vm_frame_t *frame;
    size_t i;

    if (vm->frame_count >= USSR_VM_MAX_CALLS) return -1;
    if (vm->frame_count == vm->frame_capacity)
    {
        size_t n = vm->frame_capacity == 0 ? 16 : vm->frame_capacity * 2;
        ussr_vm_frame_t *q = realloc(vm->frames, n * sizeof(*q));
        if (q == NULL) return -1;
        vm->frames = q; vm->frame_capacity = n;
    }
    frame = &vm->frames[vm->frame_count];
    memset(frame, 0, sizeof(*frame));
    frame->return_ip = vm->ip;
    frame->return_register = ins->c;
    frame->function_index = ins->immediate;
    /* The registers move into the frame (the callee starts with empty
     * ones and RET moves them back): copying them, strings and all, on
     * every call made calls cost as much as the text they held. */
    for (i = 0; i < USSR_VM_REGISTER_COUNT; ++i)
    {
        frame->saved_registers[i] = vm->registers[i];
        vm->registers[i] = ussr_null();
    }

    /* Fresh locals: parameters, the return variable and everything the
     * function assigns that is not declared global. */
    if (ussr_locals_push(function->locals, function->local_count) != 0)
    {
        for (i = 0; i < USSR_VM_REGISTER_COUNT; ++i)
        {
            vm->registers[i] = frame->saved_registers[i];
            frame->saved_registers[i] = ussr_null();
        }
        vm_frame_free(frame);
        return -1;
    }
    ++vm->frame_count;
    return 0;
}

static int vm_call(
    ussr_vm_t *vm,
    const ussr_bc_program_t *program,
    const ussr_bc_instruction_t *ins)
{
    const ussr_bc_function_t *f;
    size_t i;
    if (ins->immediate >= program->function_count || ins->b != program->functions[ins->immediate].parameter_count)
        return -1;
    if ((unsigned)ins->a + (unsigned)ins->b > USSR_VM_RETURN_REG)
        return -1;
    f = &program->functions[ins->immediate];
    if (vm_push_frame(vm, f, ins) != 0)
        return -1;
    /* the arguments are in the caller's registers, now in the frame */
    for (i = 0; i < f->parameter_count; ++i)
    {
        const ussr_value_t *argument =
            &vm->frames[vm->frame_count - 1].saved_registers[ins->a + i];
        if (ussr_set_variable(f->parameters[i], argument) != 0) return -1;
    }
    vm->ip = f->entry;
    return 0;
}

static int vm_return(
    ussr_vm_t *vm,
    const ussr_bc_program_t *program)
{
    ussr_vm_frame_t *frame;
    const ussr_bc_function_t *f;
    ussr_value_t result;
    size_t i;
    const ussr_value_t *v;
    if (vm->frame_count == 0) return -1;
    frame = &vm->frames[vm->frame_count - 1];
    if (frame->function_index >= program->function_count) return -1;
    f = &program->functions[frame->function_index];
    v = ussr_get_variable(f->return_name);
    result = v != NULL ? ussr_value_copy(v) : ussr_null();
    ussr_locals_pop();
    for (i = 0; i < USSR_VM_REGISTER_COUNT; ++i) { ussr_value_free(&vm->registers[i]); vm->registers[i] = frame->saved_registers[i]; frame->saved_registers[i] = ussr_null(); }
    vm->ip = frame->return_ip;
    ussr_value_free(&vm->registers[frame->return_register]);
    vm->registers[frame->return_register] = result;
    vm_frame_free(frame);
    --vm->frame_count;
    return 0;
}

int vm_spawn_call(
    ussr_vm_t *vm,
    const ussr_bc_program_t *program,
    size_t function_index,
    const ussr_value_t *args,
    size_t arg_count)
{
    ussr_bc_instruction_t call;
    size_t i;

    if (vm == NULL || program == NULL ||
        function_index >= program->function_count ||
        program->functions[function_index].parameter_count != arg_count ||
        program->code_count == 0)
        return -1;

    for (i = 0; i < arg_count; ++i)
    {
        ussr_value_free(&vm->registers[i]);
        vm->registers[i] = ussr_value_copy(&args[i]);
    }

    memset(&call, 0, sizeof(call));
    call.opcode = USSR_BC_CALL;
    call.a = 0;
    call.b = (uint8_t)arg_count;
    call.c = USSR_VM_RETURN_REG;
    call.immediate = (uint32_t)function_index;

    /* Return from the function straight into the program HALT. */
    vm->ip = (uint32_t)(program->code_count - 1);
    return vm_call(vm, program, &call);
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

/* strlen(text), but at most max: the bytes before any NUL, scanning no
 * further than max (strnlen is POSIX, not ISO C). */
static size_t vm_text_reach(const char *text, size_t max)
{
    size_t n = 0;

    while (n < max && text[n] != '\0')
        ++n;
    return n;
}

/*
 * Sites are recorded as their instructions are emitted, so they are in
 * instruction order: binary search, with the linear scan kept for a
 * site it does not find (a builtin call runs this every time, and big
 * programs have thousands of sites).
 */
static const ussr_command_t *vm_find_oop_site(
    const ussr_bc_program_t *program,
    uint32_t instruction
)
{
    size_t i;

    if (program == NULL)
        return NULL;

    {
        size_t low = 0;
        size_t high = program->oop_site_count;

        while (low < high)
        {
            size_t middle = low + (high - low) / 2;

            if (program->oop_sites[middle].instruction == instruction)
                return program->oop_sites[middle].command;
            if (program->oop_sites[middle].instruction < instruction)
                low = middle + 1;
            else
                high = middle;
        }
    }
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

    {
        size_t low = 0;
        size_t high = program->scan_site_count;

        while (low < high)
        {
            size_t middle = low + (high - low) / 2;

            if (program->scan_sites[middle].instruction == instruction)
                return program->scan_sites[middle].command;
            if (program->scan_sites[middle].instruction < instruction)
                low = middle + 1;
            else
                high = middle;
        }
    }
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
        ussr_int_t number;

        errno = 0;
        number = strtoll(text, &tail, 10);
        if (tail == text || *tail != '\0' || errno == ERANGE)
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
        printf(USSR_INT_FMT, value->data.integer);
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

/*
 * load(p): "file.su" ["entry"]
 *
 * Loads a USSR source file as a new scheduled process — the third
 * level of the init hierarchy (below REPL chunks and the script's
 * global init). The file is preprocessed, parsed, and compiled into a
 * program owned by the new process; its top-level code does NOT run
 * (same semantics as a script with init). The entry point is:
 *
 *   1. the explicit second argument, when given;
 *   2. otherwise the first top-level process(_): name mark found in
 *      the loaded file;
 *   3. otherwise a definition called init, if one exists.
 *
 * The entry definition takes either no parameters or the usual
 * (arg_cnt, arg_vec) pair, which then receives 0 and an empty vector.
 * p receives the new process id.
 */
static int vm_do_load(
    const ussr_value_t *values,
    size_t value_count,
    const ussr_command_t *source_command,
    uint8_t ins_a,
    ussr_value_t *out)
{
    ussr_preprocessor_t pp;
    YY_BUFFER_STATE buffer;
    ussr_command_list_t *old_program;
    ussr_bc_program_t bc;
    ussr_sched_program_t *program;
    const char *filename;
    const char *entry = NULL;
    const char *first_mark = NULL;
    ussr_value_t args[2];
    size_t arg_count = 0;
    long pid;
    size_t fi;
    int function_index = -1;

    (void)ins_a;

    if (value_count < 1 || values[0].type != USSR_STRING)
    {
        fprintf(stderr, "USSR: load(p): \"file.su\" [\"entry\"]\n");
        return -1;
    }

    filename = values[0].data.string;

    /* Explicit entry name: string value or bare name in source. */
    if (value_count >= 2)
    {
        if (values[1].type == USSR_STRING)
        {
            entry = values[1].data.string;
        }
        else if (source_command != NULL &&
                 source_command->argument_count >= 2)
        {
            const ussr_argument_t *argument =
                &source_command->arguments[1];
            if (argument->type == USSR_ARGUMENT_EXPRESSION &&
                argument->data.expression != NULL &&
                argument->data.expression->type == USSR_EXPR_VARIABLE)
            {
                entry = argument->data.expression->data.variable;
            }
        }

        if (entry == NULL)
        {
            fprintf(stderr,
                    "USSR: load entry must be a definition name\n");
            return -1;
        }
    }

    if (ussr_pp_init(&pp) != 0)
        return -1;

    if (ussr_pp_process_file(&pp, filename) != 0)
    {
        fprintf(stderr, "USSR: load: cannot read '%s'\n", filename);
        ussr_pp_cleanup(&pp);
        return -1;
    }

    ussr_parse_begin(filename, ussr_pp_locate, &pp);
    buffer = yy_scan_string(ussr_pp_output(&pp));

    if (buffer == NULL)
    {
        ussr_parse_end();
        ussr_pp_cleanup(&pp);
        return -1;
    }

    /* Parse with the parser's global program slot saved/restored,
     * exactly like eval does. */
    old_program = ussr_parsed_program;
    ussr_parsed_program = NULL;

    if (yyparse() != 0 || ussr_parsed_program == NULL)
    {
        yy_delete_buffer(buffer);
        ussr_parse_end();
        ussr_pp_cleanup(&pp);
        ussr_parsed_program = old_program;
        fprintf(stderr, "USSR: load: syntax error in '%s'\n", filename);
        return -1;
    }

    yy_delete_buffer(buffer);
    ussr_parse_end();
    ussr_pp_cleanup(&pp);

    /* Scan the loaded file's top level for process(_): name marks. */
    {
        const ussr_command_t *command;

        for (command = ussr_parsed_program->head;
             command != NULL;
             command = command->next)
        {
            const char *mark = NULL;

            if (command->name == NULL ||
                strcmp(command->name, "process") != 0 ||
                command->argument_count < 1)
                continue;

            if (command->arguments[0].type == USSR_ARGUMENT_VALUE &&
                command->arguments[0].data.value.type == USSR_STRING)
            {
                mark = command->arguments[0].data.value.data.string;
            }
            else if (command->arguments[0].type ==
                         USSR_ARGUMENT_EXPRESSION &&
                     command->arguments[0].data.expression != NULL &&
                     command->arguments[0].data.expression->type ==
                         USSR_EXPR_VARIABLE)
            {
                mark = command->arguments[0]
                    .data.expression->data.variable;
            }

            if (mark != NULL)
            {
                if (first_mark == NULL)
                    first_mark = mark;
                ussr_sched_mark_process(mark);
            }
        }
    }

    if (ussr_bc_compile(ussr_parsed_program, &bc) != 0)
    {
        fprintf(stderr, "USSR: load: compilation failed for '%s'\n",
                filename);
        ussr_command_list_free(ussr_parsed_program);
        ussr_parsed_program = old_program;
        return -1;
    }

    /* The wrapper adopts the AST: OOP/scan sites point into it. */
    program = ussr_sched_program_wrap(&bc, ussr_parsed_program);
    ussr_parsed_program = old_program;

    if (program == NULL)
    {
        ussr_bc_program_free(&bc);
        return -1;
    }

    /* Entry-point hierarchy: explicit > first process mark > init. */
    if (entry == NULL)
        entry = first_mark;
    if (entry == NULL)
        entry = "init";

    for (fi = 0; fi < bc.function_count; ++fi)
    {
        if (strcmp(bc.functions[fi].name, entry) == 0)
        {
            function_index = (int)fi;
            break;
        }
    }

    if (function_index < 0)
    {
        fprintf(stderr,
                "USSR: load: '%s' has no process entry '%s'\n",
                filename, entry);
        ussr_sched_program_release(program);
        return -1;
    }

    /* init-style ABI: (arg_cnt, arg_vec) gets 0 and an empty vector. */
    args[0] = ussr_null();
    args[1] = ussr_null();

    if (bc.functions[function_index].parameter_count == 2)
    {
        ussr_vector_t *empty = ussr_vector_create("string");
        if (empty == NULL)
        {
            ussr_sched_program_release(program);
            return -1;
        }
        args[0] = ussr_integer(0);
        args[1] = ussr_vector_value(empty);
        arg_count = 2;
    }
    else if (bc.functions[function_index].parameter_count != 0)
    {
        fprintf(stderr,
                "USSR: load entry '%s' must take 0 or 2 parameters\n",
                entry);
        ussr_sched_program_release(program);
        return -1;
    }

    pid = ussr_sched_spawn_func(
        ussr_sched_current(),
        program,
        (size_t)function_index,
        args,
        arg_count,
        entry,
        0 /* exec semantics: fresh empty scope */
    );

    ussr_value_free(&args[0]);
    ussr_value_free(&args[1]);
    ussr_sched_program_release(program);

    if (pid < 0)
        return -1;

    *out = ussr_integer(pid);
    return 1;
}

/* try(failed): "function" args ...
 * Calls one of the program's functions with args in a nested VM, in this
 * process (it shares the globals), and catches its failure: failed is 1
 * when the call failed, its error already reported, else 0. The call's
 * result is not kept. Lets a REPL written in USSR (russia.su) carry on
 * after an error in an entry, as ussr's own REPL does. */
static int vm_try_call(
    const ussr_bc_program_t *program,
    const ussr_argument_t *av,
    size_t argument_count,
    ussr_value_t *out)
{
    const char *name;
    size_t index;
    size_t i;
    size_t depth;
    ussr_vm_t nested;
    ussr_bc_instruction_t call;
    int status;

    if (argument_count < 1 || av[0].type != USSR_ARGUMENT_VALUE ||
        av[0].data.value.type != USSR_STRING)
    {
        fprintf(stderr, "USSR: try(failed): \"function\" args ...\n");
        return -1;
    }
    name = av[0].data.value.data.string;
    for (index = 0; index < program->function_count; ++index)
    {
        if (strcmp(program->functions[index].name, name) == 0)
            break;
    }
    if (index == program->function_count)
    {
        fprintf(stderr, "USSR: try: no function '%s'\n", name);
        return -1;
    }
    if (program->functions[index].parameter_count != argument_count - 1 ||
        argument_count - 1 > USSR_VM_RETURN_REG || program->code_count == 0)
    {
        fprintf(stderr, "USSR: try: '%s' takes %zu arguments\n", name,
                program->functions[index].parameter_count);
        return -1;
    }
    for (i = 1; i < argument_count; ++i)
    {
        if (av[i].type != USSR_ARGUMENT_VALUE)
        {
            fprintf(stderr, "USSR: try: arguments must be values\n");
            return -1;
        }
    }

    vm_init(&nested);
    nested.running = 1;
    nested.nested = 1;
    for (i = 1; i < argument_count; ++i)
        nested.registers[i - 1] = ussr_value_copy(&av[i].data.value);

    memset(&call, 0, sizeof(call));
    call.opcode = USSR_BC_CALL;
    call.a = 0;
    call.b = (uint8_t)(argument_count - 1);
    call.c = USSR_VM_RETURN_REG;
    call.immediate = (uint32_t)index;

    /* Return straight to the program's final HALT, as init does. */
    depth = ussr_locals_depth();
    nested.ip = (uint32_t)(program->code_count - 1);
    status = vm_call(&nested, program, &call);
    if (status == 0)
        status = vm_execute(&nested, program, 0);

    /* A failure leaves the calls it was in (and any eval inside them)
     * on the locals stack: unwind to where the call started. */
    while (ussr_locals_depth() > depth)
        ussr_locals_pop();
    vm_cleanup(&nested);

    *out = ussr_integer(status == USSR_VM_STATUS_DONE ? 0 : 1);
    return 0;
}

#ifdef USSR_PROFILE
/*
 * Build with -DUSSR_PROFILE (nmake ... "OPT=/O2 /GL /DUSSR_PROFILE") to get a
 * profile on stderr at exit: cycles and counts per opcode and per called
 * name (user functions for CALL, builtins for OOP). Cycles are charged to
 * an instruction up to the start of the next one, so a CALL's own cost
 * excludes its body. Used by the optimization cycle (docs/OPTIMIZATIONS.md).
 */
#if defined(_MSC_VER)
#include <intrin.h>
#else
#include <x86intrin.h>
#endif

#define VM_PROF_NAMES 4096
typedef struct { const char *name; unsigned long long count, cycles; } vm_prof_t;
static vm_prof_t vm_prof_ops[256];
static vm_prof_t vm_prof_names[VM_PROF_NAMES];
static int vm_prof_registered;
static vm_prof_t *vm_prof_last_op, *vm_prof_last_name;
static unsigned long long vm_prof_t0;

static vm_prof_t *vm_prof_slot(const char *name)
{
    /* one bucket per distinct string; collisions probe linearly */
    size_t h = 0, i;
    const char *c;
    for (c = name; *c != '\0'; ++c)
        h = h * 31 + (unsigned char)*c;
    for (i = 0; i < VM_PROF_NAMES; ++i)
    {
        vm_prof_t *e = &vm_prof_names[(h + i) % VM_PROF_NAMES];
        /* copied: the program and its strings are gone by atexit */
        /* copied: the program and its strings are gone by atexit */
        if (e->name == NULL) { e->name = _strdup(name); return e; }
        if (strcmp(e->name, name) == 0) return e;
    }
    return NULL;
}

static int vm_prof_cmp(const void *a, const void *b)
{
    const vm_prof_t *x = a, *y = b;
    return x->cycles < y->cycles ? 1 : x->cycles > y->cycles ? -1 : 0;
}

static void vm_prof_dump(void)
{
    static vm_prof_t sorted[VM_PROF_NAMES];
    unsigned long long total = 0;
    size_t i, n = 0;

    for (i = 0; i < 256; ++i)
        total += vm_prof_ops[i].cycles;
    fprintf(stderr, "\n== USSR profile: opcodes (Mcycles, count, %%)\n");
    memcpy(sorted, vm_prof_ops, sizeof(vm_prof_ops));
    qsort(sorted, 256, sizeof(*sorted), vm_prof_cmp);
    for (i = 0; i < 256 && sorted[i].count != 0; ++i)
        fprintf(stderr, "%-16s %10.1f %12llu %5.1f\n", sorted[i].name,
                sorted[i].cycles / 1e6, sorted[i].count,
                total ? 100.0 * sorted[i].cycles / total : 0.0);
    for (i = 0; i < VM_PROF_NAMES; ++i)
        if (vm_prof_names[i].name != NULL)
            sorted[n++] = vm_prof_names[i];
    qsort(sorted, n, sizeof(*sorted), vm_prof_cmp);
    fprintf(stderr, "== names (Mcycles, count, %%)\n");
    for (i = 0; i < n && i < 60; ++i)
        fprintf(stderr, "%-24s %10.1f %12llu %5.1f\n", sorted[i].name,
                sorted[i].cycles / 1e6, sorted[i].count,
                total ? 100.0 * sorted[i].cycles / total : 0.0);
}

static void vm_prof_step(const ussr_bc_program_t *program,
                         const ussr_bc_instruction_t *ins)
{
    unsigned long long now = __rdtsc();
    vm_prof_t *op;

    if (!vm_prof_registered)
    {
        vm_prof_registered = 1;
        atexit(vm_prof_dump);
    }
    if (vm_prof_last_op != NULL)
    {
        vm_prof_last_op->cycles += now - vm_prof_t0;
        if (vm_prof_last_name != NULL)
            vm_prof_last_name->cycles += now - vm_prof_t0;
    }
    op = &vm_prof_ops[ins->opcode];
    if (op->name == NULL)
    {
        static char labels[256][8];
        sprintf(labels[ins->opcode], "op 0x%02x", (unsigned)ins->opcode);
        op->name = labels[ins->opcode];
    }
    op->count++;
    vm_prof_last_op = op;
    vm_prof_last_name = NULL;
    if (ins->opcode == USSR_BC_CALL && ins->immediate < program->function_count)
        vm_prof_last_name = vm_prof_slot(program->functions[ins->immediate].name);
    else if (ins->opcode == USSR_BC_OOP && ins->immediate < program->string_count)
        vm_prof_last_name = vm_prof_slot(program->strings[ins->immediate]);
    if (vm_prof_last_name != NULL)
        vm_prof_last_name->count++;
    vm_prof_t0 = __rdtsc();
}
#endif

int vm_execute(
    ussr_vm_t *vm,
    const ussr_bc_program_t *program,
    unsigned long quantum)
{
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
        if (vm_call(vm, program, &entry_call) != 0)
        {
            result = -1;
            goto done;
        }

        /* Entered: do not call init again on the next time slice. */
        vm->entry_function = -1;
    }

    while (vm->running)
    {
        ussr_bc_instruction_t ins;
        ussr_value_t value, temp;
        int native_handled = 0;
        const char *name;
        ussr_value_t *args = NULL;
        size_t i;

        /*
         * Cooperative preemption point: the scheduler's quantum
         * elapsed or a yield(_) command asked for a reschedule.
         */
        if (quantum != 0)
        {
            if (quantum-- == 0)
                return USSR_VM_STATUS_SUSPENDED;
        }

        if (vm->yield_request)
        {
            vm->yield_request = 0;
            return USSR_VM_STATUS_SUSPENDED;
        }

        /*
         * Blocked-command resume: the scheduler woke this process and
         * delivered the awaited/slept/child result into resume_value;
         * it belongs in the register the blocked command targeted.
         */
        if (vm->resume_pending)
        {
            vm->resume_pending = 0;
            ussr_value_free(&vm->registers[vm->resume_register]);
            vm->registers[vm->resume_register] = vm->resume_value;
            vm->resume_value = ussr_null();
        }

        if (vm->ip >= program->code_count) { result = -1; goto done; }
        if (++vm->steps > USSR_VM_MAX_STEPS) { fprintf(stderr, "USSR VM: execution step limit exceeded\n"); result = -1; goto done; }
        ins = program->code[vm->ip++];
#ifdef USSR_PROFILE
        vm_prof_step(program, &ins);
#endif

        switch (ins.opcode)
        {
            case USSR_BC_NOP: break;
            case USSR_BC_LOAD_CONST:
                if (ins.a >= USSR_VM_REGISTER_COUNT || ins.immediate >= program->constant_count) { result=-1; goto done; }
                ussr_value_free(&vm->registers[ins.a]); vm->registers[ins.a] = ussr_value_copy(&program->constants[ins.immediate]); break;
            case USSR_BC_LOAD_VAR:
                if (ins.a >= USSR_VM_REGISTER_COUNT || ins.immediate >= program->string_count) { result=-1; goto done; }
                name=program->strings[ins.immediate];
                /* b: 0 by name, 1 the call's local in slot c, 2 global
                 * (resolved by the compiler inside functions). */
                { const ussr_value_t *v=ussr_get_variable_at(name, (ussr_var_place_t)ins.b, ins.c); vm->registers[ins.a]=v?ussr_value_copy(v):ussr_null(); }
                break;
            case USSR_BC_STORE_VAR:
                if (ins.a >= USSR_VM_REGISTER_COUNT || ins.immediate >= program->string_count) { result=-1; goto done; }
                if (ussr_set_variable_at(program->strings[ins.immediate], &vm->registers[ins.a], (ussr_var_place_t)ins.b, ins.c) != 0) { result=-1; goto done; } break;
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
                value = ussr_integer((ussr_int_t)prng64_xrp32());
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
                value = ussr_integer((ussr_int_t)time(NULL));
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
                if (ins.a >= USSR_VM_RETURN_REG || (unsigned)ins.c > (unsigned)ins.a) { result=-1; goto done; }
                { size_t total=0; char *str,*cur; for(i=0;i<ins.c;++i){ if(vm->registers[i].type!=USSR_STRING){result=-1;goto done;} total+=strlen(vm->registers[i].data.string); } str=malloc(total+1); if(!str){result=-1;goto done;} cur=str; for(i=0;i<ins.c;++i){size_t n=strlen(vm->registers[i].data.string);memcpy(cur,vm->registers[i].data.string,n);cur+=n;}*cur='\0';value=ussr_string(str);free(str);ussr_value_free(&vm->registers[ins.a]);vm->registers[ins.a]=value; } break;
            case USSR_BC_CALL:
                if (vm_call(vm,program,&ins)!=0) {result=-1;goto done;} break;
            case USSR_BC_RET:
                if (vm_return(vm,program)!=0) {result=-1;goto done;} break;
            case USSR_BC_RETURN:
                if (vm->frame_count==0) {result=-1;goto done;} if(vm_return(vm,program)!=0){result=-1;goto done;} break;
            case USSR_BC_EXTERNAL:
                if (ins.immediate>=program->string_count || ins.b>USSR_VM_RETURN_REG || vm_values_from_registers(vm,0,ins.b,&args)!=0) {result=-1;goto done;}
                if (ussr_sched_current()!=NULL)
                {
                    /*
                     * Fork-exec integration: under the scheduler the
                     * child is spawned WITHOUT waiting; the process
                     * parks on the event queue and other USSR
                     * processes keep running. The child's exit code
                     * lands in register a on wakeup.
                     */
                    ussr_child_t child;
                    memset(&child,0,sizeof(child));
                    if (ussr_external_spawn_values(program->strings[ins.immediate],args,ins.b,&child)!=0)
                    { vm_free_values(args,ins.b);args=NULL; result=-1; goto done; }
                    vm_free_values(args,ins.b);args=NULL;
                    if (ussr_sched_block_on_child(child,ins.a)!=0)
                    { ussr_child_close(&child); result=-1; goto done; }
                    return USSR_VM_STATUS_SUSPENDED;
                }
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

                /*
                 * byte, len and slice on a string read it in place:
                 * programs that scan text call them once per byte, and
                 * the general path copies every argument twice (and
                 * byte measures the whole string), which made scanning
                 * a string quadratic in its length. Only the ordinary
                 * success case is handled here; anything else takes the
                 * general path, which reports it as before.
                 */
                {
                    const char *fast_name = program->strings[ins.immediate];
                    const ussr_value_t *r0 = &vm->registers[0];
                    int fast = 0;

                    value = ussr_null();
                    if (r0->type == USSR_STRING && r0->data.string != NULL)
                    {
                        const char *text = r0->data.string;
                        const ussr_value_t *r1 = &vm->registers[1];
                        const ussr_value_t *r2 = &vm->registers[2];

                        if (argument_count == 2 &&
                            strcmp(fast_name, "byte") == 0 &&
                            r1->type == USSR_INTEGER &&
                            r1->data.integer >= 0 &&
                            (ussr_int_t)vm_text_reach(
                                text, (size_t)r1->data.integer + 1) >
                                r1->data.integer)
                        {
                            value = ussr_integer((ussr_int_t)(unsigned char)
                                text[r1->data.integer]);
                            fast = 1;
                        }
                        else if (argument_count == 1 &&
                                 strcmp(fast_name, "len") == 0)
                        {
                            value = ussr_integer((ussr_int_t)strlen(text));
                            fast = 1;
                        }
                        else if (argument_count == 3 &&
                                 strcmp(fast_name, "slice") == 0 &&
                                 r1->type == USSR_INTEGER &&
                                 r2->type == USSR_INTEGER &&
                                 r1->data.integer >= 0 &&
                                 r2->data.integer >= 0 &&
                                 r1->data.integer <=
                                     INT64_MAX - r2->data.integer &&
                                 (ussr_int_t)vm_text_reach(
                                     text, (size_t)(r1->data.integer +
                                                    r2->data.integer)) ==
                                     r1->data.integer + r2->data.integer)
                        {
                            char *piece = malloc(
                                (size_t)r2->data.integer + 1);

                            if (piece != NULL)
                            {
                                memcpy(piece, text + r1->data.integer,
                                       (size_t)r2->data.integer);
                                piece[r2->data.integer] = '\0';
                                value = ussr_string(piece);
                                free(piece);
                                fast = value.type == USSR_STRING;
                            }
                        }
                    }
                    /*
                     * getf on a struct and at on a vector, likewise
                     * (docs/OPTIMIZATIONS.md, O2): russia.su's VM calls
                     * them for every instruction it runs. A missing field
                     * or an index out of range takes the general path.
                     */
                    else if (argument_count == 2 &&
                             r0->type == USSR_STRUCT &&
                             r0->data.instance != NULL &&
                             vm->registers[1].type == USSR_STRING &&
                             strcmp(fast_name, "getf") == 0)
                    {
                        fast = ussr_struct_get_field(
                            r0->data.instance,
                            vm->registers[1].data.string,
                            &value);
                    }
                    else if (argument_count == 2 &&
                             r0->type == USSR_VECTOR &&
                             r0->data.vector != NULL &&
                             vm->registers[1].type == USSR_INTEGER &&
                             vm->registers[1].data.integer >= 0 &&
                             strcmp(fast_name, "at") == 0)
                    {
                        fast = ussr_vector_get(
                            r0->data.vector,
                            (size_t)vm->registers[1].data.integer,
                            &value);
                    }
                    if (fast)
                    {
                        ussr_value_free(&vm->registers[ins.a]);
                        vm->registers[ins.a] = value;
                        break;
                    }
                }

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

                /*
                 * Scheduler commands, socket primitives, process loading,
                 * and Petersburg natives are routed at the VM boundary;
                 * scheduler operations may suspend the current process.
                 */
                {
                    ussr_value_t *sched_values = NULL;
                    int sched_status = 0;
                    int is_load = strcmp(program->strings[ins.immediate], "load") == 0;
                    int is_sched_name = is_load ||
                        strcmp(program->strings[ins.immediate], "async") == 0 ||
                        strcmp(program->strings[ins.immediate], "await") == 0 ||
                        strcmp(program->strings[ins.immediate], "sleep") == 0 ||
                        strcmp(program->strings[ins.immediate], "yield") == 0 ||
                        strcmp(program->strings[ins.immediate], "gc") == 0 ||
                        strcmp(program->strings[ins.immediate], "process") == 0 ||
                        strcmp(program->strings[ins.immediate], "wait_readable") == 0 ||
                        strcmp(program->strings[ins.immediate], "wait_writable") == 0 ||
                        strncmp(program->strings[ins.immediate], "socket_", 7) == 0 ||
                        strncmp(program->strings[ins.immediate], "petersburg_", 11) == 0;

                    if (is_sched_name &&
                        vm_values_from_registers(
                            vm, 0, (uint8_t)argument_count,
                            &sched_values) != 0)
                    {
                        result = -1;
                        goto done;
                    }

                    if (is_load)
                        sched_status = vm_do_load(
                            sched_values, argument_count,
                            source_command, ins.a, &value);
                    else if (is_sched_name)
                        sched_status = ussr_sched_dispatch(
                            ussr_sched_current_program(),
                            program->strings[ins.immediate],
                            sched_values, argument_count,
                            source_command, ins.a, &value);

                    if (is_sched_name)
                        vm_free_values(sched_values, argument_count);

                    if (sched_status < 0)
                    {
                        ussr_value_free(&value);
                        result = -1;
                        goto done;
                    }

                    if (sched_status == 2)
                    {
                        /* Blocked: suspend mid-instruction. The wake
                         * value lands in register a via resume_value. */
                        for (i = 0; i < argument_count; ++i)
                        {
                            if (av[i].type == USSR_ARGUMENT_VALUE)
                                ussr_value_free(&av[i].data.value);
                        }
                        free(av);
                        ussr_value_free(&value);
                        return USSR_VM_STATUS_SUSPENDED;
                    }

                    if (sched_status == 1)
                    {
                        for (i = 0; i < argument_count; ++i)
                        {
                            if (av[i].type == USSR_ARGUMENT_VALUE)
                                ussr_value_free(&av[i].data.value);
                        }
                        free(av);

                        ussr_value_free(&vm->registers[ins.a]);
                        vm->registers[ins.a] = value;
                        break;
                    }
                }

                /* Advanced control blocks are AST-backed builtins.  They
                 * cannot go through the ordinary OOP/external-command
                 * lookup because their command-list arguments are executable
                 * AST objects rather than VM values. */
                if (source_command != NULL &&
                    strcmp(program->strings[ins.immediate], "@") == 0)
                {
                    result = ussr_execute_command(
                        NULL,
                        "@",
                        source_command->return_name,
                        av,
                        argument_count
                    );

                    if (result == 0)
                    {
                        const ussr_value_t *stored =
                            ussr_get_variable(source_command->return_name);

                        if (stored != NULL)
                            value = ussr_value_copy(stored);
                    }
                }
                /* try needs this program's functions: see vm_try_call */
                else if (strcmp(program->strings[ins.immediate], "try") == 0)
                {
                    result = vm_try_call(program, av, argument_count, &value);
                    if (result == 0)
                        native_handled = 1;
                }
                /* terminal_* go through ussr_oop_dispatch: those versions
                 * also work on redirected stdin/stdout and deliver console
                 * input as UTF-8 bytes on Windows. */
                else if (
                    strcmp(program->strings[ins.immediate], "file_exists") == 0 ||
                    strcmp(program->strings[ins.immediate], "writefile") == 0 ||
                    strcmp(program->strings[ins.immediate], "finv_sqrt") == 0
                )
                {
                    result = ussr_execute_command(
                        NULL,
                        program->strings[ins.immediate],
                        source_command != NULL ? source_command->return_name : NULL,
                        av,
                        argument_count
                    );

                    if (result == 0 && source_command != NULL)
                    {
                        const ussr_value_t *stored =
                            ussr_get_variable(source_command->return_name);

                        if (stored != NULL)
                            value = ussr_value_copy(stored);
                    }

                    if (result == 0)
                        native_handled = 1;
                }
                else
                {
                    result = ussr_oop_dispatch(
                        program->strings[ins.immediate],
                        source_command != NULL ? source_command->return_name : NULL,
                        av,
                        argument_count,
                        &value
                    );
                }

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

                if (result == 0 && !native_handled)
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
                        /*
                         * Capture/chained variant: still synchronous,
                         * because the process layer must pump the
                         * child's pipes to completion.
                         */
                        char *output = NULL;
                        result = ussr_external_execute_values_io(
                            program->strings[ins.immediate], external_values,
                            argument_count, vm->chain_active ? vm->chain_buffer : NULL,
                            &output, &value);
                        if (result == 0 && vm_chain_set_buffer(vm, output) != 0)
                            result = -1;
                        vm_free_values(external_values, argument_count);
                        if (result != 0) goto done;
                    }
                    else if (ussr_sched_current() != NULL && !vm->nested)
                    {
                        /*
                         * Plain external under the scheduler: spawn
                         * without waiting and park this process on the
                         * child handle (fork-exec stays intact; only
                         * the wait moved to the event queue).
                         */
                        ussr_child_t child;

                        memset(&child, 0, sizeof(child));

                        if (ussr_external_spawn_values(
                                program->strings[ins.immediate],
                                external_values, argument_count,
                                &child) != 0)
                        {
                            vm_free_values(external_values, argument_count);
                            ussr_value_free(&value);
                            result = -1;
                            goto done;
                        }

                        vm_free_values(external_values, argument_count);

                        if (ussr_sched_block_on_child(child, ins.a) != 0)
                        {
                            ussr_child_close(&child);
                            ussr_value_free(&value);
                            result = -1;
                            goto done;
                        }

                        /*
                         * av was already released above with the other
                         * OOP-lookup temporaries; only the pending null
                         * result value needs dropping before we park.
                         */
                        ussr_value_free(&value);

                        return USSR_VM_STATUS_SUSPENDED;
                    }
                    else
                    {
                        result = ussr_external_execute_values(
                            program->strings[ins.immediate], external_values,
                            argument_count, &value);
                        vm_free_values(external_values, argument_count);
                        if (result != 0) goto done;
                    }
                }

                ussr_value_free(&vm->registers[ins.a]);
                vm->registers[ins.a] = value;
                break;
            }
            case USSR_BC_EVAL:
                /* eval is implemented by the VM boundary: source is parsed, compiled, then executed as bytecode. */
                if(ins.a>=USSR_VM_REGISTER_COUNT || program->strings[ins.immediate]==NULL || vm->registers[0].type!=USSR_STRING){result=-1;goto done;}
                { YY_BUFFER_STATE b; ussr_parse_begin("<eval>", NULL, NULL); b=yy_scan_string(vm->registers[0].data.string); ussr_command_list_t *old=ussr_parsed_program; ussr_parsed_program=NULL; if(!b){result=-1;goto done;} if(yyparse()!=0||ussr_parsed_program==NULL){yy_delete_buffer(b);ussr_parsed_program=old;result=-1;goto done;} yy_delete_buffer(b); ussr_bc_program_t nested; if(ussr_bc_compile(ussr_parsed_program,&nested)!=0){ussr_command_list_free(ussr_parsed_program);ussr_parsed_program=old;result=-1;goto done;} ussr_vm_t nested_vm;vm_init(&nested_vm);nested_vm.running=1;nested_vm.nested=1;result=vm_execute(&nested_vm,&nested,0); if(result==0){const ussr_value_t *v=ussr_get_variable(program->strings[ins.immediate]); value=v?ussr_value_copy(v):ussr_null();} vm_cleanup(&nested_vm);ussr_bc_program_free(&nested);ussr_command_list_free(ussr_parsed_program);ussr_parsed_program=old;if(result!=0)goto done;ussr_value_free(&vm->registers[ins.a]);vm->registers[ins.a]=value; } break;
            case USSR_BC_HALT: vm->running=0; vm->exit_code=0; break;
            default: fprintf(stderr,"USSR VM: unknown opcode 0x%02x\n",ins.opcode);result=-1;goto done;
        }
    }
    result=USSR_VM_STATUS_DONE;
done:
    return result == USSR_VM_STATUS_DONE ? USSR_VM_STATUS_DONE : USSR_VM_STATUS_ERROR;
}

int vm_execute_uuasm(
    ussr_vm_t *vm,
    const ussr_uuasm_program_t *program,
    unsigned long quantum)
{
    ussr_bc_program_t translated;
    ussr_bc_instruction_t *code;
    size_t i;
    int result;

    if (vm == NULL || program == NULL || program->code_count == 0 ||
        program->code_count > USSR_BC_MAX_CODE ||
        program->code == NULL)
        return USSR_VM_STATUS_ERROR;

    code = malloc(program->code_count * sizeof(*code));
    if (code == NULL)
        return USSR_VM_STATUS_ERROR;

    for (i = 0; i < program->code_count; ++i)
    {
        uint8_t opcode;
        if (ussr_uuasm_to_vm_opcode(program->code[i].opcode, &opcode) != 0)
        {
            fprintf(stderr,
                    "USSR VM: UUASM opcode 0x%02x has no VM mapping\n",
                    program->code[i].opcode);
            free(code);
            return USSR_VM_STATUS_ERROR;
        }
        code[i] = program->code[i];
        code[i].opcode = opcode;
    }

    translated = *program;
    translated.code = code;
    translated.code_capacity = program->code_count;
    result = vm_execute(vm, &translated, quantum);
    free(code);
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

/*
 * Parse and compile source. Returns 0 with *ast set (NULL for an empty
 * program) and bytecode filled, or -1 after printing a diagnostic.
 */
/* Set by the REPL for the entry it is about to run: as in an interactive
 * shell, aliases apply to typed lines only, never to scripts, libraries
 * or code an entry loads or evals. compile_source clears it. */
static int repl_expand_aliases = 0;

static int compile_source(const char *source, ussr_bc_program_t *bytecode,
                          ussr_command_list_t **ast)
{
    int expand_aliases = repl_expand_aliases;

    YY_BUFFER_STATE buffer;
    int result;

    *ast = NULL;
    memset(bytecode, 0, sizeof(*bytecode));

    buffer = yy_scan_string(source);

    if (buffer == NULL)
    {
        fprintf(stderr,
                "USSR: could not create parser buffer\n");
        return -1;
    }

    repl_expand_aliases = 0;
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
    if ((expand_aliases && ussr_alias_expand(ussr_parsed_program) != 0) ||
        ussr_bc_compile(ussr_parsed_program, bytecode) != 0)
    {
        ussr_command_list_free(ussr_parsed_program);
        ussr_parsed_program = NULL;
        return -1;
    }

    *ast = ussr_parsed_program;
    ussr_parsed_program = NULL;
    return 0;
}

/*
 * Run compiled bytecode as the main process. Takes ownership of bytecode
 * and ast (the syntax tree, or the site records rebuilt from a .ru file).
 */
static int run_bytecode(ussr_bc_program_t *bytecode_in,
                        ussr_command_list_t *ast,
                        int argc, char **argv, int background_ok)
{
    ussr_bc_program_t bytecode = *bytecode_in;
    ussr_sched_program_t *program;
    long main_pid;
    ussr_proc_t *main_proc;
    int entry_function = -1;
    int result;
    size_t fi;

    /*
     * The wrapper adopts the AST: OOP/scan metadata points into it,
     * and async-spawned processes may outlive this call frame.
     */
    program = ussr_sched_program_wrap(&bytecode, ast);

    if (program == NULL)
    {
        ussr_bc_program_free(&bytecode);
        if (ast != NULL)
            ussr_command_list_free(ast);
        return -1;
    }

    for (fi = 0; fi < bytecode.function_count; ++fi)
    {
        if (strcmp(bytecode.functions[fi].name, "init") == 0)
        {
            if (bytecode.functions[fi].parameter_count != 2)
            {
                fprintf(stderr,
                        "USSR: init must have parameters arg_cnt and arg_vec\n");
                ussr_sched_program_release(program);
                return -1;
            }
            entry_function = (int)fi;
            break;
        }
    }

    main_pid = ussr_sched_spawn_main(program, entry_function, "init");
    ussr_sched_program_release(program);

    if (main_pid < 0)
        return -1;

    main_proc = ussr_sched_find(main_pid);

    /* init(arg_cnt, arg_vec) receives the script arguments. */
    if (entry_function >= 0 && main_proc != NULL)
    {
        ussr_vector_t *args_vector;
        size_t i;

        args_vector = ussr_vector_create("string");
        if (args_vector == NULL)
            return -1;

        for (i = 1; i < (size_t)argc; ++i)
        {
            ussr_value_t item = ussr_string(argv[i]);
            if (ussr_vector_push(args_vector, item) != 0)
            {
                ussr_value_free(&item);
                ussr_vector_release(args_vector);
                return -1;
            }
            ussr_value_free(&item);
        }

        main_proc->vm.registers[0] = ussr_integer(
            argc > 0 ? (ussr_int_t)(argc - 1) : 0
        );
        main_proc->vm.registers[1] = ussr_vector_value(args_vector);
    }

    result = ussr_sched_run(background_ok);

    /* The main process uses the default scope: drop the locals of any
     * call it was in when it stopped (an error inside a function). */
    ussr_locals_reset(NULL);

    /* init's integer result is the exit status: 0..255 as is, any other
     * integer means failure (1). Anything else (ret never set) is 0. */
    if (result == 0 && entry_function >= 0 && main_proc != NULL &&
        main_proc->state == USSR_PROC_ZOMBIE &&
        main_proc->result.type == USSR_INTEGER)
    {
        ussr_int_t exit_value = main_proc->result.data.integer;
        result = exit_value >= 0 && exit_value <= 255 ? (int)exit_value : 1;
    }

    if (result == -1)
        fprintf(stderr, "USSR VM: execution failed (status %d)", result);

    return result;
}

/* Process exit status for a run_bytecode/parse_and_execute result: an
 * error (-1) is 1, otherwise the status init chose (0..255). */
static int exit_status(int result)
{
    return result < 0 ? EXIT_FAILURE : result;
}

static int parse_and_execute(const char *source, int argc, char **argv, int background_ok)
{
    ussr_bc_program_t bytecode;
    ussr_command_list_t *ast;

    if (source == NULL || source[0] == '\0')
        return 0;

    if (compile_source(source, &bytecode, &ast) != 0)
        return -1;

    if (ast == NULL)
        return 0;

    return run_bytecode(&bytecode, ast, argc, argv, background_ok);
}

static int has_suffix(const char *text, const char *suffix)
{
    size_t text_length = strlen(text);
    size_t suffix_length = strlen(suffix);

    return text_length >= suffix_length &&
           strcmp(text + text_length - suffix_length, suffix) == 0;
}

/* Execute a .ru bytecode file produced by -c. */
static int run_ru_file(const char *filename, int argc, char **argv)
{
    ussr_bc_program_t bytecode;
    ussr_command_list_t *sites;

    if (ussr_ru_read_file(filename, &bytecode, &sites) != 0)
        return EXIT_FAILURE;

    return exit_status(run_bytecode(&bytecode, sites, argc, argv, 0));
}

/*
 * ussr -c input.su [output.ru]: preprocess and compile without running.
 * The output defaults to the input name with its extension replaced by
 * .ru. Only the bytecode is written to the file; diagnostics go to
 * stderr and nothing is printed on success.
 */
static int compile_file(const char *input, const char *output)
{
    ussr_preprocessor_t pp;
    ussr_bc_program_t bytecode;
    ussr_command_list_t *ast;
    char *default_output = NULL;
    const char *source;
    int result;

    if (has_suffix(input, ".ru"))
    {
        fprintf(stderr, "USSR compile: '%s' is already bytecode\n", input);
        return EXIT_FAILURE;
    }

    if (output == NULL)
    {
        const char *slash = strrchr(input, '/');
        const char *back = strrchr(input, '\\');
        const char *base = slash > back ? slash : back;
        const char *dot = strrchr(input, '.');
        size_t stem = (dot != NULL && (base == NULL || dot > base)) ?
            (size_t)(dot - input) : strlen(input);

        default_output = malloc(stem + 4);
        if (default_output == NULL)
            return EXIT_FAILURE;
        memcpy(default_output, input, stem);
        strcpy(default_output + stem, ".ru");
        output = default_output;
    }

    if (ussr_pp_init(&pp) != 0)
    {
        fprintf(stderr, "USSR: could not initialize preprocessor\n");
        free(default_output);
        return EXIT_FAILURE;
    }

    if (ussr_pp_process_file(&pp, input) != 0)
    {
        ussr_pp_cleanup(&pp);
        free(default_output);
        return EXIT_FAILURE;
    }

    source = ussr_pp_output(&pp);
    if (source == NULL || source[0] == '\0')
        source = "";

    ussr_parse_begin(input, ussr_pp_locate, &pp);
    result = compile_source(source, &bytecode, &ast);
    ussr_parse_end();
    ussr_pp_cleanup(&pp);

    if (result != 0)
    {
        fprintf(stderr, "USSR compile: '%s' was not written\n", output);
        free(default_output);
        return EXIT_FAILURE;
    }

    if (ast == NULL)
    {
        /* An empty program still compiles to a single HALT. */
        bytecode.code = calloc(1, sizeof(*bytecode.code));
        if (bytecode.code == NULL)
        {
            free(default_output);
            return EXIT_FAILURE;
        }
        bytecode.code[0].opcode = USSR_BC_HALT;
        bytecode.code_count = bytecode.code_capacity = 1;
    }

    result = ussr_ru_write_file(&bytecode, output);
    ussr_bc_program_free(&bytecode);
    if (ast != NULL)
        ussr_command_list_free(ast);
    free(default_output);

    return result == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}

static int run_eval(const char *source, int argc, char **argv)
{
    int result;

    if (source == NULL || source[0] == '\0')
        return 0;

    ussr_parse_begin("<command line>", NULL, NULL);
    result = parse_and_execute(source, argc, argv, 0);
    ussr_parse_end();

    return exit_status(result);
}

static int run_file(const char *filename, int argc, char **argv)
{
    ussr_preprocessor_t pp;
    int result;

    if (has_suffix(filename, ".ru"))
        return run_ru_file(filename, argc, argv);

    {
        size_t filename_length = strlen(filename);
        if ((filename_length >= 3 &&
             strcmp(filename + filename_length - 3, ".ua") == 0) ||
            (filename_length >= 6 &&
             strcmp(filename + filename_length - 6, ".uuasm") == 0))
        {
            FILE *input = fopen(filename, "rb");
            long file_length;
            char *source;
            ussr_uuasm_program_t program;
            ussr_vm_t vm;

            (void)argc;
            (void)argv;
            if (input == NULL)
            {
                fprintf(stderr, "UUASM: cannot open '%s'\n", filename);
                return EXIT_FAILURE;
            }
            if (fseek(input, 0, SEEK_END) != 0 ||
                (file_length = ftell(input)) < 0 ||
                fseek(input, 0, SEEK_SET) != 0)
            {
                fprintf(stderr, "UUASM: cannot read '%s'\n", filename);
                fclose(input);
                return EXIT_FAILURE;
            }
            source = malloc((size_t)file_length + 1);
            if (source == NULL)
            {
                fclose(input);
                return EXIT_FAILURE;
            }
            if (fread(source, 1, (size_t)file_length, input) !=
                (size_t)file_length)
            {
                fprintf(stderr, "UUASM: short read from '%s'\n", filename);
                free(source);
                fclose(input);
                return EXIT_FAILURE;
            }
            source[file_length] = '\0';
            fclose(input);

            result = ussr_uuasm_assemble(source, &program);
            free(source);
            if (result != 0)
                return EXIT_FAILURE;

            vm_init(&vm);
            vm.running = 1;
            result = vm_execute_uuasm(&vm, &program, 0);
            vm_cleanup(&vm);
            ussr_bc_program_free(&program);
            return result == USSR_VM_STATUS_DONE ?
                EXIT_SUCCESS : EXIT_FAILURE;
        }
    }

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

    ussr_parse_begin(filename, ussr_pp_locate, &pp);
    result = parse_and_execute(ussr_pp_output(&pp), argc, argv, 0);
    ussr_parse_end();
    ussr_pp_cleanup(&pp);

    return exit_status(result);
}

/*
 * REPL prompts: PS1 for a new command, PS2 while brackets are open. Unset
 * or empty variables give the defaults "ussr> " and "... ". The value is
 * only displayed, never executed. Escapes: \u user, \h host, \w working
 * directory, \W its last component, \v version, \$ "$", \\ backslash;
 * any other backslash pair is shown as written.
 */
static void repl_prompt_add(char **out, size_t *length, size_t *capacity,
                            const char *text)
{
    size_t add = strlen(text);
    if (*length + add + 1 > *capacity)
    {
        size_t next = (*length + add + 1) * 2;
        char *grown = realloc(*out, next);
        if (grown == NULL)
            return;
        *out = grown;
        *capacity = next;
    }
    memcpy(*out + *length, text, add + 1);
    *length += add;
}

static char *repl_prompt(int continuation)
{
    const char *variable = continuation ? "PS2" : "PS1";
    char *format = ussr_env_get(variable);
    char *out = NULL;
    size_t length = 0;
    size_t capacity = 0;
    const char *p;

    if (format == NULL || format[0] == '\0')
    {
        free(format);
        format = malloc(8);
        if (format == NULL)
            return NULL;
        strcpy(format, continuation ? "... " : "ussr> ");
    }
    repl_prompt_add(&out, &length, &capacity, "");
    for (p = format; *p != '\0'; ++p)
    {
        char single[2] = { *p, '\0' };
        if (*p != '\\' || p[1] == '\0')
        {
            repl_prompt_add(&out, &length, &capacity, single);
            continue;
        }
        ++p;
        if (*p == 'u' || *p == 'h')
        {
            char *value = ussr_env_get(*p == 'u' ? "USERNAME" : "COMPUTERNAME");
            if (value == NULL)
                value = ussr_env_get(*p == 'u' ? "USER" : "HOSTNAME");
            repl_prompt_add(&out, &length, &capacity,
                            value != NULL ? value : "?");
            free(value);
        }
        else if (*p == 'w' || *p == 'W')
        {
            char cwd[4096];
            const char *shown = "?";
#ifdef _WIN32
            if (_getcwd(cwd, (int)sizeof(cwd)) != NULL)
#else
            if (getcwd(cwd, sizeof(cwd)) != NULL)
#endif
            {
                shown = cwd;
                if (*p == 'W')
                {
                    const char *slash = strrchr(cwd, '/');
                    const char *back = strrchr(cwd, '\\');
                    const char *last = slash > back ? slash : back;
                    if (last != NULL && last[1] != '\0')
                        shown = last + 1;
                }
            }
            repl_prompt_add(&out, &length, &capacity, shown);
        }
        else if (*p == 'v')
        {
            char version[32];
            snprintf(version, sizeof(version), "%d.%d",
                     USSR_VERSION_MAJOR, USSR_VERSION_MINOR);
            repl_prompt_add(&out, &length, &capacity, version);
        }
        else if (*p == '$' || *p == '\\')
        {
            single[0] = *p;
            repl_prompt_add(&out, &length, &capacity, single);
        }
        else
        {
            char pair[3] = { '\\', *p, '\0' };
            repl_prompt_add(&out, &length, &capacity, pair);
        }
    }
    free(format);
    return out;
}

/* Line lookup for REPL input: context points at the REPL line number of
 * the entry's first line. */
static int repl_locate(const void *context, int text_line,
                       const char **file, int *line)
{
    *file = "<stdin>";
    *line = *(const int *)context + text_line - 1;
    return 0;
}

#ifndef _WIN32
/* bestline hands over each key's bytes; keybind(_) decides. */
static const char *repl_key_binding(const char *seq, unsigned long length)
{
    return ussr_keybind_match_sequence(seq, (size_t)length);
}
#endif

static int file_readable(const char *path)
{
    FILE *file = fopen(path, "rb");

    if (file == NULL)
        return 0;
    fclose(file);
    return 1;
}

static int run_startup_file(const char *path)
{
    char *startup_argv[2];

    startup_argv[0] = (char *)path;
    startup_argv[1] = NULL;
    return run_file(path, 1, startup_argv);
}

/* Startup files: USSR_MOSCOW names the only one to load ("" loads none);
 * otherwise moscow.su in the home folder (HOME, else USERPROFILE on
 * Windows) runs first, for settings wanted everywhere, then moscow.su in
 * the current folder, for the project at hand, unless it is the same
 * file. Each is optional. */
static int run_startup_files(void)
{
    char *chosen = ussr_env_get("USSR_MOSCOW");
    char *home_moscow = NULL;
    int status = EXIT_SUCCESS;

    if (chosen != NULL)
    {
        if (chosen[0] != '\0')
        {
            if (file_readable(chosen))
                status = run_startup_file(chosen);
            else
            {
                fprintf(stderr, "USSR: USSR_MOSCOW: cannot read %s\n", chosen);
                status = EXIT_FAILURE;
            }
        }
        free(chosen);
        return status;
    }

    home_moscow = ussr_home_file("moscow.su");
    if (home_moscow != NULL && !file_readable(home_moscow))
    {
        free(home_moscow);
        home_moscow = NULL;
    }

    if (home_moscow != NULL)
        status = run_startup_file(home_moscow);
    if (status == EXIT_SUCCESS && file_readable("moscow.su") &&
        (home_moscow == NULL || !ussr_same_file(home_moscow, "moscow.su")))
        status = run_startup_file("moscow.su");
    free(home_moscow);
    return status;
}

/* A loaded history entry goes where a typed line goes: the line
 * editor's Up/Down history and history-based completion. */
static void repl_history_add(const char *line)
{
#ifdef _WIN32
    worstlineHistoryAdd(line);
#else
    bestlineHistoryAdd(line);
#endif
    ussr_autocomplete_record_history(line);
}

static int repl_interactive(void)
{
#ifdef _WIN32
    return _isatty(_fileno(stdin));
#else
    return isatty(STDIN_FILENO);
#endif
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
    int entry_line = 1;
    int interactive;

    printf("USSR Unified Shell Script REPL\n");
    printf("USSR v%d.%d\n",USSR_VERSION_MAJOR, USSR_VERSION_MINOR);
    printf("Enter a command list or press Ctrl-D to exit.\n\n");

    ussr_autocomplete_init();
#ifdef _WIN32	
	worstlineSetCompletionCallback(ussr_autocomplete_callback);	
	worstlineSetKeyCallback(ussr_keybind_match_getch);
#else
	bestlineSetCompletionCallback(ussr_autocomplete_callback);
	bestlineSetKeyCallback(repl_key_binding);
#endif

    /* The history file is for people typing: a piped session neither
     * reads nor writes it. */
    interactive = repl_interactive();
    if (interactive)
        ussr_history_load(repl_history_add);

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
    for (;;)
    {
        const char *processed;
        /* Read PS1/PS2 for every prompt so setenv takes effect at once. */
        char *prompt = repl_prompt(bracket_depth > 0);

#ifdef _WIN32
        line = worstline(prompt != NULL ? prompt : "ussr> ");
#else
        /* bestline prints no prompt when input is redirected; print it
         * here so piped sessions look the same as on Windows. */
        if (!isatty(STDIN_FILENO) || !isatty(STDOUT_FILENO))
        {
            fputs(prompt != NULL ? prompt : "ussr> ", stdout);
            fflush(stdout);
            line = bestline(NULL);
        }
        else
            line = bestline(prompt != NULL ? prompt : "ussr> ");
#endif
        free(prompt);
        if (line == NULL)
            break;

        line_length = strlen(line);

        if (line_length == 0)
        {
#ifdef _WIN32
            worstlineFree(line);
#else
			bestlineFree(line);
#endif
            continue;
        }
        repl_history_add(line);
        if (interactive)
            ussr_history_append(line);

        result = ussr_pp_process_line(&pp, line);
#ifdef _WIN32
        worstlineFree(line);
#else
		bestlineFree(line);
#endif	
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

        if (source_length == 0)
            entry_line = pp.current_line;

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

        ussr_parse_begin("<stdin>", repl_locate, &entry_line);
        repl_expand_aliases = 1;
        result = parse_and_execute(source, 0, NULL, 1);
        ussr_parse_end();

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
	fprintf(stderr,
	        "Usage: %s [-v] [-e code] script.su|program.ru [args...]\n"
	        "       %s -c script.su [program.ru]\n", prog_name, prog_name);
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
        { "compile", no_argument, NULL, 'c' },
        { NULL, 0, NULL, 0 }
    };

    int result;
    int opt;
    const char *eval_source = NULL;
    int compile_only = 0;
	
	srand(time(NULL));
	seed_xrp32(rand());
	
    while ((opt = getopt_long(argc, argv, "ve:c", long_options, NULL)) != -1)
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

        case 'c':
            compile_only = 1;
            break;

        case '?':
			usage(argv[0]);
		break;
		default:
		break;
        }
    }

    if (compile_only)
    {
        /* Compile only: no moscow.su, nothing is executed. */
        if (eval_source != NULL || optind >= argc || argc - optind > 2)
        {
            fprintf(stderr, "Usage: %s -c script.su [program.ru]\n",
                    argv[0]);
            return EXIT_FAILURE;
        }
        ussr_init();
        result = compile_file(argv[optind],
                              argc - optind == 2 ? argv[optind + 1] : NULL);
        ussr_cleanup();
        return result;
    }

    ussr_init();
    ussr_sched_init();

    /* moscow.su is the USSR shell startup file, analogous to a shell rc.
     * A failing (or nonzero) startup file stops ussr with its status. */
    {
        int status = run_startup_files();

        if (status != EXIT_SUCCESS)
        {
            ussr_sched_cleanup();
            ussr_cleanup();
            return status;
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

    ussr_sched_cleanup();
    ussr_cleanup();
    return result;
}
