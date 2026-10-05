#include "scheduler.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "eventq.h"
#include "gc.h"

/*
 * Cooperative multithreading scheduler, modeled on the FreeBSD
 * kernel's ULE concepts: a 64-level bitmap run queue, instruction-
 * quantum timeslicing, sleep queues and wait channels backed by the
 * event queue, and zombie processes keeping their exit value.
 *
 * Everything is fresh code running on the interpreter's single host
 * thread; no pthreads, libevent or libev anywhere.
 */

#define USSR_SCHED_LEVELS 64
#define USSR_SCHED_DEFAULT_PRIORITY 32
#define USSR_SCHED_QUANTUM 10000UL
#define USSR_EQ_MAX_FIRED 64

/* ------------------------------------------------------------- */
/* process table                                                   */
/* ------------------------------------------------------------- */

static ussr_proc_t *sched_procs = NULL;
static long sched_next_pid = 1;
static long sched_main_pid = 0;
static ussr_proc_t *sched_current = NULL;

/* Run queue: one FIFO per priority level + presence bitmap (runq). */
static ussr_proc_t *sched_runq_head[USSR_SCHED_LEVELS];
static ussr_proc_t *sched_runq_tail[USSR_SCHED_LEVELS];
static uint64_t sched_runq_bitmap = 0;

/* process(_): name marks. */
static char **sched_marks = NULL;
static size_t sched_mark_count = 0;
static size_t sched_mark_capacity = 0;

static char *sched_strdup(const char *text)
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

/* ------------------------------------------------------------- */
/* program wrappers                                                */
/* ------------------------------------------------------------- */

ussr_sched_program_t *ussr_sched_program_wrap(
    const ussr_bc_program_t *bc,
    ussr_command_list_t *ast)
{
    ussr_sched_program_t *program;

    if (bc == NULL)
        return NULL;

    program = calloc(1, sizeof(*program));
    if (program == NULL)
        return NULL;

    program->bc = *bc; /* shallow: takes over bc's heap arrays */
    program->ast = ast;
    program->refs = 1; /* the creator's own reference */

    return program;
}

void ussr_sched_program_retain(ussr_sched_program_t *program)
{
    if (program != NULL)
        ++program->refs;
}

void ussr_sched_program_release(ussr_sched_program_t *program)
{
    if (program == NULL)
        return;

    if (--program->refs > 0)
        return;

    ussr_bc_program_free(&program->bc);
    if (program->ast != NULL)
        ussr_command_list_free(program->ast);
    free(program);
}

/* ------------------------------------------------------------- */
/* process table helpers                                           */
/* ------------------------------------------------------------- */

ussr_proc_t *ussr_sched_find(long pid)
{
    ussr_proc_t *proc;

    for (proc = sched_procs; proc != NULL; proc = proc->next_all)
        if (proc->pid == pid)
            return proc;

    return NULL;
}

ussr_proc_t *ussr_sched_current(void)
{
    return sched_current;
}

ussr_sched_program_t *ussr_sched_current_program(void)
{
    return sched_current != NULL ? sched_current->program : NULL;
}

size_t ussr_sched_proc_count(void)
{
    size_t count = 0;
    ussr_proc_t *proc;

    for (proc = sched_procs; proc != NULL; proc = proc->next_all)
        ++count;

    return count;
}

static void sched_enqueue(ussr_proc_t *proc)
{
    int level = proc->priority;

    proc->next_run = NULL;

    if (sched_runq_tail[level] != NULL)
        sched_runq_tail[level]->next_run = proc;
    else
        sched_runq_head[level] = proc;

    sched_runq_tail[level] = proc;
    sched_runq_bitmap |= (uint64_t)1 << level;
    proc->state = USSR_PROC_RUNNABLE;
}

static ussr_proc_t *sched_dequeue(void)
{
    int level;
    ussr_proc_t *proc;

    if (sched_runq_bitmap == 0)
        return NULL;

    level = 0;
    while ((sched_runq_bitmap & ((uint64_t)1 << level)) == 0)
        ++level;

    proc = sched_runq_head[level];
    sched_runq_head[level] = proc->next_run;

    if (sched_runq_head[level] == NULL)
    {
        sched_runq_tail[level] = NULL;
        sched_runq_bitmap &= ~((uint64_t)1 << level);
    }

    proc->next_run = NULL;
    return proc;
}

