# USSR User Manual

USSR (Unified Shell Script) is a small command-oriented scripting
language: every operation is a *command* with a name, a return
variable, and a list of arguments. Source is preprocessed, parsed,
compiled to bytecode, and run on a register-based virtual machine —
none of that is visible to you as a user, but it's why USSR programs
run the same way every time regardless of what's on disk.

This manual documents the language as built. A short appendix at the
end lists the handful of details worth double-checking against your
own grammar/compiler source before relying on them in production —
everywhere else, what's written here is confirmed against the actual
implementation.

---

## 1. Running USSR

**Interactively:**

```
./ussr
```

```
USSR Unified Shell Script REPL
USSR v0.1
Enter a command list or press Ctrl-D to exit.

ussr>
```

Type a command and press Enter. If you open a `[` without closing it,
the prompt changes to `... ` and keeps reading lines until brackets
balance, so you can write multi-line blocks interactively. Press
Ctrl-D (Ctrl-Z on Windows) on an empty line to exit. Arrow-key history
is supported on every platform.

**As a script file:**

```
./ussr myscript.su
```

On Windows, the same binary and the same `.su` scripts work
unmodified: `ussr.exe myscript.su`.

---

## 2. The core idea: everything is a command

```
name(return_variable): argument argument ...
```

- `name` is the command being invoked.
- `return_variable` is where its result gets stored — a plain
  variable name. If you don't care about the result, the convention
  is to use `_`.
- Every command needs **at least one argument** — there's no
  zero-argument form. If a command conceptually needs no input (like
  creating an untyped vector), pass an explicit empty string: `vec(v):
  ""` rather than `vec(v):`.
- Arguments are separated by whitespace, not commas.
- `#` starts a comment that runs to the end of the line.

```
# a comment
print(_): "hello, world"
```

---

## 3. Values

| Kind    | Syntax                          | Notes |
|---------|----------------------------------|-------|
| String  | `"like this"`                    | Backslash-escapes are supported (`\"`, `\\`, etc). |
| Integer | `42`, `-7`                        | No decimal point. |
| Real    | `3.0`, `-1.5`                     | **Must** include a decimal point. `3` and `3.0` are different types — this matters once a value goes through UNO encode/decode (§8), where a real always round-trips with its `.0`, even when whole. |
| Boolean | `true`, `false`                  | |
| Null    | `null`                            | Every struct field and vector slot accepts `null` regardless of its declared type (§8). |

---

## 4. Variables

Write a variable's bare name as an argument to read it:

```
set(x): 5
print(_): x
```

Assigning happens implicitly through any command's return-variable
slot — `set` is the plain "just store this value" command when you
don't need any actual computation.

---

## 5. Expressions: `{ ... }`

Curly braces compute a value from arithmetic/logical/bitwise
operators, usable anywhere an argument is expected:

```
set(total): {price * quantity}
if(_): {total > 100} [
    print(_): "big order"
]
```

Supported operators: `+ - * / %` (arithmetic), `> < >= <= == !=`
(comparison), `&& ||` (logical, short-circuiting), `<< >> ^ & |`
(bitwise shift/xor/and/or).

There are also dedicated two-argument arithmetic commands that skip
the braces entirely, useful when the operands are already named
values:

```
add(sum): a b
sub(diff): a b
mul(product): a b
div(quotient): a b
mod(remainder): a b
```

---

## 6. Command blocks: `[ ... ]`

A block is zero or more commands, one per line, wrapped in square
brackets. Blocks are used for:

- **The whole program**, optionally: a script can start with `[` and
  end with `]`, or just be a bare sequence of commands with no
  wrapper — both work identically.
- **Loop bodies** (§7).
- **Function bodies** (§7).

---

## 7. Control flow

```
while(_): {condition} [
    ...body...
]
```

`break`/`continue` inside a loop body work as you'd expect:

```
set(i): 0
while(_): {i < 10} [
    add(i): i 1
    if(_): {i == 5} [
        continue(_): ""
    ]
    print(_): i
]
```

**User-defined functions.** Any command whose arguments end in a `[
... ]` block — and whose name isn't `if`/`while`/`break`/
`continue`/`return` — defines a function the first time it's written
that way. Every bare-identifier argument before the block becomes a
parameter name:

```
square(result): x [
    mul(result): x x
    return(_): result
]
```

Call it like any other command:

```
square(y): 5
print(_): y   # 25
```

Recursion works correctly — each call gets its own saved copy of the
parameter variables, restored when the call returns, so a recursive
call doesn't clobber the outer call's parameters of the same name.
Variables a function sets internally (not its parameters) are *not*
isolated this way — they're plain global variables, same as anywhere
else in the language, so name them carefully if you're writing
recursive functions with internal temporaries.

---

## 8. Structs, vectors, and UNO

USSR has a small, deliberately minimal object layer: structs (records
with typed fields), a growable vector container, and UNO (Unified
Object Notation) — a text format for moving a struct or vector through
a string, a file, a pipe, or straight into source code.

### 8.1 Declaring a struct

```
struct(_): "Point" "x:number" "y:number"
```

The first argument is the type name; the rest are `"field:type"`
specs. `type` is one of `string`, `number`, `boolean`, `null`,
`vector`, `any`, or the name of another struct. Every field accepts
`null` regardless of its declared type.

The return-variable slot (`_` above) is unused by `struct` — write
`_` by convention.

### 8.2 Creating and using an instance

```
new(p): "Point" 1 2
getf(x): p "x"
setf(_): p "x" 9
```

