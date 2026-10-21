# `.ru` Bytecode File Format

A `.ru` file is a compiled USSR program: the VM instruction stream and the
pools it refers to, as produced by `ussr -c file.su` and run by
`ussr file.ru`. Writer and loader live in `src/ussr_ru.c`.

Format version: **2**. VM ABI version: **2**.

## Rules

- Every multi-byte integer is **little-endian**, independent of the host.
- Nothing in the file is a host pointer, `sizeof`, or padding.
- A *string* is a `u32` byte length followed by that many bytes (UTF-8, no
  terminating NUL, no NUL inside). Length `0xFFFFFFFF` means "absent"
  (NULL); it is only allowed where noted.
- A *count* is a `u32`.

## Layout

```
offset  size  field
0       8     signature   89 55 53 53 52 0D 0A 1A   ("\x89USSR\r\n\x1A")
8       2     format version (2)
10      2     flags (must be 0)
12      4     VM ABI version (2)
16      4     producer version: major << 16 | minor << 8 | patch (informational)
20      4     payload length: bytes from offset 24 up to the checksum
24      ...   payload: the sections below, in this order
end-4   4     CRC-32 of every byte before it (IEEE, as in zip/PNG)
```

The signature follows the PNG idea: the high byte catches 7-bit transfers,
`\r\n` and `\x1A` catch line-ending conversion and text-mode truncation.

### 1. Code

```
count  n (1 .. 1 000 000)
n x    opcode u8, a u8, b u8, c u8, immediate u32       (8 bytes each)
```

Instructions are exactly the VM instruction records of
[USSR_VM_SPEC.md](USSR_VM_SPEC.md); the last one must be `HALT`.

### 2. Constants

```
count  n
n x    tag u8, then:
         0 null      (nothing)
         1 integer   i64 (two's complement)
         2 real      u64 holding the IEEE-754 binary64 bits
         3 string    string (not absent)
         4 boolean   u8, 0 or 1
```

Structs and vectors never appear as constants: the compiler builds them at
run time from UNO text and commands.

### 3. Strings

```
count  n
n x    string (may be absent)
```

Variable, function, command and return names, UNO literal text and format
strings referenced by instruction immediates.

### 4. Functions

```
count  n
n x    name string, return name string (may be absent),
       entry u32 (instruction index), parameter count u32 (<= 32),
       parameter names: string x count,
       local count u32 (>= parameter count + 1),
       local names: string x count
```

The local names are the variables each call of the function gets for
itself: its parameters, its return variable and every other name it
assigns that is not declared `global` (user manual, "Variables in
functions"). Inside a function's code, `LOAD_VAR`/`STORE_VAR` carry the
compiler's resolution in operand `B`: `0` look the name up (the call's
locals first, then the globals), `1` the call's local in slot `C` (an index
into the local names), `2` the global. A `B` above 2 is rejected.

### 5. Command call sites, 6. Scan call sites

```
count  n
n x    instruction u32, command name string (may be absent),
       return name string (may be absent), argument count u8,
       per argument: kind u8, assignment u8 (0 or 1),
                     kind 1 only: variable name string
```

`OOP` and `SCAN` instructions look up how their call was written: which
return variable to fill, which arguments were written as `name=`, and which
were bare variable names (`scan` stores into them, `load` and `async` take a
definition name). Kind 0 is any value argument, kind 1 a bare variable. Each
site's instruction must be an `OOP` (section 5) or `SCAN` (section 6)
instruction. The loader rebuilds a small command record per site so the VM
sees the same metadata it gets from parsed source.

A `[ ... ]` block passed as a command argument (for example to `@`) is
executable syntax tree, not data; format 1 cannot store it and `-c` refuses
such programs with a message naming the command.

## Validation on load

Before anything runs, the loader rejects the file (exit status 1, message on
stderr, nothing executed) when:

- the signature is wrong (also catches source text renamed to `.ru`);
- the format version or VM ABI differs from the interpreter's, or flags are
  set;
- the payload length disagrees with the file size (truncated or trailing
  data);
- the CRC-32 does not match;
- any count or string runs past the end of the payload, a string contains a
  NUL, or a required string is absent;
- an opcode is unknown, the program does not end with `HALT`, or a jump,
  constant, function, or string index is out of range;
- a function entry is outside the code, or a call site does not point at an
  instruction of its kind;
- an integer constant does not fit the platform's integer type (a file built
  on 64-bit Linux with a value above 2^31 is rejected on Windows, where USSR
  integers are 32-bit);
- bytes remain after the last section.

The VM still checks register and pool operands as it executes, as it does
for `.ua` programs.

## Compatibility

- **Format version** changes when the container layout changes.
- **VM ABI version** (`USSR_RU_VM_ABI` in `src/ussr_ru.h`) changes whenever
  an opcode, operand meaning, or calling convention changes. A `.ru` file is
  only run by an interpreter with the same ABI; rebuild it from source after
  upgrading across an ABI change.
- The producer version is recorded for diagnostics only.
- `ussr_bc_write_file()` in `ussr_bytecode.c` (`USSRBC01` header) is an older
  debugging dump in host byte order; it is not the `.ru` format and has no
  loader.

History: format 2 / ABI 2 added function locals (local name lists, the
`B`/`C` operands of `LOAD_VAR`/`STORE_VAR`; `CALL`/`RET` no longer save and
restore parameter variables in the shared scope). Files from format 1 are
rejected; rebuild them with `-c`.
