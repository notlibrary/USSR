#ifndef USSR_SCHEDULER_H
#define USSR_SCHEDULER_H

#include <stddef.h>
#include <stdint.h>

#include "ussr.h"
#include "ussr_bytecode.h"
#include "vm.h"
#include "process.h"

/*
 * USSR process scheduler — a cooperative, FreeBSD-kernel-flavored
 * multithreading core for the interpreter.
 *
 * Fresh implementation, no pthreads / libevent / libev: since the
 * interpreter is a bytecode VM, every process is just an ussr_vm_t
 * plus a variable scope, and a context switch is a pointer swap. The
 * design borrows from the FreeBSD scheduler:
 *
 *   - a run queue with 64 priority levels and a bitmap of non-empty
 *     queues (runq), round-robin within a level;
 *   - time-slice preemption by instruction quantum (cooperative: the
 *     VM checks its quantum at instruction boundaries);
 *   - sleep queues: processes blocked on timer events (sleep) park on
 *     the event queue;
 *   - wait channels: processes blocked in await or on a fork/exec'd
 *     child park until the target process zombies or the event queue
 *     reaps the child;
 *   - zombies keep their result value until reaped by await or
 *     scheduler teardown.
 *
 * Interaction with the existing fork-exec model: external commands
 * launched through the VM spawn via the process layer WITHOUT waiting
 * (ussr_process_spawn); the child handle goes into the event queue
 * and the calling process blocks on it, so other USSR processes keep
 * running while the child executes. Only the capture/chained variant
 * (| style chaining into `file`) still waits synchronously, because
 * it must pump the child's pipes.
 *
 * The !/? hash namespace is shared between processes (IPC channel);
 * plain variables are per-process.
 *
 * Entry-point hierarchy (mirrors the init design):
 *   1. REPL chunks run as the pid-1 "init" process of the scheduler;
 *   2. a script's global init(...) runs as the main process;
 *   3. a definition marked with process(_): name inside a file loaded
 *      with load(_): "file.su" ["entry"] becomes a local init running
 *      as a freshly scheduled process.
 */

typedef enum
{
    USSR_PROC_NEW = 0,
    USSR_PROC_RUNNABLE,
    USSR_PROC_RUNNING,
    USSR_PROC_SLEEPING,  /* blocked on a timer event */
    USSR_PROC_WAITING,   /* blocked on await or child exit */
    USSR_PROC_ZOMBIE     /* finished; result kept for await */
} ussr_proc_state_t;

/* What a blocked process is waiting on. */
typedef enum
{
    USSR_BLOCK_NONE = 0,
    USSR_BLOCK_AWAIT,    /* wait_pid holds the target process */
    USSR_BLOCK_TIMER,    /* event queue token in block_token */
    USSR_BLOCK_CHILD     /* event queue token in block_token */
} ussr_block_kind_t;

/*
 * A compiled program shared by one or more processes. The AST must
 * outlive the bytecode because OOP/scan sites point into it, so the
 * wrapper owns both and is reference-counted by its processes.
 */
typedef struct ussr_sched_program_t
{
    ussr_bc_program_t bc;
    ussr_command_list_t *ast;
    size_t refs;
} ussr_sched_program_t;

typedef struct ussr_proc_t
{
    long pid;
    char name[32];
    int priority;                 /* 0 highest .. 63 lowest */
    ussr_proc_state_t state;

    ussr_vm_t vm;                 /* full resumable VM context */
    ussr_scope_t *scope;          /* per-process variable scope */
    ussr_sched_program_t *program;

    /* Suspension bookkeeping. */
    ussr_block_kind_t block_kind;
    long wait_pid;                /* AWAIT: target process */
    uint64_t block_token;         /* TIMER/CHILD: event queue token */

    ussr_value_t result;          /* ZOMBIE: final return-register value */
    int exit_code;

    struct ussr_proc_t *next_all; /* process table linkage */
    struct ussr_proc_t *next_run; /* run queue linkage */
} ussr_proc_t;

void ussr_sched_init(void);
void ussr_sched_cleanup(void);

/* Program wrapper lifecycle. */
ussr_sched_program_t *ussr_sched_program_wrap(
    const ussr_bc_program_t *bc,   /* copied (shallow struct copy) */
    ussr_command_list_t *ast       /* adopted */
);
void ussr_sched_program_retain(ussr_sched_program_t *program);
void ussr_sched_program_release(ussr_sched_program_t *program);

/* Process creation. Both enqueue the new process on the run queue.
 * spawn_main creates pid 1 around a top-level program; spawn_func
 * creates a process running one function of an existing program.
 * fork_scope != 0 gives the child a copy of the parent's current
 * scope (async: fork semantics); fork_scope == 0 gives it a fresh
 * empty scope (load: exec semantics). Returns the new pid, or -1. */
long ussr_sched_spawn_main(
    ussr_sched_program_t *program,
    int entry_function,
    const char *name
);
long ussr_sched_spawn_func(
    ussr_proc_t *parent,
    ussr_sched_program_t *program,
    size_t function_index,
    const ussr_value_t *args,
    size_t arg_count,
    const char *name,
    int fork_scope
);

ussr_proc_t *ussr_sched_current(void);
ussr_proc_t *ussr_sched_find(long pid);

/* Program wrapper of the currently running process (NULL outside
 * scheduler execution). */
ussr_sched_program_t *ussr_sched_current_program(void);

/*
 * Block the current process on a freshly spawned child (fork-exec
 * integration): registers the child with the event queue and parks
 * the process until the child exits. Called by the VM's external
 * command paths. Returns 0 when the process was parked.
 */
int ussr_sched_block_on_child(ussr_child_t child, uint8_t resume_register);

/*
 * Scheduler-command dispatch at the VM OOP boundary. `values` are the
 * command's argument values (register materialized); `source_command`
 * gives access to unevaluated argument syntax (bare names). Return:
 *   1  handled, *out holds the command's value;
 *   2  handled, current process blocked (VM must suspend; resume
 *      register is ins_a);
 *   0  not a scheduler command;
 *  -1  error.
 */
int ussr_sched_dispatch(
    ussr_sched_program_t *program,
    const char *name,
    const ussr_value_t *values,
    size_t value_count,
    const ussr_command_t *source_command,
    uint8_t ins_a,
    ussr_value_t *out
);

/* Register a definition name as a process entry (process(_): name). */
int ussr_sched_mark_process(const char *name);
/* First registered mark, or NULL (used by load with no explicit entry). */
const char *ussr_sched_first_mark(void);

/*
 * Run the scheduler.
 *   background_ok == 0: run until every process is reaped or a
 *                       deadlock is detected (script / -e mode).
 *   background_ok == 1: return as soon as the main process zombies,
 *                       leaving background processes parked for the
 *                       next REPL chunk.
 * Returns the main process's exit code, or -1 on failure.
 */
int ussr_sched_run(int background_ok);

/* Number of live (not reaped) processes — diagnostics/tests. */
size_t ussr_sched_proc_count(void);

#endif
