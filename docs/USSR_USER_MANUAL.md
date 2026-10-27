# USSR User Manual

USSR (Unified Shell Script REPL) is a small command-oriented scripting
language: every operation is a *command* with a name, a return
variable, and a list of arguments. Source is preprocessed, parsed,
compiled through UUASM, and run on a register-based virtual machine.

USSR uses three file extensions for its language and execution formats:

| Extension | Meaning | Current use |
|-----------|---------|-------------|
| `.su` | USSR source script ("script user") | Run source scripts with `ussr file.su`. |
| `.ua` | USSR Ukraine assembler, textual UUASM | Run assembler text directly with `ussr file.ua`. `.uuasm` remains a compatible legacy extension. |
| `.ru` | Final VM binary bytecode | Produce with `ussr -c file.su`, run with `ussr file.ru`. Format: [RU_BYTECODE_FORMAT.md](RU_BYTECODE_FORMAT.md). |

This manual documents the language and libraries in the current source
tree. Runtime limitations that affect behavior are called out alongside
the relevant feature.

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
balance, so you can write multi-line blocks interactively.

**Prompts.** The `PS1` environment variable sets the prompt and `PS2` the
continuation prompt; when a variable is unset or empty the defaults
`ussr> ` and `... ` are used. Both are read again for every prompt, so
`setenv(_): "PS1" "dev> "` inside the REPL takes effect on the next line.
The value is only displayed, never run. These escapes are expanded:

