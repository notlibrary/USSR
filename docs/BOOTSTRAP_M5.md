# USSR self-host bootstrap M5

This build combines the M2 bytecode bootstrap behavior with the M4 self-hosted interpreter/compiler fixes.

- Normal `init(...)` programs use the established bytecode compiler/VM path.
- `for` and multi-value `scan` programs can use the USSR AST interpreter path.
- Programs without `init(...)` use the AST path unless they contain `choose`; `choose` remains on the bytecode path because the AST path does not yet implement option/default semantics.
- `=` is the equality operator.
- Nested arithmetic expressions are parsed correctly.
- Later function definitions override earlier library definitions.
- Multi-value `scan` works in the AST path.

Verified:

M2:
- hello.su -> USSR / 30
- fac.su (input 5) -> 120
- loops.su -> 0..9
- hash.su -> USSR / 42
- choose.su -> two
- elif_else.su -> elif
- flow2.su -> 1 2 4 5 6

M4:
- definition.su -> 1
- quadratic.su (input 1 2 1) -> -1, -1
- simple_for.su -> 0..4
