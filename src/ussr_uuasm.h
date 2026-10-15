#ifndef USSR_UUASM_H
#define USSR_UUASM_H

#include "ussr_bytecode.h"

/*
 * UUASM uses the VM instruction layout and metadata containers so compiler
 * metadata can be transferred without lossy serialization. The separate API
 * boundary enforces opcode validation before the runtime consumes the stream.
 */
typedef ussr_bc_instruction_t ussr_uuasm_instruction_t;
typedef ussr_bc_program_t ussr_uuasm_program_t;

/* Compile parsed USSR into UUASM instructions and metadata. */
int ussr_uuasm_compile(
    const ussr_command_list_t *source,
    ussr_uuasm_program_t *program
);


/* Parse textual UUASM source into instructions, pools, and function metadata. */
int ussr_uuasm_assemble(
    const char *source,
    ussr_uuasm_program_t *program
);

/* Map exactly one UUASM opcode to one VM opcode; returns -1 if unsupported. */
int ussr_uuasm_to_vm_opcode(uint8_t uuasm_opcode, uint8_t *vm_opcode);

/* Transfer a compiled UUASM program to the bytecode VM, validating every op. */
int ussr_uuasm_compile_to_bytecode(
    const ussr_command_list_t *source,
    ussr_bc_program_t *program
);

#endif