| Escape | Shows |
|--------|-------|
| `\u` | user name (`USERNAME`, else `USER`) |
| `\h` | host name (`COMPUTERNAME`, else `HOSTNAME`) |
| `\w` | current working directory |
| `\W` | last component of the working directory |
| `\v` | interpreter version, e.g. `0.4` |
| `\$` | `$` |
| `\\` | `\` |

Any other backslash pair is shown as written. Terminal escape sequences in
the prompt are passed through but not measured, so cursor placement while
editing may be off with colored prompts.

```
PS1='\u@\h \W\$ ' ./ussr
```

When standard input is not a terminal (a pipe or a file), the REPL reads it
line by line on every platform, printing the prompts without line editing. Press
Ctrl-D (Ctrl-Z on Windows) on an empty line to exit. Arrow-key history
is supported on every platform.

**History file.** The REPL keeps its history across sessions in
`.ussr_history` in your home folder (`HOME`, else `USERPROFILE` on
Windows): one entry per line, oldest first. Every line you type is added
at once, so a closed window loses nothing; Up and Down reach entries from
earlier sessions, and Tab completion learns from them. `USSR_HISTFILE`
names another file, set but empty it keeps none; `USSR_HISTSIZE` (default
1000) is how many entries are loaded and kept, the file being trimmed to
that many when the REPL starts. A repeat of the previous entry is not
saved. Only a REPL reading a terminal uses the file, so piped input and
test runs never touch your history. ussr and the self-hosted
interpreter's REPL (`ussr src/russia/russia.su`) share the same file.

**Help.** `help(_):` lists every builtin by group; `help(_): "name"` shows
one command's usage, and the topics `"syntax"`, `"keys"` and `"ai"` cover
the language at a glance, the REPL keys and `prompt`'s settings.

**Key bindings.** `keybind(_): "chord" "line"` makes a key run a line of
USSR at the prompt: pressing it replaces whatever was typed with that line
and runs it, as if it had been typed and Enter pressed.

```
keybind(_): "Mod+F1" "help(_):"
keybind(_): "Ctrl+G" "ls(_): \"-l\""
keybind(line): "Alt+F1"        # the bound line, or null
keybind(n):                    # prints every binding; n is how many
keybind(_): "Alt+F1" ""        # unbind
```

A chord is modifiers and one key joined by `+` or `-`, in any case:
`Ctrl` (`Control`, `C`), `Alt` (`Meta`, `Mod`, `M`), `Shift` (`S`), and a
key `a`..`z`, `0`..`9` or `F1`..`F12`. Terminals never see the
Super/Windows key, so `Mod` means Alt. Chords a terminal cannot tell from
editing keys are refused with a reason: a letter without Ctrl or Alt,
Ctrl+H/I/J/M (Backspace, Tab, Enter), Ctrl with a digit, and Shift with a
letter or digit. Put bindings in `moscow.su` to have them in every
session.

Bindings also fire in the REPL of the self-hosted interpreter
(`ussr src/russia/russia.su`), which reads keys itself: on a terminal it
reads each line in raw mode and passes every control key and escape
sequence to `keybind_match(line): bytes`, which returns the bound line or
`null`. A REPL or editor written in USSR can do the same. On Windows,
`terminal_getch` delivers F-keys and Alt chords as the bytes an xterm
sends (`ESC [1;3P` for Alt+F1, `ESC x` for Alt+x), as on Linux.

**Aliases.** `alias(_): "name" "command" "word" ...` gives a command a
short name: afterwards `name(r): x` runs `command(r): "word" ... x`, the
alias's words in front of the call's own arguments. The command may be a
program, a builtin or a function, and may be another alias.

```
alias(_): "ll" "ls" "-l"       # ll(_): "/tmp"  runs  ls(_): "-l" "/tmp"
alias(_): "say" "print"
alias(_): "ls" "ls" "-F"       # an alias may use its own name once
alias(words): "ll"             # its words as a vector, or null
alias(n):                      # prints every alias; n is how many
alias(_): "ll" ""              # remove it
```

As in an interactive shell, aliases apply only to lines typed at the REPL
(the ussr REPL and russia.su's). Scripts, libraries, and code a typed line
loads or `eval`s never see them, so a personal alias cannot change how a
program behaves; that makes the home `moscow.su` the place for them. Not
rewritten: a command with an explicit path (`./ll(_):`), a command whose
last argument is a block (a function definition or control flow), and a
name already rewritten on the way. Names are letters, digits and `_`; the
control words (`if`, `while`, `return`, ...) and `alias` cannot be
aliases.

**Asking an AI.** `prompt(answer): "To be or not to be?"` sends the question
(several arguments are joined with spaces) to a chat model and stores the
text of its reply; `prompt(_): "..."` prints the reply instead. The request
goes over HTTPS through ryazan, ussr's TLS library (Schannel on Windows,
OpenSSL elsewhere; see `src/ryazan/README.md`), which checks the provider's
certificate. Setting `USSR_AI_CURL`, a URL other than `http://`/`https://`,
or a build without TLS sends it through the `curl` program instead; the API
key then reaches curl on its standard input, never on its command line.
Everything is set with environment variables, which `setenv` changes for
the next call:

| Variable | Meaning |
|----------|---------|
| `USSR_AI_PROVIDER` | `anthropic` (default) or `openai`; `openai` also means any OpenAI-compatible server (Ollama, LM Studio, OpenRouter, vLLM, llama.cpp) |
| `USSR_AI_URL` | endpoint URL; defaults to `https://api.anthropic.com/v1/messages` or `https://api.openai.com/v1/chat/completions` |
| `USSR_AI_MODEL` | model name; `anthropic` defaults to `claude-sonnet-5-5`, `openai` needs one |
| `USSR_AI_KEY` | API key, else `ANTHROPIC_API_KEY` / `OPENAI_API_KEY`; may be unset for a local server |
| `USSR_AI_SYSTEM` | optional system prompt |
| `USSR_AI_MAX_TOKENS` | reply length limit (`anthropic` default 1024) |
| `USSR_AI_TIMEOUT` | seconds to wait, default 120 |
| `USSR_AI_CURL` | use the curl program at this path instead of ryazan |

```
# a local model through Ollama
setenv(_): "USSR_AI_PROVIDER" "openai"
setenv(_): "USSR_AI_URL" "http://localhost:11434/v1/chat/completions"
setenv(_): "USSR_AI_MODEL" "llama3.2"
prompt(_): "Name three Soviet cities"
```

A provider's error (bad key, unknown model) stops the command with that
provider's message. When curl is used on Windows, it reads its configuration
through a temporary file that is deleted when the call returns.

**As a script file:**

```
./ussr myscript.su
```

On Windows, the same binary and the same `.su` scripts work
unmodified: `ussr.exe myscript.su`.

**Exit status.** When a script has `init(ret): arg_cnt arg_vec [ ... ]`,
the integer it leaves in `ret` is the process exit status: `0` to `255`
as is, any other integer `1`. A `ret` that is never set (or is not an
integer) gives `0`. A compile error or a runtime error gives `1`. The same
holds for `.ru` files and `-e`. `moscow.su` (the startup file) stops
`ussr` with its status when that is not `0`; the REPL ignores the
statuses of the lines it runs.

**Startup files.** Before a script, `-e` code or the REPL, `ussr` runs
`moscow.su` from your home folder (`HOME`, else `USERPROFILE` on Windows)
for settings you want everywhere (aliases, key bindings, `PS1`), and then
`moscow.su` from the current folder for the project at hand, unless the two
are the same file. Either may be missing. `USSR_MOSCOW` overrides both: it
names the only startup file to run, and set but empty it runs none
(`USSR_MOSCOW= ussr script.su`). `-c` runs no startup file.

```
init(ret): arg_cnt arg_vec [
    set(ret): 3
]
```

exits with status 3 (`echo $?` prints `3`).

`ussr`'s own options (`-v`, `-e`, `-c`) come before the script name;
everything after it, dashes included, is the script's (`ussr
src/kremlin/kremlin.su --window` passes `--window` to Kremlin).

Textual assembler files can also be run directly:

```
./ussr program.ua
```

The previous `.uuasm` spelling is still accepted.

**Compiling to bytecode (`-c`):**

```
./ussr -c myscript.su              # writes myscript.ru
./ussr -c myscript.su out/app.ru   # explicit output file
./ussr myscript.ru arg1 arg2       # runs it like the source
```

`-c` (long form `--compile`) preprocesses and compiles the script without
running it and without loading `moscow.su`. The output file holds only the
binary program; nothing is printed on success. If the source does not
compile, or the output cannot be written, a message goes to stderr, the
exit status is 1, and no output file is left behind. Includes are resolved
at compile time, so a `.ru` file does not need the `.su` files it was built
from; `load(p): "file.su"` and `eval` still read source at run time.

A file whose name ends in `.ru` is checked before anything runs: signature,
format version, VM ABI version, checksum, and every section's bounds.
Truncated, corrupted, or incompatible files are rejected with a message and
exit status 1. A `.ru` file runs with the same arguments, standard input,
output, and exit status as the source it came from.

Blocks of `if`, `while`, `for` and function definitions are compiled to
bytecode. A `[ ... ]` block passed as an argument to a command (the two
blocks of `@` command chaining) is run by the interpreter, so `-c` stores
it as syntax; it behaves the same from the `.ru` file.

---

## 2. The core idea: everything is a command

```
name(return_variable): argument argument ...
```

- `name` is the command being invoked.
- `return_variable` is where its result gets stored — a plain
  variable name. If you don't care about the result, the convention
  is to use `_`.
- Commands that take inputs list them after the colon. Some builtins
  take no inputs, for example `random64(r):` and `time(now):`; commands
  that require a placeholder (like creating an untyped vector) use an
  explicit empty string: `vec(v): ""`.
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
| Integer | `42`, `-7`, `0xCAFEBABE`          | No decimal point. 64-bit signed on every platform: `-9223372036854775808` to `9223372036854775807` (§5). A literal outside that range is a compile error. Hexadecimal: `0x` or `0X`, then up to 16 hex digits in either case (see below). |
| Real    | `3.0`, `-1.5`                     | **Must** include a decimal point. `3` and `3.0` are different types — this matters once a value goes through UNO encode/decode (§8), where a real always round-trips with its `.0`, even when whole. |
| Boolean | `true`, `false`                  | |
| Null    | `null`                            | Every struct field and vector slot accepts `null` regardless of its declared type (§8). |

**Hexadecimal integers.** `0xCAFEBABE` is 3405691582 and `0xDEADBEEF` is
3735928559: a hex literal is an ordinary integer, written in base 16, and
works anywhere a decimal one does, in arguments and inside `{...}`:

```
set(color): 0xB3261E                 # a 0xRRGGBB color
set(low): {0xCAFEBABE & 0xFFFF}      # 47806 (0xBABE)
set(hi): {(0xDEADBEEF >> 16) = 0xDEAD}  # true
```

Up to 16 digits count (leading zeros do not), and they are read as the
64-bit two's complement pattern: `0x7FFFFFFFFFFFFFFF` is the largest
integer, `0x8000000000000000` the smallest and `0xFFFFFFFFFFFFFFFF` is
`-1`. More than 16 digits is a compile error. Numbers always print in
decimal.

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

Supported operators: `+ - * / %` (arithmetic), `> < >= <= = !=`
(comparison), `&& ||` (logical), `<< >> ^ & |` (bitwise
shift/xor/and/or). Both sides of a logical expression are evaluated;
do not rely on short-circuit evaluation.

Integer arithmetic is 64-bit on every platform. `+ - *` on two
integers give an integer and wrap around on overflow (two's complement:
`9223372036854775807 + 1` is `-9223372036854775808`); `/` always gives a
real; `%` needs two integers, and `x % -1` is `0`. Comparisons and `=`
between integers are exact, also beyond the 2^53 where a real stops
holding every integer. `<<` shifts the bits (`1 << 63` is the smallest
integer), `>>` keeps the sign; a shift count outside `0..63` stops the
program with an error.

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
    if(_): {i = 5} [
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

**Variables in functions.** Each call of a function has its own
*local* variables, created by the call and dropped when it returns, so
recursion and helper functions cannot overwrite each other's
temporaries. A function's local variables are:

- its parameters and its return variable;
- every variable a command in its body assigns — the name in a command's
  parentheses (`set(i): 0`, `add(n): n 1`, `len(k): v`, a function call's
  result, …) and the destinations of `get` and `scan`.

A name the function only reads is a *global* variable, so constants and
configuration need no declaration. Top-level code (outside any function)
always uses the globals. In a program with `init`, though, top-level
commands do not run — `init` is the entry point — so there the globals
are whatever functions set under a `global` declaration.

To assign a global from inside a function, declare it with `global`:

```
global(_): count total    # at the top level: global in every function

bump(_): n [
    add(count): count 1   # updates the global count
    add(total): total n
]

scratch(r): v [
    global(_): last       # inside a function: global in this function only
    len(r): v
    set(last): r
]
```

`global(_): names…` at the top level is the place to declare a program's
shared state once; inside a function it applies to that function only,
wherever in the body it is written. Parameters and the return variable
are always local. `init` is an ordinary function: state it sets up for
the rest of the program must be declared global.

Other rules:

- `!`/`?` hash variables (§13) are unaffected: one namespace shared by
  every process.
- `eval` code run inside a function uses that function's variables: a
  name local to the function is its local there, anything else a global.
- A process started inside a function (`async`, GUI callbacks) starts
  with a copy of the globals and of the starting function's locals, as
  its own globals, as it did before locals existed.
- Compatibility: `global(_): "*"` makes every variable global except the
  parameters and the return variable — the behavior of USSR before
  locals — at the top level for the whole program, inside a function for
  that function.

The compiler warns (`file:line: warning: …`, the program still runs)
about the two mistakes this change makes likely in older code: reading a
local variable before the function sets it, and reading a global that
only ever gets set as some function's local:

```
demo.su:9: warning: 'state' is read before it is set in 'show'; it is local to 'show' (declare global(_): state if it is shared)
demo.su:14: warning: 'mode' is read as a global but only set as a local (in 'init', line 3); declare global(_): mode
```

**`elif` / `else` chains.** `elif` and `else` are ordinary commands
placed directly after an `if` (or another `elif`) command; the `if`
takes its condition and then-block as two arguments, and the chain
continues through any number of `elif` commands to an optional `else`:

```
if(_): {a = 9} [
    print(_): "nine"
]
elif(_): {a > 3} [
    print(_): "A is greater than 3 but not 9"
]
else(_): [
    print(_): "small"
]
```

Like everything else in USSR these are commands, not grammar: the
parser sees a sequence of commands, and the compiler links an
`if`/`elif`/`else` run into one conditional chain.

---

## 8. Structs, vectors, and UNO

USSR has a small, deliberately minimal object layer: structs (records
with typed fields), a growable vector container, and UNO (Unified
Object Notation) — a text format for moving a struct or vector through
a string, a file, a pipe, or straight into source code.

### 8.1 Declaring a struct

```
struct(Point): "x:number" "y:number"
```

The type name goes in the return-variable slot; the arguments are
`"field:type"` specs. `type` is one of `string`, `number`, `boolean`,
`null`, `vector`, `any`, or the name of another struct. Every field
accepts `null` regardless of its declared type. Declaring a type twice is
an error.

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
pop(last): items         # "two"; items now holds only 1
```

- `vec(var): "ElementType"` — pass `""` for an untyped vector.
- `push(_): vector value` — append; type-checked if the vector is
  typed.
- `at(var): vector index` — zero-based read.
- `pop(var): vector` — remove the last item and return it; popping an
  empty vector is a runtime error.
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

### 8.5 Math vectors: Vec2 and Vec3

Two struct types are built in: `Vec2{x, y}` and `Vec3{x, y, z}`, with
`number` fields. Everything that works on structs works on them, without
declaring them:

```
new(a): "Vec2" 1 2
set(p): 'Vec3{x=1,y=2,z=3}'
getf(x): a "x"
typeof(t): p            # "Vec3"
vec(path): "Vec2"       # a container that only holds Vec2 values
```

Inside `{ }` the VM gives them value semantics:

| Expression | Result |
|------------|--------|
| `{a + b}`, `{a - b}` | Component-wise, both operands the same type |
| `{v * k}`, `{k * v}`, `{v / k}` | Scaling by a number; dividing by zero is an error |
| `{a = b}`, `{a != b}` | Component-wise comparison (`1` equals `1.0`); a `Vec2` never equals a `Vec3` or a non-vector |

Components keep USSR number semantics (integer stays integer under `+ -
*`; `/` gives reals). Every other combination stops the script with a
message naming both operands, for example `cannot add Vec2 and Vec3`,
`cannot add Vec2 and integer`, `cannot multiply Vec2 by Vec2 (...use
vdot/vcross...)`, or `this operator is not defined for Vec2` for `<`, `%`
and the bitwise operators. A null component reports
`Vec2 component x is not a number`. Like other structs, vectors are
shared references: `set(b): a` aliases, while operators return new
vectors.

`src/libs/vectors.su` adds functions written in USSR:

| Function | Signature | Behavior |
|----------|-----------|----------|
| `vec2`, `vec3` | `vec2(v): x y` | Construct; non-numbers are an error |
| `vdim` | `vdim(n): v` | 2 or 3; anything else is an error |
| `vget`, `vset` | `vget(c): v i`, `vset(v): v i value` | Component by index 0..dim-1; other indexes are an error |
| `vadd`, `vsub`, `vscale`, `vneg` | `vadd(v): a b` | Same as the operators, with dimension checks |
| `vdot` | `vdot(n): a b` | Dot product |
| `vcross` | `vcross(c): a b` | Vec3 cross product; for two Vec2 the scalar `a.x*b.y - a.y*b.x` |
| `vlen`, `vdist`, `vnorm` | `vlen(n): v` | Length, distance, unit vector (zero vector is an error) |
| `vlerp` | `vlerp(v): a b t` | `a + (b - a) * t` |
| `vnear` | `vnear(ok): a b eps` | Every component within `eps` |
| `vstr` | `vstr(s): v` | `"(1, 2)"`, up to six decimals |

Declaring `struct(Vec2): "x:number" "y:number"` yourself is accepted (it
matches the built-in type); a different field list is the usual
`already defined` error.

### 8.6 What UNO can't represent

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
$elif defined TRACE
print(_): "tracing"
$else
print(_): "quiet"
$endif
```

- `$define NAME value` — simple text substitution.
- `$define NAME(params) replacement` — parameterized macro.
- `$include "file.su"` (or a bareword path) — inlines another file's
  content at that point, resolved relative to the *including* file's
  own directory when given a relative path.
- `$ifdef NAME` / `$if defined(NAME)` / `$elif defined(NAME)` /
  `$else` / `$endif` — conditional inclusion. The first branch whose
  name is defined is kept and the rest of the chain is skipped; `$else`
  is kept only when no branch was. `defined NAME` without parentheses
  works too. `$elif` after `$else` is an error.
- `$once` makes the current file a single-inclusion file: later
  `$include`s of the same file, by any relative path, are skipped. The
  standard library, Petersburg, Tver and Tula files use it, so a program
  can include `strings.su` and `vectors.su` (which includes it too) without
  duplicate definitions.
- `__MAIN__` counts as defined only in the file `ussr` was started with,
  never inside an `$include`d file. A library can carry its own entry point
  without clashing with the `init` of programs that include it:

  ```
  $ifdef __MAIN__
  init(ret): arg_cnt arg_vec [
      ...
  ]
  $endif
  ```

`#`-comments are handled by the language's own lexer, not the
preprocessor, so they work identically inside and outside `$define`
bodies.

---

## 12. Errors

**Before anything runs**, the whole program is parsed and compiled. A
mistake stops it there with one message on `stderr`, in the form
`file:line: reason` (the format compilers use, so editors and terminals
can jump to it), and exit status 1:

```
tests/demo.su:3: print expects exactly one value, got 3 (join values with concat first)
src/libs/mylib.su:12: add expects two values, got 1
tests/demo.su:7: syntax error
tests/demo.su:2: unexpected character '`'
```

The line is the one the command starts on in its original file, also
inside `$include`d files and after skipped `$if` blocks. Text that has no
file reports a stand-in name: `<stdin>` with the REPL's input line
number, `<eval>` for `eval` strings, `<command line>` for `-e`. Only the
first problem is reported; fix it and run again. Typical messages: wrong
number of values for a builtin or a function, a value where a variable
name is needed (`get(vector): index destination`, `scan` targets),
`break`/`continue` outside a loop, `return` outside a function, `do`
without `loop`, `else`/`option` without `if`/`choose`, a function defined
twice.

**At run time**, an error (bad argument type, division by zero, an
invalid VM operation, a failed `struct`/`getf`/`setf`/`push`, and so on)
prints a message to `stderr` and halts the entire script immediately.
The process exits non-zero. Run-time messages do not carry a line number
yet. At the REPL an error ends only the entry it happened in; variables
and the session carry on.

`try(failed): "function" arg ...` contains an error: it calls one of the
program's functions, and an error inside that call ends the call instead
of the program. `failed` is `1` then (the message is still printed),
else `0`. The call's result is not kept, and whatever the function did
before the error stays done; pass results back through globals.

```
global(_): saved
save(r): path [
    writefile(r): path "data"
    set(saved): 1
]
init(result): c v [
    set(saved): 0
    try(failed): "save" "/no/such/dir/x.txt"
    if(_): {failed = 1} [ print(_): "could not save" ]
    set(result): 0
]
```

`try` is not available yet in scripts run by the self-hosted
interpreter (russia.su), which uses it itself to keep its REPL going.

---

## 13. The scheduler: processes, async/await, events

USSR has a cooperative multithreading scheduler, modelled on the
FreeBSD kernel's design: a run queue of runnable processes, a timer
event queue, and context switches only at well-defined points. It is
implemented from scratch — no pthreads, no libevent/libev — and it
works together with the existing fork-exec model: spawning an
external command from a scheduled process blocks only that process
(the child becomes an event-queue entry), never the whole
interpreter.

Every script runs as process 1 (its `init`), so the scheduler is
always active; single-threaded scripts behave exactly as before.

### async / await

`async(p): "name" arg...` spawns the definition `name` as a new
scheduled process and stores its process id in `p`. The child starts
with a copy of the caller's variables (fork semantics). The name may
be a string or a bare identifier.

`await(r): p` blocks the current process until process `p` finishes
and stores its return value in `r`. Awaiting an already-finished
process returns immediately.

```
worker(r): n delay [
    sleep(_): delay
    set(r): n
]

init(ret): arg_cnt arg_vec [
    async(p1): "worker" 1 50
    async(p2): worker 2 10
    await(r2): p2      # short sleeper finishes first
    print(_): r2       # 2
    await(r1): p1
    print(_): r1       # 1
    set(ret): 0
]
```

### sleep / yield

`sleep(_): ms` suspends the current process onto the timer event
queue for `ms` milliseconds; other processes run meanwhile.
`yield(_): 0` voluntarily ends the current time slice and moves the
process to the back of the run queue. Scheduling is cooperative with
a quantum: a long-running process is also suspended after its
instruction budget is spent, so CPU-bound loops can't starve others.

### process / load: the init hierarchy

There are three levels of entry points:

1. **outer commands** typed into the REPL;
2. **global init** — the script's own `init` without a process mark;
3. **local init** — a definition marked as a separate scheduled
   entity, invoked through `load` from a level above.

`process(_): name` in a file marks the definition `name` as that
file's scheduled entry point. `load(p): "file.su" ["entry"]` reads,
parses and compiles the file as a new scheduled process with a fresh
scope (exec semantics — its top-level code does not run) and stores
the new pid in `p`. The entry is the explicit second argument when
given, otherwise the first `process(_)` mark in the file, otherwise a
definition called `init`. An entry taking `(arg_cnt, arg_vec)`
receives `0` and an empty vector.

```
# worker.su
process(_): init
init(ret): arg_cnt arg_vec [
    set(ret): 7
]

# main.su
init(ret): arg_cnt arg_vec [
    load(p): "worker.su"
    await(r): p
    print(_): r        # 7
    set(ret): 0
]
```

### gc

`gc(_): 0` asks the garbage collector to reclaim unreachable values.
The collector is incremental and also runs on its own as allocation
pressure grows; the command exists for moments where a script knows
it has just dropped a large structure.

### Notes and limits

- Scheduler commands (`async`, `await`, `sleep`, `yield`, `process`,
  `load`, `gc`) are bytecode-VM features. Inside `@[]` advanced
  control blocks (the tree-walking interpreter) there is no resumable
  VM state, so they are rejected with a clear error there.
- The `russia.su` bootstrap implements the same scheduler in pure USSR
  inside its own bytecode VM. There time is a *logical* tick (one VM
  slice is one tick, idle time jumps straight to the next timer), and
  a loaded file shares the main program's function table, so a
  definition in a loaded file that reuses a main-file name is
  shadowed by the main-file version (the entry point itself is
  resolved correctly among the loaded definitions).
- `!`/`?` hash sigils behave the same under `russia.su`: one hash
  shared by every process there too.

---

## 14. Standard library

The standard library sources in this repository live in `src/libs/`
(`io.su`, `bytes.su`, `math.su`, `standard.su`); each module's header
comment describes its purpose. Include the needed source file with
`$include`, using a path relative to the script that contains the
directive. For example, from a script in the repository root:

```
$include "src/libs/math.su"
```

The file and byte primitives are real host operations, independent of the
whole-file `readfile`/`writefile` VM builtins.

**io.su — stream files with C-style handles:**

```
open_file(h): "path" "mode"   # h >= 0 is a handle, -1 on error
close_file(ok): h
write_file(n): h "text"       # n = bytes written
read_file(s): h [n]           # rest of file, or n bytes
```

`mode` is a C `fopen` mode: `"r"` `"w"` `"a"`, the binary forms
`"rb"` `"wb"` `"ab"`, and the `"+"` update variants.

```
open_file(h): "log.txt" "a"
write_file(_): h "one more line\n"
close_file(_): h
```

`write_stderr(n): "text"` writes text to standard error exactly as given (no
prefix, no added newline) and returns the bytes written.

`read_line(s): h` returns the next line without its LF or CRLF, or `null`
at end of file. Prefer it to scanning a whole file byte by byte: reading a
variable copies its string, so per-byte loops over large strings are slow.

**io.su — directories and paths.** These report failure through their
result instead of stopping the script:

```
list_dir(v): "path"        # sorted names, directories end in "/"; null if unreadable
is_dir(b): "path"          # 1 or 0
getcwd(s):                 # current working directory
make_dir(ok): "path"       # 0 ok, 1 failed or already exists
rename_path(ok): from to   # 0 ok, 1 failed; never replaces an existing destination
copy_file(ok): from to     # 0 ok, 1 failed; copies one file, never replaces a destination
remove_file(ok): "path"    # 0 ok, 1 failed; refuses directories
remove_dir(ok): "path"     # 0 ok, 1 failed; empty directories only
```

`list_dir` omits `.` and `..` and sorts without regard to ASCII case.

**strings.su — text and number conversion (pure USSR):**

| Function | Signature | Behavior |
|----------|-----------|----------|
| `str_from_int` | `str_from_int(s): n` | Decimal text of an integer. |
| `str_trunc` | `str_trunc(i): x` | Integer part, toward zero. `/` always yields a real, so `str_trunc(q): {a / b}` is integer division for exact quotients. |
| `str_from_number` | `str_from_number(s): x digits` | Rounds to `digits` decimals and drops trailing zeros; whole values print as integers. |
| `str_to_number` | `str_to_number(n): s` | Integer or real from decimal text (sign, fraction, exponent; blanks ignored); `null` if `s` is not a number. |
| `str_lower` | `str_lower(s): text` | ASCII lower case. |
| `str_find` | `str_find(i): s needle from` | First index at or after `from`, or `-1`. |
| `str_trim` | `str_trim(s): text` | Strips spaces, tabs, CR and LF at both ends. |
| `str_repeat` | `str_repeat(s): text count` | Repeats `text`. |

`concat` accepts only strings, so convert numbers with `str_from_int` or
`str_from_number` before joining them into text.

`strings.su` also steps through UTF-8 text by character (text stays a byte
string; `len`, `byte` and `slice` count bytes):

| Function | Signature | Behavior |
|----------|-----------|----------|
| `utf8_count` | `utf8_count(n): s` | Number of characters. |
| `utf8_next` / `utf8_prev` | `utf8_next(i): s i` | Byte index of the next / previous character start. |
| `utf8_index` | `utf8_index(i): s column` | Byte index of character number `column` (or `len(s)`). |
| `utf8_column` | `utf8_column(c): s i` | Characters before byte index `i`. |
| `utf8_len` | `utf8_len(k): lead_byte` | Length of the sequence a lead byte starts. |
| `utf8_from_bytes` | `utf8_from_bytes(s): v` | String from a vector of byte values. |

Malformed bytes count as one character each. On every platform
`terminal_getch` delivers keyboard input as UTF-8 bytes (Windows console
input is converted), and `terminal_raw` switches the Windows console to
UTF-8 output.

**Environment variables (native):**

```
getenv(v): "HOME"            # value; "" when set to empty; null when not set
setenv(ok): "MODE" "fast"    # create or overwrite; 0 ok, 1 refused
unsetenv(ok): "MODE"         # remove; 0 ok (also when it was not set), 1 refused
```

A name must be non-empty and must not contain `=`; otherwise `setenv` and
`unsetenv` return `1` and `getenv` returns `null`. Values are stored byte
for byte (spaces, `=` and UTF-8 text included). Passing a non-string is a
runtime error. Changes affect the `ussr` process and the external commands
it starts afterwards, never the shell that started `ussr`.

Platform notes: on Windows names are case-insensitive (`Path` and `PATH` are
one variable), values are converted between UTF-8 and the Win32 Unicode
environment, and empty values are kept rather than deleting the variable.
On POSIX systems names are case-sensitive and the C library's `setenv`,
`unsetenv` and `getenv` are used.

**Starting programs and local time (native):**

```
launch(ok): "notes.txt"                  # open with the system's program
launch(ok): "https://example.org"        # a URL opens in the browser
launch(ok): "notepad" "notes.txt"        # a program and its arguments
launch(ok): "ussr" "klin.su" true        # no console window (Windows)
localtime(t):                            # now: [2026,10,27,9,5,0,2,300]
localtime(t): 0                          # a time(s): value
```

Unlike an external command (§9), `launch` does not wait: the program keeps
running on its own, and `ok` is `0` when it started, `1` when it could not
(no such program, nothing to open the file with). The first argument is a
program (by name on `PATH`, or a path to it) or a file, folder or URL, which
opens with the program the system has for it: `ShellExecute` on Windows,
`xdg-open` on Linux, `open` on macOS. The optional second argument is the
argument string (on POSIX split at blanks, `"double quotes"` grouping
words); the third, when true, starts a program without a console window on
Windows (a GUI script run by `ussr`), and on POSIX sends its output to
`/dev/null`.

`localtime` gives the local date and time as
`[year month day hour minute second weekday yearday]`: month 1-12, weekday
0 (Sunday) to 6, yearday 1-366.

**Runtime helpers (native):**

```
typeof(t): value     # "null" "integer" "real" "string" "boolean" "vector",
                     # or a struct instance's type name
fail(_): "message"   # print "USSR: message" and stop like any runtime error
chr(s): byte         # one-byte string for 1..255
finv_sqrt(r): x      # approximate 1/sqrt(x), Quake III style, for x > 0
```

`finv_sqrt` converts `x` to a 32-bit float, applies the classic bit trick
and one Newton step, so results are within about 0.2% of `1/sqrt(x)`
(`finv_sqrt(r): 4` gives `0.499154`). Zero, negative or non-finite input is
a runtime error.

Integers are 64-bit on every platform (§5). `str_int_limit(m):` in
`strings.su` gives `2^62`, the largest value `str_trunc` accepts.

**bytes.su — memset analogs over vectors, and binary files:**

```
set_bytes(v): value bytes     # every item of bytes := value (0..255)
clear_bytes(v): bytes         # set_bytes with value 0
read_bytes(v): "path"         # the file's bytes 0..255 (null if unreadable)
write_bytes(n): "path" bytes  # write exactly these bytes; count, or -1
text_bytes(v): "text"         # the string's bytes 0..255
```

**math.su — mathematical functions:**

All functions below are USSR functions defined in `src/libs/math.su`,
not C builtins. Trigonometric inputs and outputs are in radians unless
explicitly stated otherwise. Include the file before calling them.

| Function | Signature | Behavior |
|----------|-----------|----------|
| `sqrt` | `sqrt(r): x` | Square root by Newton iteration; negative input prints an error and returns `-1`. |
| `sin` | `sin(r): x` | Range-reduced polynomial sine, radians. |
| `cos` | `cos(r): x` | Sine shifted by pi/2, radians. |
| `tan` | `tan(r): x` | `sin(x) / cos(x)`, radians. |
| `ctg` | `ctg(r): x` | Cotangent, `cos(x) / sin(x)`, radians. |
| `ln` | `ln(r): x` | Natural logarithm for positive input; returns `0` for `x <= 0`. |
| `exp` | `exp(r): x` | Exponential, `e^x`, using range reduction and a polynomial. |
| `pow` | `pow(r): x y` | `x^y`, implemented as `exp(y * ln(x))`; when `x <= 0`, `ln` returns `0`, so this does not implement general powers for those bases. |
| `floor` | `floor(r): x` | Greatest integer not greater than `x`. |
| `ceil` | `ceil(r): x` | Least integer not less than `x`. |
| `abs` | `abs(r): x` | Absolute value. |
| `atan` | `atan(r): x` | Inverse tangent in radians. |
| `asin` | `asin(r): x` | Inverse sine for `-1 <= x <= 1`; out-of-domain input prints an error and returns `0`. |
| `acos` | `acos(r): x` | Inverse cosine for `-1 <= x <= 1`, in radians; uses `asin`. |
| `atan2` | `atan2(r): y x` | Angle of point `(x,y)` in `(-pi, pi]`; `(0,0)` returns `0`. |
| `grcirc_dist` | `grcirc_dist(r): lat1 long1 lat2 long2` | Great-circle distance between latitude/longitude pairs in degrees; result is kilometers using Earth's mean radius. |
| `log2` | `log2(r): x` | Base-2 logarithm derived from `ln`; positive input is required for a meaningful result. |
| `log10` | `log10(r): x` | Base-10 logarithm derived from `ln`; positive input is required for a meaningful result. |
| `hypot` | `hypot(r): x y` | Euclidean hypotenuse, scaled to reduce intermediate overflow/underflow. |
| `cbrt` | `cbrt(r): x` | Real cube root, including negative inputs, using range reduction and Newton iteration. |

`tan` and `ctg` divide by `cos(x)` and `sin(x)` respectively, so their
poles result in the runtime's division-by-zero error.

`math.su` also defines the preprocessor constants `M_PI` and `M_E`.
For example:

```
$include "src/libs/math.su"

sin(s): {M_PI / 2.0}
print(_): s
```

**standard.su:** `random64(r):`, `seed_random64(_): seed`, and
`time(r):`.

**vectors.su:** 2D/3D vector functions; see section 8.5.

---

## 15. More details

The textual UUASM format and instruction reference are documented in
[`USSR_UUASM_SPEC.md`](USSR_UUASM_SPEC.md). The current C VM instruction
set, runtime behavior, and bytecode status are described in
[`USSR_VM_SPEC.md`](USSR_VM_SPEC.md).

---

## 16. Building and testing USSR

**Linux** (gcc or clang, flex, bison; GLFW and OpenGL development files
through `pkg-config` enable the Petersburg GUI, otherwise it is left out):

```
make -f ussr-ubuntu.mk -j"$(nproc)" ussr
```

**Windows** (Visual Studio 2022 x64 developer prompt, `win_bison` and
`win_flex` from winflexbison3). Leave out `GLFW_DIR`/`GLFW_LIB` for a build
without Petersburg:

```
win_bison -d -o src\parser.tab.c src\parser.y
win_flex -o src\lexer.c src\lexer.l
nmake /f Makefile.msc PREGENERATED=1 GLFW_DIR=C:\glfw-3.5.1.bin.WIN64 GLFW_LIB=C:\glfw-3.5.1.bin.WIN64\lib-vc2022\glfw3dll.lib
```

`glfw3.dll` must be next to `ussr.exe` (or on `PATH`) to run a GLFW build.

**Tests.** Run everything from the repository root:

```
sh tests/run_all.sh                    # ./ussr, or ./ussr.exe on Windows
USSR_SKIP_GUI=1 sh tests/run_all.sh    # without a display or GLFW
xvfb-run -a sh tests/run_all.sh        # GUI tests on a headless Linux box
```

It runs the suites in `tests/ru`, `tests/mbl`, `tests/russia`, `tests/vectors`,
`tests/repl`, `tests/errors` and `tests/exit`; every script in `tests/`, `tests/math` and `tests/oop`,
comparing stdout, stderr and exit status with `tests/expected/<path>.txt`
(stdin comes from `tests/expected/<path>.in` when that file exists); and the
GUI tests in `tests/gui`. Each check prints a `PASS` or `FAIL` line and the
exit status is nonzero if anything failed. After an intended change in
output, `sh tests/run_all.sh --update` rewrites the expectations; review the
diff before committing it. Not run: the socket and HTTP tests (they need a
free port), `tests/chain.su` and `tests/script.su` (their output is the
host's `ps` listing, and chaining is POSIX-only), and `tests/quines`.

GitHub Actions (`.github/workflows/ci.yml`) runs the same script on every
pull request and push to `main`: Ubuntu with GLFW under Xvfb, Ubuntu without
GLFW, and Windows with MSVC and GLFW.
