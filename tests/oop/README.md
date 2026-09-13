# OOP / vector / UNO tests

There's no assertion builtin in USSR yet, so these are eyeball tests:
every `print(_): x` line has a `# expected value` comment after it.
Run each file and diff its actual stdout against the comments.

```
./ussr tests/oop/basic.su
./ussr tests/oop/types.su
./ussr tests/oop/vector.su
./ussr tests/oop/uno.su
```

Each should run to completion and exit 0.

## Error cases

These are separate files because a failing builtin call halts the
whole script (there's no try/catch), so each one only tests a single
failure mode. Each is expected to print an error to stderr, exit
nonzero, and **not** print `"should not reach here"`:

```
./ussr tests/oop/errors_argcount.su          # wrong constructor arg count
./ussr tests/oop/errors_type_mismatch.su     # field type mismatch on new()
./ussr tests/oop/errors_vector_type.su       # wrong element type on push()
./ussr tests/oop/errors_uno_backtick.su      # unencodable string (contains `)
./ussr tests/oop/errors_uno_unknown_type.su  # decode() of an unregistered struct
```

A quick way to check the batch of error tests at once:

```sh
for f in tests/oop/errors_*.su; do
    echo "== $f =="
    ./ussr "$f"
    echo "exit: $?"
done
```

Every one of those should print exactly one error line, show a
nonzero exit code, and never print `should not reach here`.

## Assumptions worth knowing about

- These tests assume a `set(x): value` builtin exists for plain
  variable assignment (used for aliasing and for the `'...'` literal
  examples). If yours is named differently, it's a find-and-replace
  across these files.
- `basic.su` and `vector.su` both include a small reference-semantics
  check (mutating through an alias, or pushing through an aliased
  vector) — worth keeping even after you have real assertions, since
  it's the part of the design most likely to regress silently if the
  refcounting in `uno.c` ever gets touched.
