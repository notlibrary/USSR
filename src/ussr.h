#ifndef USSR_H
#define USSR_H

#include <stddef.h>
#include <stdint.h>
#include <inttypes.h>
#include "process.h"
typedef struct ussr_command_list_t ussr_command_list_t;

/*
 * USSR integers: 64-bit signed on every platform (C `long` is 32-bit on
 * Windows). Print them with USSR_INT_FMT, parse with strtoll.
 */
typedef int64_t ussr_int_t;
#define USSR_INT_MIN INT64_MIN
#define USSR_INT_MAX INT64_MAX
#define USSR_INT_FMT "%" PRId64

/* Integer + - * and negation wrap around (two's complement) on overflow,
 * the same on every platform, instead of being undefined behavior. */
static inline ussr_int_t ussr_int_add(ussr_int_t a, ussr_int_t b)
{
    return (ussr_int_t)((uint64_t)a + (uint64_t)b);
}

static inline ussr_int_t ussr_int_sub(ussr_int_t a, ussr_int_t b)
{
    return (ussr_int_t)((uint64_t)a - (uint64_t)b);
}

static inline ussr_int_t ussr_int_mul(ussr_int_t a, ussr_int_t b)
{
    return (ussr_int_t)((uint64_t)a * (uint64_t)b);
}

/* b must not be 0; MIN % -1 (which traps in C) is 0. */
static inline ussr_int_t ussr_int_mod(ussr_int_t a, ussr_int_t b)
{
    return b == -1 ? 0 : a % b;
}

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
    USSR_BOOLEAN,
    USSR_VECTOR,
    USSR_STRUCT
} ussr_value_type_t;

/*
 * USSR_VECTOR and USSR_STRUCT are reference types: the value union
 * holds a pointer to a heap object with its own refcount (see
 * uno.h). Copying an ussr_value_t of either kind (ussr_value_copy)
 * must retain the pointee; freeing it (ussr_value_free) must release
 * it. Everything else keeps the existing value-copy semantics.
 */
typedef struct ussr_vector_t ussr_vector_t;
typedef struct ussr_struct_instance_t ussr_struct_instance_t;

typedef struct
{
    ussr_value_type_t type;

    union
    {
        ussr_int_t integer;
        double real;
        char *string;
        int boolean;
        ussr_vector_t *vector;
        ussr_struct_instance_t *instance;
    } data;
} ussr_value_t;

typedef struct ussr_variable_t
{
    char *name;
    ussr_value_t value;
} ussr_variable_t;

/*
 * Variable scope. Each scheduled process (scheduler.c) owns its own
 * scope; the interpreter also keeps a default global scope for the
 * top level / non-scheduled execution. The !/? hash namespace is NOT
 * part of the scope: it is deliberately global and shared between
 * processes as an IPC channel.
 */
typedef ussr_variable_t ussr_scope_variable_t;

/* The local variables of one function call: slot i holds names[i] once
 * assigned. `names` lists the function's local names (borrowed from the
 * compiled program); a name not in it is looked up in the globals. The
 * slot arrays are kept and reused by later calls at the same depth. */
typedef struct ussr_locals_t
{
    ussr_value_t *values;
    unsigned char *assigned;
    size_t capacity;

    char *const *names;
    size_t name_count;
} ussr_locals_t;

typedef struct ussr_scope_t
{
    ussr_scope_variable_t *vars;
    size_t count;
    size_t capacity;

    /* Hash index over vars: index_size slots (a power of two, or 0 when
     * there is no index yet), each a vars position + 1 or 0 for empty.
     * Rebuilt on demand; removing a variable drops it. */
    size_t *index;
    size_t index_size;

    /* Call stack of the process using this scope: one entry per active
     * function call, the running function's on top. */
    ussr_locals_t *locals;
    size_t local_depth;
    size_t local_capacity;
} ussr_scope_t;

/* Scope lifecycle. The current scope pointer is cooperative-thread
 * local in effect: the scheduler swaps it on every context switch. */
ussr_scope_t *ussr_scope_create(void);
void ussr_scope_free(ussr_scope_t *scope);

/* Passing NULL restores the default global scope. */
void ussr_scope_set_current(ussr_scope_t *scope);
ussr_scope_t *ussr_scope_current(void);

/* Set a variable inside a specific scope (no current-scope switch).
 * Used by the scheduler when it clones parent scopes on spawn. */
int ussr_scope_set(
    ussr_scope_t *scope,
    const char *name,
    const ussr_value_t *value
);

/* Mark GC roots owned by the runtime itself: the default scope and
 * the global hash namespace. Process scopes are marked by the
 * scheduler. */
void ussr_runtime_mark_gc_roots(void);

/* Mark every value of a scope: globals and all call frames' locals. */
void ussr_scope_mark_gc(const ussr_scope_t *scope);

/*
 * Function-call locals in the current scope. ussr_locals_push starts a
 * call whose local names are `names` (they must outlive the call);
 * ussr_get_variable/ussr_set_variable then resolve those names in the
 * call's own table. ussr_locals_pop ends the innermost call.
 * ussr_locals_reset drops every call of a scope (NULL = the default
 * scope), e.g. after a program stopped inside a function.
 */
int ussr_locals_push(char *const *names, size_t name_count);

/* Where a LOAD_VAR/STORE_VAR operand b says a name lives: decided by the
 * compiler inside functions, by name (local first) everywhere else. For
 * USSR_VAR_LOCAL, slot is the name's index in the function's locals. */
typedef enum
{
    USSR_VAR_BY_NAME = 0,
    USSR_VAR_LOCAL = 1,
    USSR_VAR_GLOBAL = 2
} ussr_var_place_t;