static void sched_proc_free(ussr_proc_t *proc)
{
    ussr_proc_t **link;

    if (proc == NULL)
        return;

    link = &sched_procs;
    while (*link != NULL)
    {
        if (*link == proc)
        {
            *link = proc->next_all;
            break;
        }
        link = &(*link)->next_all;
    }

    vm_cleanup(&proc->vm);
    if (proc->scope != NULL)
        ussr_scope_free(proc->scope);
    ussr_value_free(&proc->result);
    ussr_sched_program_release(proc->program);
    free(proc);
}

static ussr_proc_t *sched_proc_create(
    ussr_sched_program_t *program,
    ussr_scope_t *scope, /* NULL = share the default global scope */
    const char *name)
{
    ussr_proc_t *proc;

    proc = calloc(1, sizeof(*proc));
    if (proc == NULL)
        return NULL;

    proc->pid = sched_next_pid++;
    proc->priority = USSR_SCHED_DEFAULT_PRIORITY;
    proc->state = USSR_PROC_NEW;
    proc->scope = scope;
    proc->result = ussr_null();
    proc->block_kind = USSR_BLOCK_NONE;

    vm_init(&proc->vm);

    if (name != NULL)
    {
        size_t length = strlen(name);
        if (length >= sizeof(proc->name))
            length = sizeof(proc->name) - 1;
        memcpy(proc->name, name, length);
        proc->name[length] = '\0';
    }
    else
    {
        snprintf(proc->name, sizeof(proc->name), "proc%ld", proc->pid);
    }

    ussr_sched_program_retain(program);
    proc->program = program;

    proc->next_all = sched_procs;
    sched_procs = proc;

    return proc;
}

/* Copy a scope (fork semantics for async-spawned processes). */
static ussr_scope_t *sched_scope_clone(const ussr_scope_t *source)
{
    ussr_scope_t *clone;
    size_t i;

    if (source == NULL)
        return NULL;

    clone = ussr_scope_create();
    if (clone == NULL)
        return NULL;

    for (i = 0; i < source->count; ++i)
    {
        if (ussr_scope_set(clone, source->vars[i].name,
                           &source->vars[i].value) != 0)
        {
            ussr_scope_free(clone);
            return NULL;
        }
    }

    return clone;
}

/* ------------------------------------------------------------- */
/* spawning                                                        */
/* ------------------------------------------------------------- */

long ussr_sched_spawn_main(
    ussr_sched_program_t *program,
    int entry_function,
    const char *name)
{
    ussr_proc_t *proc;

    if (program == NULL)
        return -1;

    /* The main process shares the default global scope so that REPL
     * chunks and scripts keep their historical variable behavior. */
    proc = sched_proc_create(program, NULL,
                             name != NULL ? name : "init");
    if (proc == NULL)
        return -1;

    proc->vm.running = 1;
    proc->vm.entry_function = entry_function;

    sched_main_pid = proc->pid;
    sched_enqueue(proc);

    return proc->pid;
}

long ussr_sched_spawn_func(
    ussr_proc_t *parent,
    ussr_sched_program_t *program,
    size_t function_index,
    const ussr_value_t *args,
    size_t arg_count,
    const char *name,
    int fork_scope)
{
    ussr_proc_t *proc;
    ussr_scope_t *scope;
    const ussr_bc_function_t *function;

    if (program == NULL ||
        function_index >= program->bc.function_count)
        return -1;

    function = &program->bc.functions[function_index];

    if (function->parameter_count != arg_count)
    {
        fprintf(stderr,
                "USSR: async %s expects %zu argument(s), got %zu\n",
                function->name, function->parameter_count, arg_count);
        return -1;
    }

    if (name == NULL)
        name = function->name;

    /*
     * fork_scope != 0 (async): the child receives a copy of the
     * parent's current scope — fork semantics. fork_scope == 0
     * (load): the child starts with an empty scope — exec semantics.
     * The !/? hash namespace stays shared either way.
     */
    if (fork_scope && parent != NULL)
    {
        scope = sched_scope_clone(ussr_scope_current());
        if (scope == NULL)
            return -1;
    }
    else
    {
        scope = ussr_scope_create();
        if (scope == NULL)
            return -1;
    }

    proc = sched_proc_create(program, scope, name);
    if (proc == NULL)
    {
        ussr_scope_free(scope);
        return -1;
    }

    /* Bind the arguments inside the child's own scope. */
    ussr_scope_set_current(proc->scope);

    proc->vm.running = 1;
    if (vm_spawn_call(&proc->vm, &program->bc, function_index,
                      args, arg_count) != 0)
    {
        ussr_scope_set_current(
            sched_current != NULL ? sched_current->scope : NULL);
        sched_proc_free(proc);
        return -1;
    }

    ussr_scope_set_current(
        sched_current != NULL ? sched_current->scope : NULL);

    sched_enqueue(proc);
    return proc->pid;
}

