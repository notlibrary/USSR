# highlight (highlighters/) — refinement ideas

**Today:** syntax highlighting for USSR in four forms:
- **Notepad++**: a UDL XML file.
- **TextMate**: `ussr.tmLanguage.json`, which VS Code and Sublime can
  use.
- **Vim/Neovim**: `syntax/ussr.vim` and `ftdetect/ussr.vim`.
- **tree-sitter**: `grammar.js` with a corpus and highlights, folds,
  indents and locals queries.

## Coverage of the language
- Highlight UUASM files (`.ua`, `.uuasm`) with opcodes, registers
  R0..R31, labels and pools (docs/USSR_UUASM_SPEC.md)
- Highlight UNO literals inside strings (`'Package{...}'`), including
  `box.uno` files
- Preprocessor directives (`$include $define $ifdef $once`) and macros
- `.ru` disassembly output
- `.kl` Kremlin launchers, `.kremlin` settings and
  `/etc/murmansk.rules` as tiny grammars
- Novgorod markup (`docs/MARKUP.md`) for previews
- Builtin command names as a separate scope, generated from the
  interpreter's builtin table so the lists stay current

## Editors
- A VS Code extension package (`package.json` + the TextMate grammar +
  language configuration: brackets, comments, auto-closing pairs)
- A Neovim tree-sitter parser registration (`nvim-treesitter`
  `parser_config`) with instructions
- Helix, Zed and Emacs (tree-sitter mode or a classic `ussr-mode.el`)
- JetBrains through the TextMate bundle import
- Kate/KSyntaxHighlighting XML (also used by many Qt editors)
- GtkSourceView `.lang` (gedit, GNOME Builder)
- Pygments and Rouge lexers for docs and GitHub-style rendering
- A GitHub Linguist submission (needs a grammar and samples)
- Highlight.js and Prism definitions for web pages (Novgorod's
  ```` ```ussr ```` fences)
- Mbl built-in highlighting from a shared token list

## Editor features beyond colour
- Snippets: `init(ret): argc argv [ ... ]`, `if/elif/else`, `while`,
  struct declarations
- Folding of `[ ... ]` blocks in every editor (tree-sitter has folds)
- Indentation rules
- Comment toggling (`#`)
- A language server (LSP) in USSR, reusing russia's `check.su` for
  diagnostics, with go-to-definition for user functions, hover with
  builtin contracts, and completion

## Quality
- One source of truth: generate the Vim, Notepad++, TextMate and
  KSyntax definitions from one keyword/scope table
- Tests: tree-sitter corpus for every construct in `grammar.bnf`, and
  TextMate grammar tests (`vscode-tmgrammar-test`)
- Screenshots of each highlighter on the same sample file in the README
- A theme recommendation that matches Pobeda and Mbltishi colours

## Semantic highlighting
- Tell definitions from calls, parameters from locals, and globals
  (declared with `global(_):`) from the rest
- Mark the result variable in `name(result): args` differently from
  the arguments
- Highlight expression blocks `{ ... }` and command blocks `[ ... ]`
  with distinct bracket colours, and rainbow nesting
- Flag unknown commands and unbalanced brackets as errors while typing
- Highlight format strings of `template` and `scan` (`TYPE;TYPE`)
- Embedded languages inside strings: SQL in `dubna_exec`, HTML in
  Novgorod, regex patterns
- Hash-namespace keys (`!name` / `?name`) as their own scope

## Tools around the grammars
- An HTML exporter: a `.su` file to a highlighted page, written in
  USSR (for docs and Novgorod)
- A terminal highlighter (`ussr --cat file.su`) with ANSI colours
- A pretty-printer and formatter sharing the tree-sitter grammar
- Code navigation queries (tags) for GitHub and editors: definitions
  and references
- An outline or symbols view per file
- Injection queries for Markdown code fences tagged `ussr`
- A playground page: type USSR, see the tree-sitter tree

## More files
- C files compiled by usa: mark the extensions usa lacks
- Lenin's `models.conf`, Ural's `box.uno` fields with validation, and
  KGB Markdown conventions (`key: value` facts, `[[links]]`)
- Test expectation files and `.ru` hex dumps
