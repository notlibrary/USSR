#ifndef USSR_RU_H
#define USSR_RU_H

#include "ussr_bytecode.h"

/*
 * .ru raw bytecode files.
 *
 * A .ru file is a compiled USSR program: the VM instruction stream and
 * the pools it refers to, written byte-for-byte in a fixed little-endian
 * layout (see docs/RU_BYTECODE_FORMAT.md). It never contains host
 * pointers. Source-site metadata that the VM reads from the syntax tree
 * at run time (OOP and scan call sites) is stored in a reduced form and
 * rebuilt as small synthetic command records when the file is loaded.
 */

#define USSR_RU_MAGIC          "\x89USSR\r\n\x1a"
#define USSR_RU_MAGIC_SIZE     8U
#define USSR_RU_FORMAT_VERSION 1U

/*
 * VM ABI revision. Bump it whenever an opcode, operand meaning or
 * calling convention changes; files with another ABI are rejected
 * instead of being run with the wrong semantics.
 */
#define USSR_RU_VM_ABI         1U

/*
 * Write a compiled program to path. Returns 0 on success. On failure a
 * diagnostic is printed to stderr, nothing is left at path, and -1 is
 * returned.
 */
int ussr_ru_write_file(const ussr_bc_program_t *program, const char *path);

/*
 * Read and validate a .ru file. On success fills program and sets
 * *site_commands to a command list that owns the rebuilt site records
 * (the program's site pointers point into it; free it after the
 * program). On failure prints a diagnostic and returns -1.
 */
int ussr_ru_read_file(
    const char *path,
    ussr_bc_program_t *program,
    ussr_command_list_t **site_commands
);

#endif
