# USSR Virtual Machine Specification

Status: current C VM implementation reference

## 1. Purpose

The USSR Virtual Machine (VM) executes compiled Unified Shell Script. The C compiler emits an in-memory UUASM instruction stream and translates it one-to-one into the VM instruction ABI. The VM also accepts textual UUASM directly through `vm_execute_uuasm()`.

The bytecode VM does not execute the parser's linked command list or AST. Lists
and trees are compiler input, not VM instructions.

The source architecture is:

    source -> preprocessor -> lexer/parser -> UUASM compiler -> UUASM
           -> one-to-one VM opcode translation -> bytecode -> VM

C is the first host implementation of the VM. The VM instruction set and value semantics are the portable language runtime contract.

File formats are named `.su` (USSR source), `.ua` (textual UUASM), and `.ru`
(final VM binary bytecode). The CLI runs all three; `.uuasm` remains a
compatibility suffix for assembler text. `ussr -c` writes `.ru` files, whose
layout and versioning are in [`RU_BYTECODE_FORMAT.md`](RU_BYTECODE_FORMAT.md). The
versioned text syntax is described in [`USSR_UUASM_SPEC.md`](USSR_UUASM_SPEC.md).

## 2. Machine state

The VM has 32 value registers:

    R0 .. R31

Every register contains a `ussr_value_t` value. The current implementation supports the existing primitive and UNO object value kinds defined by the USSR runtime.

The VM also keeps an instruction pointer and execution state. Call frames are
stored in a VM-owned frame stack; there are no public bytecode `SP` or `FP`
registers.

The resumable VM state includes:

    ip            instruction pointer
    running       non-zero while the VM is executing
    exit_code     VM termination status
    steps         instruction execution counter

The implementation limits a program to 1,000,000 instructions, 1,024 call
frames, and 2,000,000,000 executed steps.

## 3. Instruction format

The baseline instruction is fixed-width:

    uint8_t  opcode
    uint8_t  a
    uint8_t  b
    uint8_t  c
    uint32_t immediate

Total: 8 bytes.

Operands refer to the program's constant pool, string pool, and function
table; the in-memory instruction contains no pointers.

`ussr_bc_write_file()` writes a versioned file beginning with `USSRBC01`.
That writer is currently a C API only: the CLI has no matching bytecode
reader, and the emitted representation is not yet a finalized portable `.ru`
ABI.

## 4. Fetch/decode/execute cycle

The VM performs:

    instruction = code[IP]
    IP = IP + 1
    decode instruction
    execute instruction

A jump, call, or return may replace IP.

There is no `command->next` operation in the VM execution loop.

## 5. Opcode groups

### 5.1 Machine

    0x00 NOP

### 5.2 Value loading and movement

    0x01 LOAD_CONST
    0x02 LOAD_VAR
    0x03 STORE_VAR
    0x04 LOAD_HASH
    0x05 STORE_HASH
    0x06 DECODE_UNO
    0x07 GET
    0x08 VECTOR_GET
    0x09 VECTOR_SET

### 5.3 Arithmetic

    0x10 ADD
    0x11 SUB
    0x12 MUL
    0x13 DIV
    0x14 MOD
    0x15 NEG

Arithmetic follows existing USSR value semantics:

- integer + integer produces integer
- operations involving a real produce real where applicable
- division by zero is an error
- modulo requires integer operands
- invalid operand types are VM errors

### 5.4 Comparison and logical/bitwise operations

    0x16 EQ       0x17 NE       0x18 LT       0x19 LE
    0x1a GT       0x1b GE       0x1c LAND     0x1d LOR
    0x1e XOR      0x1f BAND     0x20 BOR      0x21 SHL
    0x22 SHR

### 5.5 Control flow

    0x30 JMP
    0x31 JMP_TRUE
    0x32 JMP_FALSE

Jump targets are bytecode instruction indices, not C pointers.

### 5.6 Function calls

    0x50 CALL
    0x51 RET
    0x52 RETURN

`CALL` identifies a VM function by function-table index.