/* ------------------------------------------------------------- */
/* blocking / wakeup                                               */
/* ------------------------------------------------------------- */

static void sched_block_current(
    ussr_block_kind_t kind,
    long wait_pid,
    uint64_t token,
    uint8_t resume_register)
{
    ussr_proc_t *proc = sched_current;

    if (proc == NULL)
        return;

    proc->block_kind = kind;
    proc->wait_pid = wait_pid;
    proc->block_token = token;
    proc->state = kind == USSR_BLOCK_TIMER
        ? USSR_PROC_SLEEPING
        : USSR_PROC_WAITING;
    proc->vm.resume_register = resume_register;
    proc->vm.resume_pending = 0;
}

static void sched_wake(ussr_proc_t *proc, const ussr_value_t *value)
{
    ussr_value_free(&proc->vm.resume_value);
    proc->vm.resume_value = value != NULL
        ? ussr_value_copy(value)
        : ussr_null();
    proc->vm.resume_pending = 1;
    proc->block_kind = USSR_BLOCK_NONE;
    sched_enqueue(proc);
}

/* Turn a finished process into a zombie and wake its awaiters. */
static void sched_zombify(ussr_proc_t *proc, int exit_code)
{
    ussr_proc_t *other;

    proc->state = USSR_PROC_ZOMBIE;
    proc->exit_code = exit_code;
    ussr_value_free(&proc->result);
    proc->result = ussr_value_copy(
        &proc->vm.registers[USSR_VM_RETURN_REG]);

    for (other = sched_procs; other != NULL; other = other->next_all)
    {
        if (other->state == USSR_PROC_WAITING &&
            other->block_kind == USSR_BLOCK_AWAIT &&
            other->wait_pid == proc->pid)
        {
            sched_wake(other, &proc->result);
        }
    }
}

/* ------------------------------------------------------------- */
/* GC boundary                                                     */
/* ------------------------------------------------------------- */

static void sched_gc_mark_vm(const ussr_vm_t *vm)
{
    size_t i;
    size_t f;

    for (i = 0; i < USSR_VM_REGISTER_COUNT; ++i)
        ussr_gc_mark_value(&vm->registers[i]);

    for (f = 0; f < vm->frame_count; ++f)
    {
        const ussr_vm_frame_t *frame = &vm->frames[f];

        for (i = 0; i < USSR_VM_REGISTER_COUNT; ++i)
            ussr_gc_mark_value(&frame->saved_registers[i]);

        for (i = 0; i < frame->saved_variable_count; ++i)
            if (frame->saved_variable_existed[i])
                ussr_gc_mark_value(&frame->saved_variables[i]);
    }

    if (vm->resume_pending)
        ussr_gc_mark_value(&vm->resume_value);
}

static void sched_gc_collect(void)
{
    ussr_proc_t *proc;
    size_t collected;

    ussr_gc_begin();

    /* Runtime roots: default scope + shared hash namespace. */
    ussr_runtime_mark_gc_roots();

    /* Process roots. */
    for (proc = sched_procs; proc != NULL; proc = proc->next_all)
    {
        if (proc->scope != NULL)
        {
            size_t i;
            for (i = 0; i < proc->scope->count; ++i)
                ussr_gc_mark_value(&proc->scope->vars[i].value);
        }

        sched_gc_mark_vm(&proc->vm);

        if (proc->state == USSR_PROC_ZOMBIE)
            ussr_gc_mark_value(&proc->result);
    }

    collected = ussr_gc_sweep();
    (void)collected;
}

