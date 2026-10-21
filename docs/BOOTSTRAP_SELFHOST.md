# USSR self-host bootstrap

`russia.su` is the self-hosted compiler/interpreter bootstrap.  It does not use
`eval`, `chain`, or `capture` to hand the target program back to the C runtime.

The pipeline is:

1. USSR lexer written in USSR (`lx_scan`).
2. USSR parser written in USSR (`ps_parse`).
3. AST representation written in USSR (`nd`).
4. USSR UUASM instruction emitter/compiler written in USSR (`ua_emit`, `cv_*`).
5. Bootstrap VM written in USSR (`vm_*`), with an explicit one-to-one adapter
   between C VM opcode IDs and its legacy dispatch IDs.
6. The C executable is only the stage-0 bootstrap that starts `russia.su`.

The bootstrap currently includes the fixes for:

- bytecode `continue`: emit the jump before recording its patch location;
- `do(condition/body) ... loop(condition)` execution in the self-hosted VM;
- preserving the host-independent compiler/VM path instead of using nested C `eval`.

Build the stage-0 executable using the normal project build, then run:

    ./ussr src/russia/russia.su tests/hello.su

Textual UUASM runs the same way (`./ussr src/russia/russia.su prog.ua`); see
the bootstrap section of `USSR_UUASM_SPEC.md`. The interpreted program's
standard library is read from `src/libs/` (or `libs/`, `../libs/`, the
current directory). `standard.su` is not `$include`d by `russia.su` itself
because its PRNG definitions shadow host builtins.

Known issue: interpreting the PRNG definitions now in `src/libs/standard.su`
prints a stray `undefined variable` line before the program output.

This is a bootstrap stage, not yet a replacement for every OS-facing C service.
External process execution and some compatibility commands still belong to the
stage-0 host runtime, and forward function declarations are not yet implemented
by the USSR-side compiler.

The bootstrap's internal `In` records now retain the UUASM mnemonic and C VM
opcode ID. `ua_emit` validates supported opcodes before emitting one record;
`vm_step` maps that opcode to the existing bootstrap dispatch number without
changing operands or expanding instructions. This keeps scheduler execution
and legacy `.su` behavior on the bootstrap VM while making its compiler output
explicitly UUASM-shaped.
