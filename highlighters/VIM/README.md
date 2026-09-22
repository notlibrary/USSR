# USSR syntax highlighting for Vim / Neovim

Two files, laid out to match Vim's runtime path convention directly:

```
syntax/ussr.vim      -- the highlighting rules
ftdetect/ussr.vim     -- makes *.su files auto-detect as filetype "ussr"
```

## Install

**Plain Vim**, copy both into your `~/.vim/` (Windows: `~\vimfiles\`) directory,
preserving the `syntax/`/`ftdetect/` subfolder structure:

```
~/.vim/syntax/ussr.vim
~/.vim/ftdetect/ussr.vim
```

**Neovim**: same idea, under `~/.config/nvim/` instead of `~/.vim/`.

**vim-plug / packer / lazy.nvim** users: drop these two files into their own
tiny local plugin directory (matching the same `syntax/`/`ftdetect/`
structure) and point your plugin manager at that local path — there's nothing
else to build, it's just the two files.

Once installed, any `.su` file opened in Vim should highlight automatically —
`ftdetect/ussr.vim` handles the filetype detection, so you don't need to run
`:set filetype=ussr` by hand.

## Folding

The syntax file marks `[...]` blocks as foldable, but Vim only folds using
`syntax` info if you tell it to. Add this to your vimrc, either globally or
scoped to just this filetype:

```vim
autocmd FileType ussr setlocal foldmethod=syntax
```

## What's highlighted

- `#` comments
- `"..."` strings (with `\x` escapes)
- `'...'` UNO literals — styled distinctly from ordinary strings (linked to
  `Special`), since these are genuinely a different thing (a compile-time
  decoded object literal, not a plain string — see the manual §8.4)
- `` `...` `` strings *inside* a UNO literal — styled as `String` again, but
  only recognized within a `'...'` region, matching the language's actual
  three-quote-level design
- Numbers, `true`/`false`/`null`
- Control flow (`if`, `else`, `while`, `break`, `continue`, `return`)
- Builtins (`set`, `print`, `struct`, `new`, `getf`, `setf`, `vec`, `push`,
  `at`, `len`, `encode`, `decode`, `get`, `scan`, `time`, `random64`,
  `seed_random64`, `cd`, the arithmetic commands, `concat`, `eval`)
- The `name(return_var)` slot of every command call — the command name and
  the return variable each get their own highlight, uniformly, whether the
  command is a builtin, a user-defined function, or an external process
- `?`/`!` hash-lookup/assignment sigils
- `$define`/`$include`/`$ifdef`/`$if`/`$else`/`$endif` directive lines, and
  `defined(NAME)` inside a `$if`

## Notes on accuracy

- I couldn't load this into a real copy of Vim or Neovim to confirm rendering
  — there's neither available in the environment I'm working in. I did do a
  careful manual pass for regex correctness (character-class escaping,
  `\zs`/`\ze` boundaries, alternation syntax) and caught and fixed one real
  bug before delivering it — an earlier draft's `defined(...)` handling used
  `\zs` in a way that made part of it a no-op. If anything doesn't highlight
  the way this document describes, tell me what's off and I'll fix the
  regex directly rather than guess again.
- Like any regex-based (not real-parser) highlighter, this can't tell a
  variable literally named `time` from the `time` builtin — both get the
  builtin color. That's inherent to how Vim syntax files work, not specific
  to this one.
- The keyword lists only include the builtins this project has settled on so
  far. Adding a new builtin later just means adding one word to the relevant
  `syn keyword` line in `syntax/ussr.vim`.