- `new(var): "TypeName" arg1 arg2 ...` — positional construction, one
  argument per field in declaration order.
- `getf(var): instance "field"` — read a field.
- `setf(_): instance "field" value` — write a field, type-checked
  against its declared type.

Struct instances are **reference types**: assigning one to a second
variable aliases the same underlying instance, so mutating through
either variable is visible through both.

### 8.3 Vectors

```
vec(items): ""           # untyped: holds any mix of values
vec(points): "Point"     # typed: every push() must be a Point

push(_): items 1
push(_): items "two"

len(n): items
at(first): items 0
```

- `vec(var): "ElementType"` — pass `""` for an untyped vector.
- `push(_): vector value` — append; type-checked if the vector is
  typed.
- `at(var): vector index` — zero-based read.
- `len(var): vector` — element count.

Vectors are reference types too, same aliasing behavior as structs.

### 8.4 UNO — encoding, decoding, and literals

`encode`/`decode` convert between a live struct/vector and its text
form:

```
encode(s): p
# s = "Point{x=1,y=2}"

decode(p2): s
```

UNO's own grammar:

```
Point{x=1,y=2}
[Point{x=1,y=2},Point{x=3,y=4}]
Tag{label=`hello world`}
```

- Objects: `TypeName{field=value,field=value}`
- Vectors: `[value,value,...]`
- Strings *inside* UNO use backticks (`` ` ``), not `"` or `'` — this
  is deliberate: it means a UNO document never needs escaping no
  matter where it's embedded.
- Numbers/booleans/null use the same literal forms as the language
  itself.

You can also write a UNO document directly in source as a literal,
using single quotes — no `decode()` call needed:

```
set(p3): 'Point{x=5,y=6}'
set(pts): '[Point{x=7,y=8},Point{x=9,y=10}]'
```

The three quote characters never collide: `"..."` is a normal USSR
string, `'...'` is a UNO literal, and `` `...` `` is a string *inside*
a UNO document.

### 8.5 What UNO can't represent

A string containing a literal backtick or a newline can't be encoded
— `encode()` fails rather than silently corrupting it. Keep that in
mind for user-supplied text passing through `encode`.

---

## 9. External commands

Any command name that isn't a builtin, an OOP command (§8), or a
user-defined function is looked up on `PATH` and run as an external
program:

```
ls(_): "-la"
git(status): "status" "--short"
```

Two things worth knowing:

- The value bound to the return variable is the process's **exit
  code** (an integer), not its output. Standard output/error print
  directly to your terminal (or wherever the script's own stdout/
  stderr are pointed) — there's currently no way to capture a
  program's output into a variable.
- On normal exit, you get the process's real exit code. If it was
  killed by a signal (POSIX) or exited via an unhandled exception
  (Windows), you get `128 + signal-number` (or an analogous mapped
  value on Windows) — the same "exit code > 127 means it died
  abnormally" convention shells generally use.

---

## 10. `eval`: running code from a string

```
eval(x): "set(x): 5 mul(x): x 2"
print(_): x   # 10
```

`eval` parses, compiles, and runs the given string as its own
sub-program, sharing the same global variables as the rest of your
script. After it finishes, `eval`'s own return variable is refreshed
by re-reading a variable with that same name — so the code you hand
to `eval` should set a variable named exactly like `eval`'s own return
slot if you want a value back out.

---

## 11. The preprocessor

Lines starting with `$` are preprocessor directives, handled before
the parser ever sees the file:

```
$define MAX_RETRIES 3

$include "helpers.su"

$ifdef DEBUG
print(_): "debug build"
$endif

$if defined(DEBUG)
print(_): "verbose"
$else
print(_): "quiet"
$endif
```

- `$define NAME value` — simple text substitution.
- `$define NAME(params) replacement` — parameterized macro.
- `$include "file.su"` (or a bareword path) — inlines another file's
  content at that point, resolved relative to the *including* file's
  own directory when given a relative path.
- `$ifdef` / `$if defined(NAME)` / `$else` / `$endif` — conditional
  inclusion.

`#`-comments are handled by the language's own lexer, not the
preprocessor, so they work identically inside and outside `$define`
bodies.

---

## 12. Errors

A runtime error (bad argument type, division by zero, an invalid VM
operation, a failed `struct`/`getf`/`setf`/`push`, and so on) prints a
message to `stderr` and halts the entire script immediately — there's
no exception/try-catch mechanism. The process exits non-zero.

---

## Appendix: verify against your build

Everything above is confirmed against the implementation as built
through this project. A few things came from the architecture
spec/design intent rather than something I directly traced end-to-end
in source, and are worth a quick sanity check before you rely on them
in something important:

- **`if`'s exact argument shape** — documented above as `if(_):
  {condition} [ then ] [ else ]` with an optional else block, based on
  the VM spec's description of if/else compilation. I didn't
  personally verify the exact argument count/order the compiler
  expects for `if` the way I did for `while` (which I traced directly).
- **`?` / `!` sigils** — the grammar reserves these for a "hash"
  lookup/assignment mechanism (`name?` reads a hash entry, `value!`
  marks an assignment) distinct from plain variables. Real, separate
  hash storage exists in the runtime (`ussr_hash_get_value`/
  `ussr_hash_set_value`), but this manual doesn't document the exact
  surface syntax for triggering it, since I never saw a worked example
  exercise it directly.
- **Directive list** — only `$define`, `$include`, `$ifdef`, `$if
  defined(...)`, `$else`, `$endif` are confirmed present. There's no
  confirmed `$ifndef`, `$elif`, or similar — don't assume one exists
  without checking.
