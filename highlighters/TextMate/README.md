# USSR TextMate grammar

`ussr.tmLanguage.json` is the canonical TextMate grammar for USSR
(`.su`) sources. It covers the full 0.4.6+ surface: command syntax
`name(ret): args`, `[ ]` blocks, `{ }` expressions, `@[]` advanced
control blocks, strings/chars/numbers, `true`/`false`/`null`, the
`M_PI`/`M_E` macros, `!`/`?` hash sigils, preprocessor directives
(`$define` `$include` `$ifdef` `$if` `$else` `$endif`, `defined(...)`),
control flow (`if`/`elif`/`else`/`while`/`do`/`loop`/`for`/`choose`/
`option`/`default`/`break`/`continue`/`return`), the scheduler commands
(`async`/`await`/`sleep`/`yield`/`gc`/`process`/`load`), the io/bytes
primitives, and the math commands.

The same file is mirrored at `highlighters/tree-sitter/syntaxes/
ussr.tmLanguage.json` for the tree-sitter editor integration — keep
the two copies identical when editing.

## Using it

- **VS Code / VSCodium**: reference it from an extension's
  `package.json`:
  ```json
  {
    "contributes": {
      "languages": [{
        "id": "ussr",
        "extensions": [".su"]
      }],
      "grammars": [{
        "language": "ussr",
        "scopeName": "source.ussr",
        "path": "./ussr.tmLanguage.json"
      }]
    }
  }
  ```
- **Sublime Text**: convert to `.sublime-syntax` or place the plist
  conversion in `Packages/USSR/`.
- **TextMate**: drop the converted `.tmLanguage` plist into a bundle's
  `Syntaxes/` folder.
- **GitHub (Linguist)**: GitHub highlights languages through
  [github/linguist](https://github.com/github/linguist). Getting USSR
  recognized there means a Linguist pull request containing a language
  entry (`languages.yml`: name `USSR`, extensions `.su`, this grammar
  as the highlighter source) plus `samples/` — and Linguist only
  accepts languages used across hundreds of public repositories, so
  this grammar is the prerequisite artifact until USSR clears that
  bar. Adding `*.su linguist-language=USSR` to `.gitattributes` only
  takes effect after Linguist knows the language.