The instruction fields are:

    A = first argument register
    B = argument count
    C = destination register for the returned value
    immediate = function index

A call creates a VM frame. The frame stores the caller IP, return register, function identity, and the state needed to restore the caller.

### 5.7 Runtime and host services

    0x40 SET        0x41 PRINT       0x42 CONCAT
    0x43 EXTERNAL   0x44 OOP         0x45 EVAL
    0x46 RANDOM64   0x47 SEED64      0x48 SCAN
    0x49 TIME       0x4a CHAIN       0x4b FILE
    0x4c CD         0x4d TEMPLATE

These are explicit VM operations. `EXTERNAL`, `OOP`, `EVAL`, `SCAN`, and file
operations use host/runtime services; they do not dispatch through the old
command-list interpreter.

### 5.8 Program control

    0xf0 HALT

`BREAK` and `CONTINUE` compile to concrete jump targets. Function returns use
`RET` or `RETURN`.

## 6. Register convention

The current compiler convention is:

    R0-R29   general temporary/value registers
    R30      compiler scratch where needed
    R31      function return register

For `CALL`, `A` is the first argument register, `B` the argument count, `C` the
destination register, and `immediate` the function-table index. Arguments are
copied into the function's named parameters before the callee's registers are
cleared for execution.

## 7. Constants

Literal values are stored in the program constant pool/reference table and loaded with:

    LOAD_CONST destination, constant

`LOAD_CONST` reads an entry from the program constant pool. Names and textual
operands are held in the string pool and are referenced by their index in the
instruction's `immediate` field.

Strings are immutable VM values from the bytecode compiler's perspective.

## 8. Variables

Normal USSR variables are accessed by bytecode:

    LOAD_VAR
    STORE_VAR

The variable store is provided by the USSR runtime. It is not the VM's
instruction stream or control-flow representation.

Explicit hash semantics are:

    name?   read hash entry
    value!  write hash entry under the command return name

These compile to `LOAD_HASH` and `STORE_HASH`.

## 9. Expressions

Expressions are compiled recursively into instructions.

For example:

    {a + b * 2}

becomes conceptually:

    LOAD_VAR   R0, a
    LOAD_VAR   R1, b
    LOAD_CONST R2, 2
    MUL        R3, R1, R2
    ADD        R4, R0, R3

The runtime does not call the old recursive expression evaluator for normal arithmetic.

Logical and comparison operations use their dedicated opcodes. In the current
compiler, both operands of `LAND` and `LOR` are evaluated before the operation;
they do not provide source-level short-circuit evaluation.

## 10. Commands

USSR commands are compiled into one of three categories:

1. Dedicated VM instruction
2. VM function call
3. Native runtime service

Examples:

    set     -> LOAD/STORE instructions
    add     -> ADD + STORE_VAR
    print   -> PRINT
    user function -> CALL
    external process -> EXTERNAL
    UNO operation -> OOP

The forbidden execution path is:

    VM -> ussr_execute_command() -> linked-list executor

That path must not exist in VM execution.

## 11. Functions

Function definitions are discovered during compilation. Each receives a stable function-table index and a bytecode entry address.

Function bodies are appended to the bytecode program and are not executed during top-level startup.

A function call:

    CALL function, arguments, return-register

creates a frame, binds parameters, changes IP to the function entry address, and starts normal VM execution.

`RET` restores the caller frame and places the function result in the caller's designated return register.

This supports recursion without executing a command list recursively.

## 12. Control flow compilation

### if

An `if` is compiled as conditional jumps.

Conceptual form:

    evaluate condition
    JMP_FALSE condition, else
    then bytecode
    JMP end
else:
    else bytecode
end:

### while

Conceptual form:

    loop:
        evaluate condition
        JMP_FALSE condition, end
        body
        JMP loop
    end:

`break` targets `end`.

`continue` targets `loop`.

The compiler maintains patch lists while compiling a loop. The VM only receives final instruction addresses.

## 13. Return values

Every USSR command has a return variable by language design.

