# USSR: Structs, Vectors, and UNO — Design Doc

Status: proposal
Scope: adds a minimal OOP layer (`struct`, methods, single dispatch), a
`vector` collection type, and **UNO** (Unified Notable Objects) — a
string encoding that lets a USSR object cross the one parameter type
capable of holding arbitrary data: `<string>`.

Design constraint driving everything below: the current grammar's
`<parameter>` production is closed —

```
<parameter> ::= <string> | <number> | <boolean> | <variable> | <null> | <command-list>
```

There's no `<object>` alternative. This spec adds exactly one new
alternative to close that gap:

```
<parameter> ::= <string> | <uno-literal> | <number> | <boolean> | <variable> | <null> | <command-list>
```

`<uno-literal>` is a single quoted (`'...'`) token, recognized by the
lexer, that the parser turns directly into a live struct/vector value
— no runtime `decode()` step needed for anything written in source.
Structs, vectors, and methods themselves (§2–4) stay pure builtins with
no grammar impact; only UNO touches `lexer.l`/`parser.y`, and only by
this one addition. Appendix A lists further, optional sugar
(`p.x`, explicit block params) that nothing here depends on.

---

## 1. Value model

- **Structs** are reference types. A variable holds a handle to a
  heap-allocated instance; assigning it to another variable aliases it
  (mutation is visible through both), matching how you'd want `self`
  to work for methods.
- **Vectors** are also reference types (growable, ordered, like a JS
  array/Python list) — object-processing pipelines want shared mutation
  and cheap passing, not copy-on-write list semantics.
- Everything else (`string`, `number`, `boolean`, `null`) keeps normal
  value semantics, unchanged.

---

## 2. `struct`: declaring a type

```
struct(Point): "x:number" "y:number"
struct(Shape): "kind:string" "area:number"
struct(Anything): "label:string" "payload:any"
```

- Each field parameter is a **string** of the form `"name:type"`,
  fitting the existing `<string>` parameter — no grammar change.
- `type` is one of: `string`, `number`, `boolean`, `null`, `vector`,
  `<StructName>`, or `any` (unchecked).
- `struct(Name): ...` registers `Name` in a type table **and** installs
  `Name` itself as a callable command — the constructor. This reuses
  `<command-name>` exactly as-is; a struct name is just a command that
  the runtime defines dynamically instead of one compiled into
  `ussr.c`.

### Constructing an instance

```
Point(p): 1.0 2.0
```

Positional args bind to fields in declaration order. Wrong arity or a
type mismatch (e.g. passing a string where `number` was declared) is a
runtime error — this is where the lightweight type tags earn their
keep, especially once instances start arriving over UNO from outside
the process (see §5).

### Field access

```
get(v):  p "x"          # v = 1.0
set(_):  p "x" 9.5       # mutates p in place
```

`get`/`set` are the v1 field-access primitives: object handle + field
name string (+ value for `set`). This is verbose on purpose — it needs
no new syntax. §Appendix A shows the `p.x` sugar once the grammar can
support it.

---

## 3. Methods (single dispatch)

Methods are blocks (`<command-list>`) registered against a struct type
and a name:

```
method(_): "Shape" "describe" [
    get(k): self "kind"
    get(a): self "area"
    concat(msg): k ": area=" a
    return(_): msg
]
```

- `method(_): TypeName MethodName Block` — registers `Block` in a
  method table keyed by `(TypeName, MethodName)`.
- Inside the block, `self` is an **implicit variable**, bound to the
  receiver for the duration of the call and restored afterward (shadow
  semantics identical to how loop variables work in §4). No grammar
  change needed: the block is just a normal `<command-list>`, and the
  binding is something the `invoke` builtin does before running it.
- Extra call arguments (if any) are bound the same way as `arg1`,
  `arg2`, ... (or packed into an implicit `args` vector — pick one
  convention and keep it consistent with how blocks receive loop items
  in §4, since both are "implicit parameter injection into a block").
- `return(_): value` sets the block's result. Without it, a block's
  result defaults to `null`. (This is general block-return machinery,
  useful beyond OOP — worth building once, used everywhere blocks are
  invoked as functions.)

### Invoking

