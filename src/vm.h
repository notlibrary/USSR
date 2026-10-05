#ifndef USSR_VM_H
#define USSR_VM_H

#include <stddef.h>
#include <stdint.h>

#include "ussr.h"
#include "ussr_bytecode.h"

/*
 * Resumable virtual machine state.
 *
 * The bytecode VM used to run to completion inside a single C call.
 * The scheduler (scheduler.c, FreeBSD-style cooperative
 * multithreading) needs many processes to share one interpreter
 * thread, so ALL execution state now lives in ussr_vm_t: registers,
 * instruction pointer, call frames, and suspension bookkeeping. A
 * context switch is therefore just a pointer swap — no fibers, no
 * ucontext, no pthreads.
 *
 * vm_execute() runs at most `quantum` instructions and returns:
 *
 *   USSR_VM_STATUS_DONE      - the program halted; vm->exit_code set
 *   USSR_VM_STATUS_ERROR     - execution failed
 *   USSR_VM_STATUS_SUSPENDED - quantum exhausted, yield requested, or
 *                              the process blocked (await/sleep/child);
 *                              the scheduler decides what happens next
 */

#define USSR_VM_MAX_STEPS 2000000000UL
#define USSR_VM_MAX_CALLS 1024U
#define USSR_VM_RETURN_REG USSR_BC_RETURN_REG
#define USSR_VM_REGISTER_COUNT USSR_BC_MAX_REGS

typedef enum
{
    USSR_VM_STATUS_DONE = 0,
    USSR_VM_STATUS_ERROR = -1,
    USSR_VM_STATUS_SUSPENDED = 1
} ussr_vm_status_t;

typedef struct ussr_vm_frame_t
{
    uint32_t return_ip;
    uint8_t return_register;
    uint32_t function_index;
    ussr_value_t saved_registers[USSR_VM_REGISTER_COUNT];
    ussr_value_t *saved_variables;
    unsigned char *saved_variable_existed;
    size_t saved_variable_count;
} ussr_vm_frame_t;

typedef struct ussr_vm_t
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

    /* Call frame stack (moved into the VM so it survives suspension). */
    ussr_vm_frame_t *frames;
    size_t frame_count;
    size_t frame_capacity;

    /* Voluntary reschedule requested by the yield(_) command. */
    int yield_request;

    /*
     * Block resume protocol. When a process blocks on await/sleep/a
     * child process, the VM suspends mid-instruction and the
     * scheduler remembers where the blocked command's result belongs.
     * On wakeup the scheduler stores the outcome in resume_value and
     * sets resume_pending; vm_execute applies it to resume_register
     * before fetching the next instruction.
     */
    int resume_pending;
    uint8_t resume_register;
    ussr_value_t resume_value;
} ussr_vm_t;

void vm_init(ussr_vm_t *vm);
void vm_cleanup(ussr_vm_t *vm);

/* Run up to `quantum` instructions (0 = unlimited). */
int vm_execute(
    ussr_vm_t *vm,
    const ussr_bc_program_t *program,
    unsigned long quantum
);

/*
 * Prepare a pristine VM to run program->functions[function_index]
 * with args bound to registers 0..arg_count-1, returning to the
 * program's final HALT. Used by the scheduler to spawn processes.
 */
int vm_spawn_call(
    ussr_vm_t *vm,
    const ussr_bc_program_t *program,
    size_t function_index,
    const ussr_value_t *args,
    size_t arg_count
);

#endif