For normal commands the compiler stores the resulting register value into the command return variable.

For functions, the callee produces its result in the VM return register and `RET` transfers that value to the caller's destination register. The caller then stores it into its requested return variable.

## 14. Errors

VM errors include:

- invalid opcode
- invalid register
- invalid constant/reference
- IP outside bytecode
- invalid jump target
- invalid function index
- call-stack overflow
- execution-step limit exceeded
- invalid operand type
- division by zero
- modulo by zero
- invalid shift count
- invalid function argument count
- return without a call frame

A VM error terminates the current VM invocation with a non-zero status.

`vm_execute()` and `vm_execute_uuasm()` return `USSR_VM_STATUS_DONE` (`0`) when
the program halts, `USSR_VM_STATUS_ERROR` (`-1`) on failure, and
`USSR_VM_STATUS_SUSPENDED` (`1`) when the instruction quantum expires or the
process yields/blocks. The scheduler resumes suspended VM state.

## 15. Memory ownership

VM registers own their current `ussr_value_t` contents.

Call frames own saved register values and saved frame state.

The bytecode program owns its instruction buffer and compiler metadata.

Some compiler metadata for `OOP` and `SCAN` instructions points to the parsed
source tree. That AST must remain alive while the compiled program uses this
metadata. The current binary writer does not serialize these source-site
tables, so such instructions are not portable in the emitted binary snapshot.

## 16. Compiler/runtime boundary

The compiler is responsible for:

- AST traversal
- name resolution
- function discovery
- bytecode emission
- jump patching
- register assignment
- compile-time validation

The VM is responsible for:

- instruction dispatch
- register state
- control flow
- calls/returns
- runtime value operations
- runtime errors

The C host runtime provides host-dependent facilities:

- terminal I/O
- filesystem
- process creation
- UNO object services

## 17. Portability goal

A portable serialized USSR program is intended to contain:

    bytecode header
    instruction stream
    constant pool
    string/symbol pool
    function table
    optional debug table

No serialized instruction may contain:

- C pointers
- `size_t` as an ABI field
- host addresses
- linked-list pointers
- AST pointers
- compiler-private memory addresses

The current `USSRBC01` writer is not yet that finalized cross-platform
contract: there is no matching reader, raw instructions use the host's
in-memory representation, and source-site metadata is omitted. Treat `.ru` as
the intended final VM-binary extension, not as a currently executable CLI
format.

## 18. Debugging interface

`ussr_bc_dump()` prints a human-readable bytecode disassembly, including
instruction indices, opcode names, operands, immediates, and pool/function
counts. Register tracing and interactive breakpoints are not currently
provided.

## 19. Execution example

Source:

    [
        set(a): 10
        set(b): 20
        add(result): a b
        print(out): result
    ]

Conceptual bytecode:

    LOAD_CONST R0, 10
    STORE_VAR R0, a
    LOAD_CONST R0, 20
    STORE_VAR R0, b
    LOAD_VAR R0, a
    LOAD_VAR R1, b
    ADD R2, R0, R1
    STORE_VAR R2, result
    LOAD_VAR R0, result
    PRINT R0
    HALT

The VM executes those instructions sequentially. There is no command-list traversal during execution.

## 20. Runtime boundary

The parser representation is compiler input. The runtime boundary is:

    parser representation -> UUASM compiler -> one-to-one opcode translation -> VM

and never:

    parser representation -> runtime execution

The normal compiled execution path does not traverse command lists. The C
runtime still contains a separate tree-walking execution path for legacy and
advanced-control contexts; it is not the bytecode VM's instruction loop.

## 21. Current limitations and next work

The C source compiler, textual `.ua` assembler, opcode translator, and VM are
implemented. Remaining format/runtime work includes:

- a `.ru` reader and executable CLI path;
- a portable, explicitly endian-defined binary encoding and complete metadata
  serialization;
- round-trip tests for bytecode writing and loading;
- preserving or replacing AST-backed `OOP` and `SCAN` metadata in serialized
  programs.
