# USSR self-host bootstrap

`russia.su` is the self-hosted compiler/interpreter bootstrap.  It does not use
`eval`, `chain`, or `capture` to hand the target program back to the C runtime.

The pipeline is:

1. USSR lexer written in USSR (`lx_scan`).
2. USSR parser written in USSR (`ps_parse`).
3. AST representation written in USSR (`nd`).
4. USSR bytecode compiler written in USSR (`cv_*`).
5. USSR bytecode VM written in USSR (`vm_*`).
6. The C executable is only the stage-0 bootstrap that starts `russia.su`.

The bootstrap currently includes the fixes for:

- bytecode `continue`: emit the jump before recording its patch location;
- `do(condition/body) ... loop(condition)` execution in the self-hosted VM;
- preserving the host-independent compiler/VM path instead of using nested C `eval`.

Build the stage-0 executable using the normal project build, then run:

    ./ussr src/russia.su tests/hello.su

or from `src`:

    ./../ussr russia.su ../tests/hello.su

This is a bootstrap stage, not yet a replacement for every OS-facing C service.
External process execution and some compatibility commands still belong to the
stage-0 host runtime, and forward function declarations are not yet implemented
by the USSR-side compiler.