```
invoke(msg): shape_obj "describe"
invoke(x):   shape_obj "scaleBy" 2.0
```

`invoke` looks up `typeof(shape_obj)` in the method table. If you later
want inheritance, extend `struct` with an optional parent:

```
struct(Circle): "extends:Shape" "radius:number"
```

and have `invoke` walk the type's parent chain on a miss — simple
single-inheritance, no interfaces/traits needed for v1.

---

## 4. `vector`: arrays of objects

```
vec(shapes): "Shape"      # empty, element-typed vector
vec(bag):    ""           # empty, untyped ("any") vector
```

Passing `"Shape"` (a string, fitting `<parameter>` as-is) declares the
element type; `push` checks it.

```
push(_): shapes circle1
push(_): shapes circle2
len(n):  shapes            # n = 2
at(c):   shapes 0          # c = circle1
```

### Higher-order ops without new grammar

Blocks passed to `each`/`map`/`filter` get the current element bound to
an implicit `it` (and index to `idx`), the same shadow-and-restore
pattern used for `self` in methods — one mechanism, two use sites:

```
each(_): shapes [
    invoke(desc): it "describe"
    print(_): desc
]

map(areas): shapes [
    invoke(a): it "area"
    return(_): a
]

filter(big): shapes [
    invoke(a): it "area"
    gt(b): a 10.0
    return(_): b
]
```

This is the main place where "no grammar change" is felt as a real
cost — see Appendix A's `[|it, idx| ...]` sugar if implicit `it`
starts colliding with variables named `it` in practice.

---

## 5. UNO — Unified Object Notation

**Purpose:** give struct instances and vectors a textual form that
works two ways at once — a literal you can write directly in source,
*and* a wire format for carrying objects across the boundaries a shell
language actually cares about: files, pipes, env vars, other
processes. One grammar, two entry points (§5.4).

### 5.1 Three quote levels, none colliding

UNO gets its own quote character distinct from both USSR's string
quote and its own literal-boundary quote, so nothing at any level ever
needs escaping relative to its container:

| Quote | Meaning |
|---|---|
| `"..."` | USSR `<string>` (unchanged) |
| `'...'` | USSR `<uno-literal>` — a UNO document, recognized by the parser |
| `` `...` `` | a *string value inside* a UNO document |

```
Shape{kind=`circle`,area=12.56,tags=[`red`,`big`]}
```

### 5.2 Grammar

```
<uno-literal> ::= "'" <uno-value> "'"

<uno-value>   ::= <uno-object> | <uno-vector> | <uno-string>
                | <number> | <boolean> | <null>

<uno-object>  ::= <identifier> "{" <uno-field-list>? "}"
<uno-field-list> ::= <uno-field> | <uno-field> "," <uno-field-list>
<uno-field>   ::= <identifier> "=" <uno-value>

<uno-vector>  ::= "[" <uno-value-list>? "]"
<uno-value-list> ::= <uno-value> | <uno-value> "," <uno-value-list>

<uno-string>  ::= "`" <uno-string-char>* "`"
<uno-string-char> ::= <any-character-except-backtick-or-newline>
```

`<number>`, `<boolean>`, `<null>`, `<identifier>` reuse the definitions
already in `grammar.bnf` verbatim. Note `<uno-literal>` itself is *not*
part of `<uno-value>` — UNO documents don't nest a `'...'` inside
another `'...'`; an object field that needs to hold another object
just nests `<uno-object>` directly, no extra quoting layer:

```
'Line{start=Point{x=0,y=0},end=Point{x=3,y=4}}'
```

### 5.3 Lexer/parser sketch

`lexer.l` gains an exclusive start condition for the `'...'` span,
mirroring how the existing double-quoted `<string>` rule presumably
works, but recursively tokenizing UNO's internal grammar instead of
scanning to a matching close quote:

