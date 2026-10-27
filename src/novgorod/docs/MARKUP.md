# Novgorod markup

A small, markdown-like syntax for forum posts. Anything that is not markup
is shown exactly as typed; HTML is never interpreted.

| You type | You get |
|----------|---------|
| `**bold**` | **bold** |
| `*italic*` | *italic* |
| `~~strike~~` | ~~strike~~ |
| `\|\|spoiler\|\|` | hidden until hovered or tapped |
| `` `code` `` | inline code; no markup inside |
| ` ```lang ` ... ` ``` ` on lines of their own | a code block; the optional language word is shown as a label |
| `> text` at the start of a line | a quoted line |
| `>>123` | a link to post 123, with a preview on hover |
| `>>>/c/` | a link to board /c/ |
| `https://...`, `http://...` | a link |
| `\*`, `` \` ``, `\\`, `\~`, `\|`, `\>`, `\_` | the character itself |

## Rules

- Emphasis opens on a delimiter followed by a non-blank and closes on the
  next matching one preceded by a non-blank, on the same line. So
  `a * b * c` and `int *p, *q` stay as typed, and `**bold *and* italic**`
  nests (up to eight levels).
- An unclosed delimiter is shown as typed.
- A code block runs from a line starting with three backticks to the next
  such line, or to the end of the post. Lines inside are not changed.
- Line breaks are kept; more than one empty line in a row counts as one.
- A URL ends at a blank, a quote or angle bracket; trailing `.,;:!?)]` are
  left outside it. Only `http` and `https` become links.
- `>>N` lines are links, not quotes. Every `>>N` also adds a backlink to post
  N in its thread.

## In code

`src/novgorod/markup.su` has no dependency on the forum:

```USSR
$include "src/novgorod/markup.su"
nvm_render(html): "**hi** `x < y`\n> quoted"
# <b>hi</b> <code>x &lt; y</code><br>
# <span class="quote">&gt; quoted</span>
```
