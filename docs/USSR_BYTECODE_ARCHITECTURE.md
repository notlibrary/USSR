# USSR Bytecode Architecture

## 1. Execution pipeline

```text
.su source
   |
   v
preprocessor (pp.c)
   |
   v
lexer/parser (flex + bison)
   |
   v
USSR AST / command linked lists
   |
   |  compile only
   v
ussr_bytecode.c
   |
   v
flat USSR bytecode
   |
   |  execute only
   v
main.c VM runtime
   |
   v
ussr.c / uno.c host services
```

The linked-list AST is a compiler input. It is not the VM instruction stream.
The VM never calls `ussr_execute_command()`, `ussr_execute_program()`, or
`ussr_execute_user_definition()`.

## 2. Responsibilities

### `ussr.c`

Owns USSR language/runtime state that is not CPU execution:

- values and value ownership
- local variables
- the separate `!` / `?` uthash namespace
- user-definition metadata
- argument/value helpers
- printing
- host external-process service

The VM uses small public host-service functions from this layer for hash and
external-process operations.

### `ussr_bytecode.c`

This is the bytecode compiler.

Its central function is:

```c
static int bc_compile_command(...);
```

It translates command nodes into explicit opcodes. It is the only component
that walks `ussr_command_list_t` for execution purposes.

It also compiles:

- constants
- variables
- `?` hash reads
- `!` hash writes
- arithmetic
- comparisons
- logical and bitwise operators
- `if`
- `while`
- `break`
- `continue`
- `return`
- user-defined function calls
- `set`
- `print`
- `concat`
- `eval`
- UNO decoding
- OOP/UNO command calls
- external command calls

### `main.c`

Contains the VM runtime.

It fetches an 8-byte instruction, decodes it, and performs the operation.
It does not traverse command lists and does not dispatch through
`ussr_execute_command()`.

### `uno.c`

Owns the actual OOP data model:

- struct type registry
- inheritance
- struct instances
- field access/type checks
- vectors
- methods
- UNO encode/decode

The VM reaches these facilities through explicit `DECODE_UNO` and `OOP`
opcodes and the OOP host primitive boundary.

## 3. Instruction format

Every instruction is 8 bytes:

```text
+--------+----+----+----+----------------+
| opcode | a  | b  | c  | immediate      |
| 8 bit  | 8  | 8  | 8  | 32 bits        |
+--------+----+----+----+----------------+
```

## 4. Opcode ABI

| Range | Opcode | Purpose |
|---|---|---|
| 00 | NOP | no operation |
| 01 | LOAD_CONST | constant pool -> register |
| 02 | LOAD_VAR | local variable -> register |
| 03 | STORE_VAR | register -> local variable |
| 04 | LOAD_HASH | `?` lookup |
| 05 | STORE_HASH | `!` store |
| 06 | DECODE_UNO | UNO text -> value |
| 10-22 | arithmetic/logic | value operations |
| 30-32 | jumps | control flow |
| 40 | SET | direct assignment primitive |
| 41 | PRINT | print register |
| 42 | CONCAT | concatenate strings |
| 43 | EXTERNAL | host process primitive |
| 44 | OOP | UNO/OOP primitive boundary |
| 45 | EVAL | parse + compile + execute generated source |
| 50 | CALL | VM function call |
| 51 | RET | VM function return |
| 52 | RETURN | compatibility/control opcode |
| f0 | HALT | stop VM |

## 5. Registers

The VM has 32 value registers, `R0..R31`.

`R31` is reserved as the return/result register.

Function arguments are placed in consecutive registers starting at `R0`.

## 6. Function calls

A user definition is compiled into the function table and its body becomes
bytecode at a known entry address.

```text
CALL R0, argument_count, R31, function_index
```

No command-name lookup is required during the call.

`RET` restores the caller register file and returns the callee result in the
specified result register.

## 7. Control flow

`if` and `while` are compiled into ordinary jumps.

Example:

```su
if(result): {x > 10} [
    print(out): "large"
]
```

becomes conceptually:

```text
LOAD_VAR       R0, x
LOAD_CONST     R2, 10
GT             R0, R1, R2
JMP_FALSE      R0, else
LOAD_CONST     R0, "large"
PRINT          R0
STORE_VAR      out, R0
JMP            end
else:
end:
```

There is no runtime `if` command object.

## 8. Expressions

Expressions are compiled recursively into registers. Logical AND/OR use
short-circuit jumps.

Supported operators include:

```text
+ - * / %
> < >= <= == !=
&& ||
<< >> ^ & |
```

## 9. Hash semantics

The normal local-variable namespace remains separate from the hash namespace.

```su
set(name): "USSR"!
print(out): name?
```

compiles conceptually to:

```text
LOAD_CONST   R0, "USSR"
STORE_VAR    name, R0
STORE_HASH   name, R0
LOAD_HASH    R0, name
PRINT        R0
STORE_VAR    out, R0
```

## 10. UNO / OOP

UNO reference values remain reference-counted in `uno.c`.

The compiler emits:

- `DECODE_UNO` for UNO literals
- `OOP` for OOP operations

The VM does not duplicate the struct/vector implementation. It calls the
UNO primitive boundary supplied by `uno.c` / the OOP builtins. This preserves
the existing type checking, inheritance, field access, vector ownership, and
method behavior.

## 11. `eval`

`eval` is a compiler/VM boundary, not an old interpreter escape hatch.

The runtime performs:

```text
runtime string
    -> lexer/parser
    -> AST
    -> ussr_bc_compile()
    -> bytecode
    -> VM execution
```

It must never call `ussr_execute_program()`.

## 12. External commands

External processes are not VM CPU instructions. `EXTERNAL` is an explicit
host-service opcode. The VM owns the instruction dispatch; `ussr.c` owns the
POSIX process implementation.

## 13. Ownership

The compiler owns:

- instruction memory
- constant pool
- string pool
- function metadata

Runtime values remain owned by the USSR value system. Vector and struct values
retain/release through the existing UNO reference-counting implementation.

## 14. Important invariant

The following execution path is forbidden:

```text
bytecode -> command -> ussr_execute_command()
```

The required path is:

```text
command AST -> bc_compile_command() -> opcode -> VM switch -> primitive
```

That separation is the foundation of the USSR virtual machine architecture.
