#ifndef USSR_BYTECODE_H
#define USSR_BYTECODE_H

#include <stddef.h>
#include <stdint.h>

#include "ussr.h"

#define USSR_BC_MAX_CODE       1000000U
#define USSR_BC_MAX_REGS       32U
#define USSR_BC_RETURN_REG     31U

/*
 * USSR bytecode instruction format.
 *
 * Every instruction is exactly 8 bytes:
 *
 *   opcode : 8 bits
 *   a      : 8 bits
 *   b      : 8 bits
 *   c      : 8 bits
 *   imm    : 32 bits
 *
 * The compiler only produces this representation.  It never executes it.
 */
typedef struct
{
    uint8_t opcode;
    uint8_t a;
    uint8_t b;
    uint8_t c;
    uint32_t immediate;
} ussr_bc_instruction_t;

typedef enum
{
    USSR_BC_NOP = 0x00,

    USSR_BC_LOAD_CONST = 0x01,
    USSR_BC_LOAD_VAR   = 0x02,
    USSR_BC_STORE_VAR  = 0x03,
    USSR_BC_LOAD_HASH  = 0x04,
    USSR_BC_STORE_HASH = 0x05,
    USSR_BC_DECODE_UNO = 0x06,
    USSR_BC_GET        = 0x07,

    USSR_BC_ADD  = 0x10,
    USSR_BC_SUB  = 0x11,
    USSR_BC_MUL  = 0x12,
    USSR_BC_DIV  = 0x13,
    USSR_BC_MOD  = 0x14,
    USSR_BC_NEG  = 0x15,

    USSR_BC_EQ   = 0x16,
    USSR_BC_NE   = 0x17,
    USSR_BC_LT   = 0x18,
    USSR_BC_LE   = 0x19,
    USSR_BC_GT   = 0x1a,
    USSR_BC_GE   = 0x1b,

    USSR_BC_LAND = 0x1c,
    USSR_BC_LOR  = 0x1d,
    USSR_BC_XOR  = 0x1e,
    USSR_BC_BAND = 0x1f,
    USSR_BC_BOR  = 0x20,
    USSR_BC_SHL  = 0x21,
    USSR_BC_SHR  = 0x22,

    USSR_BC_JMP       = 0x30,
    USSR_BC_JMP_TRUE  = 0x31,
    USSR_BC_JMP_FALSE = 0x32,

    USSR_BC_SET      = 0x40,
    USSR_BC_PRINT    = 0x41,
    USSR_BC_CONCAT   = 0x42,
    USSR_BC_EXTERNAL = 0x43,
    USSR_BC_OOP      = 0x44,
    USSR_BC_EVAL     = 0x45,
    USSR_BC_RANDOM64 = 0x46,
    USSR_BC_SEED64   = 0x47,
    USSR_BC_SCAN     = 0x48,
    USSR_BC_TIME     = 0x49,

    USSR_BC_CALL   = 0x50,
    USSR_BC_RET    = 0x51,
    USSR_BC_RETURN = 0x52,

    USSR_BC_HALT = 0xf0
} ussr_bc_opcode_t;

typedef struct
{
    char *name;
    char *return_name;
    char **parameters;
    size_t parameter_count;

    /* Bytecode offset of the function entry point. */
    uint32_t entry;
} ussr_bc_function_t;

typedef struct
{
    uint32_t instruction;
    const ussr_command_t *command;
} ussr_bc_oop_site_t;

typedef struct
{
    uint32_t instruction;
    const ussr_command_t *command;
} ussr_bc_scan_site_t;

typedef struct
{
    ussr_bc_instruction_t *code;
    size_t code_count;
    size_t code_capacity;

    ussr_value_t *constants;
    size_t constant_count;
    size_t constant_capacity;

    /*
     * String pool contains variable names, function names, return names,
     * UNO literal text, and eval return-variable names.
     */
    char **strings;
    size_t string_count;
    size_t string_capacity;

    ussr_bc_function_t *functions;
    size_t function_count;
    size_t function_capacity;

    /* Source command metadata for OOP calls whose arguments may include blocks.
     * These pointers are valid only while the parsed source tree is alive. */
    ussr_bc_oop_site_t *oop_sites;
    size_t oop_site_count;
    size_t oop_site_capacity;

    /* Source command metadata for scan calls. */
    ussr_bc_scan_site_t *scan_sites;
    size_t scan_site_count;
    size_t scan_site_capacity;
} ussr_bc_program_t;

/*
 * Compile an already parsed USSR command list into flat bytecode.
 *
 * Compilation walks the parser tree exactly once for each emitted
 * construct.  The resulting program contains no runtime linked-list
 * traversal instructions and no calls to ussr_execute_command().
 */
int ussr_bc_compile(
    const ussr_command_list_t *source,
    ussr_bc_program_t *program
);

/* Release all compiler-owned bytecode/program storage. */
void ussr_bc_program_free(ussr_bc_program_t *program);

/* Human-readable bytecode disassembly for debugging. */
void ussr_bc_dump(const ussr_bc_program_t *program);

/*
 * Optional binary serializer.
 *
 * The file contains the bytecode instruction stream plus constant,
 * string, and function metadata.  It is intended as the basis of the
 * stable .sub / cached-bytecode format; it does not serialize host
 * pointers.
 */
int ussr_bc_write_file(
    const ussr_bc_program_t *program,
    const char *path
);

#endif