/* ------------------------------------------------------------- */
/* process(_): marks                                               */
/* ------------------------------------------------------------- */

int ussr_sched_mark_process(const char *name)
{
    size_t i;
    char *copy;

    if (name == NULL || name[0] == '\0')
        return -1;

    for (i = 0; i < sched_mark_count; ++i)
        if (strcmp(sched_marks[i], name) == 0)
            return 0;

    copy = sched_strdup(name);
    if (copy == NULL)
        return -1;

    if (sched_mark_count == sched_mark_capacity)
    {
        size_t capacity = sched_mark_capacity == 0
            ? 8 : sched_mark_capacity * 2;
        char **grown = realloc(sched_marks, capacity * sizeof(*grown));
        if (grown == NULL)
        {
            free(copy);
            return -1;
        }
        sched_marks = grown;
        sched_mark_capacity = capacity;
    }

    sched_marks[sched_mark_count++] = copy;
    return 0;
}

const char *ussr_sched_first_mark(void)
{
    return sched_mark_count > 0 ? sched_marks[0] : NULL;
}

/* ------------------------------------------------------------- */
/* command argument helpers                                        */
/* ------------------------------------------------------------- */

/*
 * Extract a process/function name from a command argument: either a
 * string value ("worker") or a bare name written directly in source
 * (worker), the latter recovered from the command's syntax tree.
 */
static int sched_arg_name(
    const ussr_value_t *values,
    size_t index,
    const ussr_command_t *source_command,
    char *buffer,
    size_t buffer_size)
{
    const char *name = NULL;

    if (values != NULL && values[index].type == USSR_STRING &&
        values[index].data.string != NULL &&
        values[index].data.string[0] != '\0')
    {
        name = values[index].data.string;
    }
    else if (source_command != NULL &&
             index < source_command->argument_count)
    {
        const ussr_argument_t *argument =
            &source_command->arguments[index];

        if (argument->type == USSR_ARGUMENT_EXPRESSION &&
            argument->data.expression != NULL &&
            argument->data.expression->type == USSR_EXPR_VARIABLE)
        {
            name = argument->data.expression->data.variable;
        }
    }

    if (name == NULL || buffer_size == 0)
        return -1;

    if (strlen(name) >= buffer_size)
        return -1;

    strcpy(buffer, name);
    return 0;
}

static long sched_arg_integer(const ussr_value_t *values, size_t index)
{
    if (values[index].type == USSR_INTEGER)
        return values[index].data.integer;
    if (values[index].type == USSR_REAL)
        return (long)values[index].data.real;
    return -1;
}

/* ------------------------------------------------------------- */
/* scheduler command dispatch (VM OOP boundary)                    */
/* ------------------------------------------------------------- */

static int sched_find_function(
    const ussr_sched_program_t *program,
    const char *name)
{
    size_t i;

    if (program == NULL || name == NULL)
        return -1;

    for (i = 0; i < program->bc.function_count; ++i)
        if (strcmp(program->bc.functions[i].name, name) == 0)
            return (int)i;

    return -1;
}

static int sched_do_async(
    ussr_sched_program_t *program,
    const ussr_value_t *values,
    size_t value_count,
    const ussr_command_t *source_command,
    ussr_value_t *out)
{
    char name[64];
    int function_index;
    long pid;

    if (value_count < 1)
    {
        fprintf(stderr, "USSR: async(p): \"function\" args...\n");
        return -1;
    }

    if (sched_arg_name(values, 0, source_command,
                       name, sizeof(name)) != 0)
    {
        fprintf(stderr,
                "USSR: async expects a function name "
                "(string or bare name)\n");
        return -1;
    }

    function_index = sched_find_function(program, name);
    if (function_index < 0)
    {
        fprintf(stderr, "USSR: async: unknown definition '%s'\n", name);
        return -1;
    }

    pid = ussr_sched_spawn_func(
        sched_current,
        program,
        (size_t)function_index,
        values + 1,
        value_count - 1,
        name,
        1 /* fork semantics: copy the parent's scope */
    );

    if (pid < 0)
        return -1;

    *out = ussr_integer(pid);
    return 1;
}

