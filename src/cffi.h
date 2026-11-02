#ifndef USSR_CFFI_H
#define USSR_CFFI_H

/*
 * C libraries as USSR commands: a shared library is loaded, its
 * functions found by name and called with integers, strings and byte
 * buffers, and memory is read and written through addresses.
 *
 *   c_load(lib): path           a library (.dll, .so, .dylib), or null
 *   c_sym(f): lib name          a function's or a variable's address, or null
 *   c_call(r): f [args ...]     calls f with up to 12 arguments and gives
 *                               back the integer it returns (rax)
 *   c_alloc(p): n               n zeroed bytes; their address
 *   c_free(_): p                gives them back
 *   c_read(v): p n              n bytes at p, as a byte vector
 *   c_write(n): p data          a string's or a byte vector's bytes to p
 *   c_string(s): p              the NUL-terminated string at p
 *
 * An argument of c_call is an integer (also an address), true or false
 * (1, 0), null (0), a string (the address of a NUL-terminated copy, for
 * the call) or a vector of bytes (the address of a copy, whose bytes
 * come back into the vector when the call returns: a buffer the
 * function fills). Integer and pointer arguments only: on x86-64 a
 * function taking fewer than 12 never sees the rest.
 *
 * Moscow's runtime gives the same commands for ELF libraries it loads
 * itself (src/moscow/arch/amd64/cffi.inc), so a program that calls C
 * runs on Windows, Linux and Moscow.
 */

#include "ussr.h"

/* 1 when command is one of these and it ran, -1 when it failed, 0 when
 * it is not one of these */
int ussr_cffi_dispatch(const char *command, ussr_argument_t *arguments,
                       size_t argument_count, ussr_value_t *out_result);

#endif
