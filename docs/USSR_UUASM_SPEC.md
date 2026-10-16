# UUASM specification

UUASM (USSR Ukraine ASM) is USSR's explicit assembly/intermediate layer. The C
compiler emits a UUASM instruction stream and metadata first; the assembler
validates and translates each instruction to one VM instruction. A valid
translation preserves the opcode and all four operands exactly. No UUASM
instruction expands into multiple VM instructions.

The in-memory instruction is fixed-width and matches the VM's 8-byte
instruction layout:

```text
opcode:8  a:8  b:8  c:8  immediate:32
```

The opcode numbers and operand meanings are the VM ABI in
[`USSR_BYTECODE_ARCHITECTURE.md`](USSR_BYTECODE_ARCHITECTURE.md). Every opcode
in that ABI has the same-named UUASM mnemonic; unknown opcodes are rejected.

## Text syntax

UUASM source is UTF-8/ASCII text, one directive or instruction per line.
Whitespace separates tokens, `#` and `;` begin comments outside quoted text,
and quoted strings use `\\`, `\"`, `\n`, `\r`, and `\t` escapes.

```text
.uuasm 1
.const 0 int 42
.const 1 real 3.5
.const 2 bool true
.const 3 null
.const 4 string "hello, world"
.string 0 "result"

start:
LOAD_CONST R0 _ _ 0
STORE_VAR R0 _ _ 0
PRINT R0 _ _ 0
HALT _ _ _ 0
```

`.const` and `.string` indices start at zero and must be dense and ordered.
Supported constant kinds are `int`, `real`, `bool`, `null`, and `string`.
Strings referenced by variable/hash/service instructions live in the string
pool. Operands `A`, `B`, and `C` are one-byte fields: write registers as
`R0`–`R31`, or use an unsigned integer where the opcode defines a count or
flag. `_` means zero. The final operand is an unsigned 32-bit immediate.
`@label` is accepted as the immediate only for `JMP`, `JMP_TRUE`, and
`JMP_FALSE`; it resolves to an instruction index, not a source-line number.
Labels are case-sensitive and unique.

Function metadata is declared separately from instructions:

```text
.function increment @increment result 1 amount
```

This declares the function table entry and parameter names; the entry label
points at the first instruction of its body. `CALL A B C immediate` takes the
first argument register in `A`, the argument count in `B`, the result register
in `C`, and the function-table index in `immediate`. `RET` returns the value
stored in the function's declared return variable. Function names and
parameter names are case-sensitive.

Every file must begin with `.uuasm 1` and end in `HALT`. Invalid directives,
duplicate/unresolved labels, malformed operands, invalid opcodes, and invalid
constant/function metadata fail assembly with a line-numbered diagnostic.
VM-level operand and runtime errors remain VM errors.

## Calls and VM entry points

Parsed USSR source uses `ussr_uuasm_compile()` to produce UUASM, then
`ussr_uuasm_compile_to_bytecode()` performs the validated one-to-one opcode
translation used by the existing scheduler and bytecode runtime. Existing
`ussr_bc_compile()` remains as a compatibility wrapper for that pipeline.

Text assembly is available through `ussr_uuasm_assemble()`. Call
`vm_execute_uuasm()` to execute its program directly; quantum-limited runs can
be resumed by passing the same program again. `.ua` is the canonical file
extension for textual UUASM; the CLI also accepts `.uuasm` for compatibility:

```text
ussr tests/uuasm_loop.ua
ussr tests/uuasm_function.ua
```

The bytecode VM still owns runtime execution. The parser AST remains compiler
input only. Text assembly does not fabricate parser-owned source metadata:
AST-backed `OOP` and `SCAN` operations requiring such metadata must be
generated from parsed USSR source, not written as standalone UUASM.

`.ru` is reserved for the final serialized VM bytecode format. The current C
library can write a versioned bytecode file with `ussr_bc_write_file()`, but
there is no `.ru` reader or CLI execution path yet.

## Bootstrap

`src/russia/russia.su` retains its own host-independent compiler/VM and
continues to bootstrap `.su` programs. Its `ua_emit` compiler layer emits one
UUASM opcode per bootstrap instruction, records the canonical mnemonic, and
validates the supported opcode set. The bootstrap VM maps those opcode IDs
one-to-one to its legacy dispatch numbers; operands and instruction counts
remain unchanged. This adapter is internal to the bootstrap and does not
serialize bootstrap instruction records as C VM bytecode. The C textual
assembler and VM implement the versioned assembler text format, conventionally
stored as `.ua` (`.uuasm` is accepted for compatibility).
