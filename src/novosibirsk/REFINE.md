# Novosibirsk — refinement ideas

**Today:** a small document editor split from Siberia's `--pad`. It reads
and writes flat ODF text (`.fodt`) and `.txt`, and wraps paragraphs with
system TrueType fonts (Petersburg `loadfont`). There is one font, size
and bold/italic for the whole document, saved as the default paragraph
style. It has a font picker, navigation keys, click to place the cursor,
and new/open/save/save-as with an unsaved-changes guard.

## Character formatting
- Bold, italic, underline, strikethrough, sub/superscript on a
  selection, not the whole document (ODF text spans with automatic
  styles)
- Font family, size and colour per span; a highlight colour
- Keyboard shortcuts on the selection: Ctrl+B/I/U

## Paragraph formatting
- Alignment: left, centre, right, justify
- Indents, first-line indent, spacing before/after, line spacing
- Headings (Heading 1–6) using ODF outline levels, plus a navigator pane
- Bulleted and numbered lists with nesting
- Page breaks and paragraph keep-with-next
- Named paragraph styles, with a style picker

## Editing
- Text selection with Shift+arrows, mouse drag, double-click word,
  triple-click paragraph
- Cut, copy and paste with the system clipboard
- Undo/redo with grouping (Mbl's change model could be reused)
- Find and replace, with match case and regex
- Word and character count in the status bar
- Spell checking with Hunspell dictionaries (Russian and English)
- Auto-correct for quotes («» or “”), dashes and ellipses
- Insert special characters (a table with Pobeda's Cyrillic punctuation)
- Ctrl+arrow word movement, Ctrl+Backspace word delete

## Objects
- Tables (ODF `table:table`) with cell editing, shared with Siberia's
  cell code
- Images (PNG/JPEG via Omsk decoding) inline in the text
- Hyperlinks, and footnotes and endnotes
- A table of contents generated from headings
- Page headers and footers with page numbers

## Pages and output
- Page layout view: paper size, margins, orientation
- Print to PDF (a small PDF writer with embedded TrueType subsets)
- Export to HTML, Markdown and plain text
- Import of Markdown and HTML
- Zip-packed `.odt` read/write once a zip/deflate library exists (Omsk
  already has inflate)
- `.docx` import (through a zip reader and a minimal WordprocessingML
  parser)

## Interface
- Zoom, plus a draft view versus a page view
- A ruler with tab stops and indents
- A recent files list and autosave and crash recovery
- Bidi (RTL) text and complex scripts through a shaping step
- Remember window size and the last font

## Tests
- Round-trip tests: LibreOffice-written `.fodt` files load and save
  without losing content
- Golden rendering tests of wrapping across fonts and sizes