static int sched_do_await(
    const ussr_value_t *values,
    size_t value_count,
    uint8_t resume_register,
    ussr_value_t *out)
{
    long pid;
    ussr_proc_t *target;

    if (value_count != 1)
    {
        fprintf(stderr, "USSR: await(r): pid\n");
        return -1;
    }

    pid = sched_arg_integer(values, 0);
    if (pid <= 0)
    {
        fprintf(stderr, "USSR: await expects a process id\n");
        return -1;
    }

    target = ussr_sched_find(pid);
    if (target == NULL)
    {
        fprintf(stderr, "USSR: await: no process %ld\n", pid);
        return -1;
    }

    if (sched_current != NULL && target->pid == sched_current->pid)
    {
        fprintf(stderr, "USSR: a process cannot await itself\n");
        return -1;
    }

    if (target->state == USSR_PROC_ZOMBIE)
    {
        *out = ussr_value_copy(&target->result);
        return 1;
    }

    sched_block_current(USSR_BLOCK_AWAIT, pid, 0, resume_register);
    return 2;
}

static int sched_do_sleep(
    const ussr_value_t *values,
    size_t value_count,
    uint8_t resume_register)
{
    long ms;
    uint64_t token;

    if (value_count != 1)
    {
        fprintf(stderr, "USSR: sleep(_): milliseconds\n");
        return -1;
    }

    ms = sched_arg_integer(values, 0);
    if (ms < 0)
        ms = 0;

    token = ussr_eq_add_timer(
        sched_current != NULL ? sched_current->pid : 0,
        ussr_eq_now_ms() + (uint64_t)ms
    );

    if (token == 0)
        return -1;

    sched_block_current(USSR_BLOCK_TIMER, 0, token, resume_register);
    return 2;
}

static int sched_do_process(
    const ussr_value_t *values,
    size_t value_count,
    const ussr_command_t *source_command,
    ussr_value_t *out)
{
    char name[64];

    if (value_count < 1 ||
        sched_arg_name(values, 0, source_command,
                       name, sizeof(name)) != 0)
    {
        fprintf(stderr,
                "USSR: process(_): name expects a definition name\n");
        return -1;
    }

    if (ussr_sched_mark_process(name) != 0)
        return -1;

    *out = ussr_integer(0);
    return 1;
}

int ussr_sched_dispatch(
    ussr_sched_program_t *program,
    const char *name,
    const ussr_value_t *values,
    size_t value_count,
    const ussr_command_t *source_command,
    uint8_t ins_a,
    ussr_value_t *out)
{
    if (name == NULL)
        return 0;

    if (strcmp(name, "async") == 0)
        return sched_do_async(program, values, value_count,
                              source_command, out);

    if (strcmp(name, "await") == 0)
        return sched_do_await(values, value_count, ins_a, out);

    if (strcmp(name, "sleep") == 0)
        return sched_do_sleep(values, value_count, ins_a);

    if (strcmp(name, "yield") == 0)
    {
        if (sched_current != NULL)
            sched_current->vm.yield_request = 1;
        *out = ussr_integer(0);
        return 1;
    }

    if (strcmp(name, "gc") == 0)
    {
        ussr_gc_request();
        *out = ussr_integer(0);
        return 1;
    }

    if (strcmp(name, "process") == 0)
        return sched_do_process(values, value_count,
                                source_command, out);

    return 0;
}

/*
 * Block the current process on a freshly spawned child process.
 * Called by the VM's external-command paths (fork-exec integration).
 */
int ussr_sched_block_on_child(ussr_child_t child, uint8_t resume_register)
{
    uint64_t token;

    if (sched_current == NULL)
        return -1;

    token = ussr_eq_add_child(sched_current->pid, child);
    if (token == 0)
        return -1;

    sched_block_current(USSR_BLOCK_CHILD, 0, token, resume_register);
    return 0;
}

/* ------------------------------------------------------------- */
/* the scheduler loop                                              */
/* ------------------------------------------------------------- */

static int sched_any_live(void)
{
    const ussr_proc_t *proc;

    for (proc = sched_procs; proc != NULL; proc = proc->next_all)
        if (proc->state != USSR_PROC_ZOMBIE)
            return 1;

    return 0;
}

static int sched_main_done(void)
{
    const ussr_proc_t *main_proc = ussr_sched_find(sched_main_pid);
    return main_proc == NULL || main_proc->state == USSR_PROC_ZOMBIE;
}