```c
%x UNOLIT

"'"                 { BEGIN(UNOLIT); return UNO_QUOTE_OPEN; }
<UNOLIT>"'"          { BEGIN(INITIAL); return UNO_QUOTE_CLOSE; }
<UNOLIT>[A-Za-z_][A-Za-z0-9_]*  { return UNO_IDENT; }
<UNOLIT>"{"|"}"|"["|"]"|"="|","  { return yytext[0]; }
<UNOLIT>"`"[^`\n]*"`" { return UNO_STRING; }
<UNOLIT>[0-9]+(\.[0-9]+)?        { return UNO_NUMBER; }
<UNOLIT>"true"|"false"           { return UNO_BOOL; }
<UNOLIT>"null"                   { return UNO_NULL; }
```

`parser.y` adds `uno_literal` as a new alternative under `parameter`,
with productions matching §5.2 directly — `uno_object`,
`uno_field_list`, `uno_vector`, etc. — building the same runtime value
representation that `struct`/`vec` builtins already produce, so the
rest of the interpreter (methods, `invoke`, type-checking) doesn't
care whether an object arrived via a literal or via `Point(p): ...`.

### 5.4 Two entry points, one format

- **Source literal** — `'...'` is parsed straight to a live
  struct/vector value at parse time:
  ```
  set(p): 'Point{x=1,y=2}'
  ```
  No `decode()` call; type-checking against the struct registry (§2)
  happens as part of parsing/binding the literal, same as construction
  via `Point(p): 1.0 2.0` would.

- **External text** — anything arriving as plain bytes (a pipe, a
  file, an env var, another process's stdout) is just a string until
  something looks at it. `decode(obj): s` parses that string using the
  *identical* `<uno-value>` grammar (§5.2, sans the `'...'` wrapper —
  the wrapper is a source-syntax marker, not part of the wire format)
  and type-checks it the same way. `encode(s): obj` is the inverse,
  and always emits bare UNO text with no wrapping quote, so its output
  is equally valid dropped into a file, piped to another `.su` script,
  or pasted back into source between `'`s.

### 5.5 Example

```
Point(p): 1.0 2.0
encode(s): p
# s = "Point{x=1,y=2}"          (bare text — a real <string>)

decode(p2): s
# p2 is a new Point instance, type-checked against struct Point

set(p3): 'Point{x=1,y=2}'
# p3 built directly from a source literal, no decode() needed

vec(pts): "Point"
push(_): pts p
push(_): pts p2
encode(v): pts
# v = "[Point{x=1,y=2},Point{x=1,y=2}]"
```

### 5.6 `encode` / `decode` semantics

- `encode(s): obj` — works on any struct instance or vector; recurses
  into nested structs/vectors/strings automatically. Numbers/booleans/
  null print as their literal forms.
- `decode(obj): s` — parses `s` against the `<uno-value>` grammar,
  looks up the named struct type(s) in the registry, and **type-checks
  every field against the declared struct** (§2). This matters because
  `decode` is specifically the entry point for data that crossed a
  boundary (pipe, file, subprocess, another script) — treat it as
  untrusted and fail closed on a mismatched field type or unknown
  struct name, rather than silently coercing. Source literals (§5.4)
  get the same checking, just at parse time instead of at a `decode`
  call.
- Unknown struct name → runtime error (don't guess a shape for a type
  you've never registered) — applies equally to `decode` and to a bare
  `'...'` literal.
- `any`-typed fields accept whatever UNO value was present (object,
  vector, string, number, boolean, null) with no further checking —
  the deliberate escape hatch for loosely-typed data.

---

## Appendix A — optional further grammar deltas

Neither of these is needed to build the spec above; each is a small,
additive change to `grammar.bnf` for later, once you've lived with the
implicit-variable conventions long enough to know if they bother you:

1. **Dot field access**: `p.x` as sugar for `get(_): p "x"`, needs a
   lexer rule allowing `.` inside a restricted post-identifier position
   (careful: don't let it collide with `<real>`'s `.` in numbers).

2. **Block parameters**: `[|it, idx| ...]` so `each`/`map`/`filter`
   (and `method`'s implicit `self`) bind named parameters explicitly
   instead of relying on a magic implicit variable name. This is the
   one most likely to matter once real programs start nesting HOF
   calls or methods-calling-methods, where implicit `self`/`it` from
   an outer scope could otherwise be shadowed unexpectedly by an inner
   one — worth a defined shadow/restore rule *now*, before this sugar
   exists.
