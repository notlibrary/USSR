#ifndef USSR_OOP_BUILTINS_H
#define USSR_OOP_BUILTINS_H

#include "ussr.h"

/*
 * Simple structs + a vector container. No methods/dispatch — you
 * design whatever constructor style you want on top of `new`
 * (positional) yourself, e.g. with $define macros in the
 * preprocessor, or just call `new` directly.
 *
 * Commands added: struct, new, getf, setf, vec, push, at, len,
 * encode, decode. Check these against your existing builtin names
 * before wiring this in (only "new" and "at" feel like likely
 * collisions) — each is only referenced by its string literal in one
 * place in ussr_oop_builtins.c, so renaming is a one-line change.
 *
 * Integration point: ussr_execute_command's big if/else chain sets a
 * local `ussr_value_t result` per branch and falls through to shared
 * code afterward that does ussr_set_variable(return_name, &result)
 * plus the `!`-assignment handling. Add a branch there — see
 * INTEGRATION.md for the exact anchor and snippet — that calls this:
 *
 *   int status = ussr_oop_dispatch(command, return_name, arguments,
 *                                   argument_count, &result);
 *   if (status < 0) return -1;      // handled, error already reported
 *   if (status > 0) { }             // handled, `result` is set — fall through
 *   // status == 0: not ours, continue the existing chain
 */
int ussr_oop_dispatch(
    const char *command,
    const char *return_name,
    ussr_argument_t *arguments,
    size_t argument_count,
    ussr_value_t *out_result
);

#endif