static int sched_main_exit_code(void)
{
    const ussr_proc_t *main_proc = ussr_sched_find(sched_main_pid);
    return main_proc != NULL ? main_proc->exit_code : 0;
}

static void sched_handle_events(void)
{
    ussr_eq_event_t fired[USSR_EQ_MAX_FIRED];
    size_t count;
    size_t i;
    long timeout;

    timeout = ussr_eq_next_timeout_ms();

    count = ussr_eq_dispatch(
        timeout < 0 ? -1 : (int)timeout,
        fired,
        USSR_EQ_MAX_FIRED
    );

    for (i = 0; i < count; ++i)
    {
        ussr_proc_t *owner = ussr_sched_find(fired[i].owner_pid);
        ussr_value_t wake_value;

        if (owner == NULL || owner->state == USSR_PROC_ZOMBIE)
            continue;

        if (owner->block_token != fired[i].token)
            continue;

        if (fired[i].kind == USSR_EQ_CHILD)
            wake_value = ussr_integer(fired[i].exit_code);
        else
            wake_value = ussr_integer(0);

        sched_wake(owner, &wake_value);
        ussr_value_free(&wake_value);
    }
}

int ussr_sched_run(int background_ok)
{
    int failed = 0;

    for (;;)
    {
        ussr_proc_t *proc;
        int status;

        /* GC runs only here, at slice boundaries, never
         * mid-instruction. */
        if (ussr_gc_pending())
            sched_gc_collect();

        proc = sched_dequeue();

        if (proc == NULL)
        {
            /* Nobody runnable. */
            if (!sched_any_live())
                break;

            if (background_ok && sched_main_done())
                break;

            if (!ussr_eq_pending())
            {
                fprintf(stderr,
                        "USSR scheduler: deadlock — processes blocked "
                        "with no pending events\n");
                failed = 1;
                break;
            }

            sched_handle_events();
            continue;
        }

        proc->state = USSR_PROC_RUNNING;
        sched_current = proc;
        ussr_scope_set_current(proc->scope);

        status = vm_execute(
            &proc->vm,
            &proc->program->bc,
            USSR_SCHED_QUANTUM
        );

        sched_current = NULL;
        ussr_scope_set_current(NULL);

        if (status == USSR_VM_STATUS_ERROR)
        {
            fprintf(stderr,
                    "USSR scheduler: process %ld (%s) failed\n",
                    proc->pid, proc->name);
            sched_zombify(proc, -1);
            if (proc->pid == sched_main_pid)
                failed = 1;
            continue;
        }

        if (status == USSR_VM_STATUS_DONE)
        {
            sched_zombify(proc, proc->vm.exit_code);
            continue;
        }

        /* SUSPENDED: either blocked (dispatch set the state) or
         * quantum/yield (still marked RUNNING). */
        if (proc->state == USSR_PROC_RUNNING)
            sched_enqueue(proc);
    }

    if (failed)
        return -1;

    return sched_main_exit_code();
}

/* ------------------------------------------------------------- */
/* init / cleanup                                                  */
/* ------------------------------------------------------------- */

void ussr_sched_init(void)
{
    sched_procs = NULL;
    sched_next_pid = 1;
    sched_main_pid = 0;
    sched_current = NULL;
    memset(sched_runq_head, 0, sizeof(sched_runq_head));
    memset(sched_runq_tail, 0, sizeof(sched_runq_tail));
    sched_runq_bitmap = 0;
    sched_marks = NULL;
    sched_mark_count = 0;
    sched_mark_capacity = 0;

    ussr_eq_init();
    ussr_gc_init();
}

void ussr_sched_cleanup(void)
{
    size_t i;

    while (sched_procs != NULL)
        sched_proc_free(sched_procs);

    for (i = 0; i < sched_mark_count; ++i)
        free(sched_marks[i]);
    free(sched_marks);
    sched_marks = NULL;
    sched_mark_count = 0;
    sched_mark_capacity = 0;

    sched_current = NULL;
    sched_main_pid = 0;
    sched_next_pid = 1;
    sched_runq_bitmap = 0;
    memset(sched_runq_head, 0, sizeof(sched_runq_head));
    memset(sched_runq_tail, 0, sizeof(sched_runq_tail));

    ussr_eq_cleanup();
    ussr_gc_cleanup();
}
