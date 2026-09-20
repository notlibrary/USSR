# USSR syntax highlighting for Notepad++

## Install

1. Notepad++ menu: **Language → User Defined Language → Define your language...**
2. Click **Import...** and select `USSR_NotepadPlusPlus_UDL.xml`.
3. Close the dialog. Open a `.su` file (or manually pick **Language → USSR** from the
   menu) — it should now be highlighted.

(Newer Notepad++ versions also support dropping the XML straight into the
`userDefineLangs\` folder — **Language → User Defined Language → Open User
Defined Language folder...** shows you where that is — instead of using the
Import dialog.)

## What's highlighted

- **Comments** (`#`) — green, italic
- **Strings** (`"..."`) — standard string red
- **UNO literals** (`'...'`) — bold orange, distinct from ordinary strings
- **UNO-internal strings** (`` `...` ``) — brown, distinct again, since these are
  a third, separate quoting level (see the language manual's §8.4)
- **Numbers** — orange
- **Control flow** (`if`, `while`, `break`, `continue`, `return`) — bold blue
- **Builtins** (`set`, `print`, `struct`, `new`, `getf`, `setf`, `vec`, `push`,
  `at`, `len`, `encode`, `decode`, `scan`, `get`, `time`, `random64`,
  `seed_random64`, `cd`, and the arithmetic/`concat`/`eval` commands) — purple
- **`true`/`false`/`null`** — bold teal
- **Preprocessor directive words** (`define`, `include`, `ifdef`, `else`,
  `endif`, `defined`) — grey italic. The `$` itself is colored as an operator
  rather than specially, since `$define` tokenizes as `$` + `define` either
  way — no special "prefix" handling was needed.
- **Operators/punctuation** — dark red
- Folding on `[...]` blocks, matching the language's actual block syntax

## A few notes on scope

- Notepad++'s UDL system doesn't do real parsing — it's pattern/keyword
  matching, so it can't be 100% precise (e.g. it can't tell a variable
  literally named `time` from the `time` builtin — both get the same color).
  That's a limitation of UDL in general, not something specific to this file.
- The keyword lists (`Keywords2` especially) only include the builtins this
  project has settled on through our conversation history. If you add new
  builtins later, they just won't be colored until you add them to the list
  yourself via **Define your language...** — no need to reimport the whole
  file, you can edit keyword lists directly in that dialog.
- I verified the underlying XML schema (field ordering, the "Comments" list's
  five slot meanings, the Delimiters list's 3-codes-per-pair structure) against
  Notepad++'s own official UDL collection and community documentation rather
  than from memory, since this format is unforgiving of small mistakes — but
  I obviously couldn't load it into a real copy of Notepad++ myself to confirm
  rendering. If anything doesn't highlight the way this document describes,
  tell me what's off and I'll adjust the XML directly rather than guess again.