const ussr_value_t *ussr_get_variable_at(
    const char *name,
    ussr_var_place_t place,
    size_t slot
);
int ussr_set_variable_at(
    const char *name,
    const ussr_value_t *value,
    ussr_var_place_t place,
    size_t slot
);
void ussr_locals_pop(void);
/* How many calls' locals are active (for unwinding after an error). */
size_t ussr_locals_depth(void);
void ussr_locals_reset(ussr_scope_t *scope);

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
    USSR_OP_NE,
    USSR_OP_LOGICAL_AND,
    USSR_OP_LOGICAL_OR,
    USSR_OP_SHIFT_LEFT,
    USSR_OP_SHIFT_RIGHT,
    USSR_OP_BITWISE_XOR,
    USSR_OP_BITWISE_AND,
    USSR_OP_BITWISE_OR
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
    USSR_ARGUMENT_ADVANCED_LIST,
    USSR_ARGUMENT_LOOKUP,
    USSR_ARGUMENT_UNO_LITERAL
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

        /*
         * Raw text between a `'...'` literal's quotes, e.g. for
         * 'Point{x=1,y=2}' this is "Point{x=1,y=2}". Owned; free it
         * the same way you already free .expression/.command_list for
         * the other deferred argument kinds. Resolved to a real value
         * with ussr_uno_decode() (uno.h) wherever arguments are
         * evaluated for a command call — struct types referenced here
         * may not be registered until the interpreter actually runs
         * the struct(...) command that defines them, so this can't be
         * resolved any earlier than the other deferred kinds are.
         */
        char *uno_text;
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
    /* Directory/device prefix of an explicit command path, or NULL.
     * Examples: "./", "/usr/bin/", "C:\\Users\\serenity\\". */
    char *path;
    char *name;
    char *return_name;

    ussr_argument_t *arguments;
    size_t argument_count;

    /* Where the command starts, for diagnostics: the source file
     * (interned, never freed) and its 1-based line; NULL and 0 when
     * unknown, e.g. for commands loaded from a .ru file. */
    const char *source_file;
    int source_line;

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
ussr_value_t ussr_integer(ussr_int_t value);
ussr_value_t ussr_real(double value);
ussr_value_t ussr_boolean(int value);
ussr_value_t ussr_string(const char *value);

/*
 * Wrap an existing (already-owned) vector/instance pointer in a
 * value, taking one reference. Do not call on a pointer you still
 * need a reference to elsewhere without retaining it yourself first.
 */
ussr_value_t ussr_vector_value(ussr_vector_t *vector);
ussr_value_t ussr_struct_value(ussr_struct_instance_t *instance);

void ussr_value_free(ussr_value_t *value);
ussr_value_t ussr_value_copy(const ussr_value_t *value);

int ussr_set_variable(
    const char *name,
    const ussr_value_t *value
);

const ussr_value_t *ussr_get_variable(const char *name);

/* VM runtime access to the separate !/? hash namespace. */
int ussr_hash_set_value(const char *name, const ussr_value_t *value);
const ussr_value_t *ussr_hash_get_value(const char *name);

/* Host-only external process service used by the EXTERNAL VM opcode. */
int ussr_external_execute_values(
    const char *command,
    const ussr_value_t *values,
    size_t value_count,
    ussr_value_t *result
);

/*
 * Non-blocking variant: resolves the command and launches it with
 * inherited std handles, returning immediately with the child handle
 * registered nowhere yet — the caller (VM/scheduler) parks the
 * current process on it via ussr_sched_block_on_child().
 */
int ussr_external_spawn_values(
    const char *command,
    const ussr_value_t *values,
    size_t value_count,
    ussr_child_t *child
);

void ussr_print_value(const ussr_value_t *value);

int ussr_execute_command(
    const char *command_path,
    const char *command,
    const char *return_name,
    ussr_argument_t *arguments,
    size_t argument_count
);

int ussr_execute_program(ussr_command_list_t *program);

ussr_command_list_t *ussr_command_list_create(void);
ussr_command_t *ussr_command_create(
    char *path,
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

int ussr_execute_eval(const char *return_name, ussr_argument_t *arguments, size_t argument_count);
int ussr_execute_do_loop(const char *return_name,ussr_argument_t *arguments,size_t argument_count);
int ussr_external_execute_values_io(const char *command,const ussr_value_t *values,size_t value_count,const char *input,char **output,ussr_value_t *result);
ussr_vector_t *ussr_vector_create(const char *element_type);
void ussr_vector_retain(ussr_vector_t *vector);
void ussr_vector_release(ussr_vector_t *vector);
int ussr_vector_push(ussr_vector_t *vector, ussr_value_t item);
int ussr_vector_get(const ussr_vector_t *vector, size_t index, ussr_value_t *out);
int ussr_vector_set(ussr_vector_t *vector, size_t index, ussr_value_t item);
size_t ussr_vector_length(const ussr_vector_t *vector);

/*
 * Source locations while parsing. Call ussr_parse_begin() before every
 * yyparse(): it restarts the lexer's line count and tells the parser how
 * to turn a line of the parsed text into a file and line (lookup may be
 * NULL, then every line is reported in `name`). ussr_parse_end() forgets
 * the lookup context. The parser stores the result in each command's
 * source_file/source_line.
 */
typedef int (*ussr_line_lookup_t)(
    const void *context,
    int text_line,
    const char **file,
    int *line
);

void ussr_parse_begin(
    const char *name,
    ussr_line_lookup_t lookup,
    const void *context
);
void ussr_parse_end(void);
void ussr_parse_locate(int text_line, const char **file, int *line);

/* Returns a copy of name that lives until the process exits. */
const char *ussr_source_name_intern(const char *name);


#endif
