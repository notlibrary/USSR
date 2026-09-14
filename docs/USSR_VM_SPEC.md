# USSR Virtual Machine Specification

Version: 0.1
Status: architecture baseline

## 1. Purpose

The USSR Virtual Machine (VM) is the execution machine for Unified Shell Script. USSR source is parsed into an intermediate representation and compiled into a flat bytecode instruction stream. Runtime execution consumes bytecode only.

The VM must never execute the parser's linked command list or AST. Lists and trees are compiler input, not runtime execution structures.

The long-term architecture is:

    source -> preprocessor -> lexer/parser -> bytecode compiler -> bytecode -> VM

C is the first host implementation of the VM. The VM instruction set and value semantics are the portable language runtime contract.

## 2. Machine state

The VM has 32 general-purpose registers:

    R0 .. R31

Every register contains a `ussr_value_t` value. The current implementation supports the existing primitive and UNO object value kinds defined by the USSR runtime.

Special machine registers:

    IP  instruction pointer
    SP  value/VM stack pointer
    FP  current call-frame pointer

The first implementation keeps these as 32-bit VM addresses so bytecode does not depend on host pointer size.

Additional state:

    running       non-zero while the VM is executing
    exit_code     VM termination status
    steps         instruction execution counter

The VM has a bounded call depth and instruction count to prevent accidental runaway execution.

## 3. Instruction format

The baseline instruction is fixed-width:

    uint8_t  opcode
    uint8_t  a
    uint8_t  b
    uint8_t  c
    uint32_t immediate

Total: 8 bytes.

The bytecode format never stores host pointers directly.

References to compiler/runtime objects use 32-bit reference indices into a separate reference table in the host implementation. This is a transitional implementation mechanism; serialized portable bytecode will eventually replace host reference entries with proper constant-pool and symbol-pool records.

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

    0x02 EVAL_ARG
    0x03 STORE_VAR
    0x07 LOAD_VAR
    0x08 LOAD_HASH
    0x09 LOAD_CONST

`EVAL_ARG` is a compatibility/deferred-value instruction. It is reserved for values which cannot yet be represented by ordinary VM instructions, such as UNO literals or other deferred runtime objects. Ordinary literals and expressions should compile to normal bytecode.

### 5.3 Arithmetic

    0x0a ADD
    0x0b SUB
    0x0c MUL
    0x0d DIV
    0x0e MOD
    0x0f NEG

Arithmetic follows existing USSR value semantics:

- integer + integer produces integer
- operations involving a real produce real where applicable
- division by zero is an error
- modulo requires integer operands
- invalid operand types are VM errors

### 5.4 Comparison and logical/bitwise operations

    0x13 CMP

`CMP` uses the referenced USSR operator to implement:

    >  <  >=  <=  ==  !=
    && ||
    << >>
    ^ & |

The compiler should eventually specialize these into separate opcodes when useful, but the grouped form is the v0.1 ABI.

### 5.5 Control flow

    0x10 JMP
    0x11 JMP_TRUE
    0x12 JMP_FALSE

Jump targets are bytecode instruction indices, not C pointers.

### 5.6 Function calls

    0x05 CALL
    0x06 RET

`CALL` identifies a VM function by function-table index.

The instruction fields are:

    A = first argument register
    B = argument count
    C = destination register for the returned value
    immediate = function index

A call creates a VM frame. The frame stores the caller IP, return register, function identity, and the state needed to restore the caller.

### 5.7 Runtime/native services

    0x01 NATIVE

`NATIVE` is a host-runtime boundary, not the old command-list execution engine.

It is used for operations that inherently require the host environment, such as OOP runtime services and external process execution. Built-in language operations should preferably become dedicated VM instructions.

### 5.8 Program control

    0x20 BREAK
    0x21 CONTINUE
    0x22 RETURN
    0xf0 HALT

During compilation, `BREAK` and `CONTINUE` are patched to concrete jump targets. Function `RETURN` is compiled to value production followed by `RET`.

## 6. Register convention

The first compiler/runtime convention is:

    R0-R29   general temporary/value registers
    R30-R30  compiler scratch where needed
    R31      function return register

This convention is not a restriction on the future VM ABI; it is the v0.1 compiler convention.

Function arguments are placed starting at `R0` before `CALL`.

## 7. Constants

Literal values are stored in the program constant pool/reference table and loaded with:

    LOAD_CONST destination, constant

The compiler must not emit repeated C literals into the instruction stream.

Strings are immutable VM values from the bytecode compiler's perspective.

## 8. Variables

Normal USSR variables are accessed by bytecode:

    LOAD_VAR
    STORE_VAR

The existing local variable store remains a runtime storage implementation detail. It is not an execution structure.

Explicit hash semantics are:

    name?   read hash entry
    value!  write hash entry under the command return name

The final VM ABI will expose these as `LOAD_HASH` and `STORE_HASH` rather than routing through generic command execution.

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

Logical `&&` and `||` must eventually compile with short-circuit jumps, preserving existing USSR semantics.

## 10. Commands

USSR commands are compiled into one of three categories:

1. Dedicated VM instruction
2. VM function call
3. Native runtime service

Examples:

    set     -> LOAD/STORE instructions
    add     -> ADD + STORE_VAR
    print   -> NATIVE/PRINT service during v0.1
    user function -> CALL
    external process -> NATIVE host service
    UNO operation -> NATIVE host service until a stable VM ABI exists

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

## 15. Memory ownership

VM registers own their current `ussr_value_t` contents.

Call frames own saved register values and saved frame state.

The bytecode program owns its instruction buffer and compiler metadata.

The parser AST is owned by the parser-side program object and is freed after compilation. It is not retained as an execution object.

The host reference table in v0.1 points at compiler-owned AST objects and therefore must not outlive the AST. A future serialized bytecode format must eliminate this limitation by storing true constants/symbols in the bytecode file.

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

The C host runtime is responsible only for host-dependent facilities:

- terminal I/O
- filesystem
- process creation
- UNO object services
- future OS interfaces

## 17. Portability goal

A future portable USSR program is represented by:

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

A conforming USSR VM implemented in another language should be able to execute the same bytecode.

## 18. Debugging interface

The VM should eventually support:

    disassemble bytecode
    dump registers
    dump call stack
    trace instructions
    break at bytecode address

A disassembler is especially important because bytecode is now the language's executable representation.

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

## 20. v0.1 implementation rule

The existing parser representation may remain temporarily because it is currently the compiler input. The critical architectural boundary is:

    parser representation -> compiler -> bytecode -> VM

and never:

    parser representation -> runtime execution

Once the bytecode compiler covers all ordinary USSR semantics, the old `ussr_execute_program`, `ussr_execute_list`, and `ussr_execute_command` path can be removed from the executable runtime.

## 21. Next ABI milestones

1. Compile all primitive expressions to VM opcodes.
2. Compile `?` and `!` to dedicated hash instructions.
3. Compile `concat` to a VM instruction.
4. Move `print` to a dedicated VM/runtime instruction.
5. Compile `eval` through a VM-owned compiler invocation.
6. Give external processes a dedicated host-service ABI.
7. Give UNO a VM-native call ABI.
8. Remove `ussr_execute_command()` from the VM build.
9. Remove `ussr_execute_list()` from normal runtime.
10. Replace host reference tables with a serialized constant/symbol pool.
11. Add bytecode serialization and versioning.
12. Add an independent VM test suite using hand-written bytecode.

The final goal is that `ussr.c` supplies value/storage/host facilities while the VM defines how a USSR program executes.
