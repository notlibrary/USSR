# Novosibirsk — refinement ideas

**Today:** a small document editor split from Siberia's `--pad`. It reads
and writes flat ODF text (`.fodt`) and `.txt`, and wraps paragraphs with
system TrueType fonts (Petersburg `loadfont`). There is one font, size
and bold/italic for the whole document, saved as the default paragraph
style. It has a font picker, navigation keys, click to place the cursor,
and new/open/save/save-as with an unsaved-changes guard.

**Refine pass 1 (0.2.0):** `more.su`; [x] done, [~] partly done. See
CHANGELOG.md and `tests/novosibirsk_more_test.su`.

## Character formatting
- [~] Bold, italic, underline, strikethrough, sub/superscript on a
  selection, not the whole document (ODF text spans with automatic
  styles) — partly: bold, italic, underline, strikethrough, sub and superscript on byte ranges (nsm_format), as ODF spans with automatic styles, in the document model and its files (more.su); the window draws one plain style
- [~] Font family, size and colour per span; a highlight colour — partly: font, size, colour and highlight per span, in the document model and its files (more.su); the window draws one plain style
- Keyboard shortcuts on the selection: Ctrl+B/I/U

## Paragraph formatting
- [~] Alignment: left, centre, right, justify — partly: left, centre, right, justify in .fodt, HTML and RTF, in the document model and its files (more.su); the window draws one plain style
- [~] Indents, first-line indent, spacing before/after, line spacing — partly: indents, first-line indent, spacing before and after, line spacing in .fodt, in the document model and its files (more.su); the window draws one plain style
- [~] Headings (Heading 1–6) using ODF outline levels, plus a navigator pane — partly: headings 1-6 as text:h with outline levels; nsm_outline lists them; no navigator pane
- [~] Bulleted and numbered lists with nesting — partly: bulleted and numbered lists (Markdown nesting by depth), in the document model and its files (more.su); the window draws one plain style
- [~] Page breaks and paragraph keep-with-next — partly: page breaks before a paragraph in .fodt; no keep-with-next
- Named paragraph styles, with a style picker

## Editing
- Text selection with Shift+arrows, mouse drag, double-click word,
  triple-click paragraph
- Cut, copy and paste with the system clipboard
- [x] Undo/redo with grouping (Mbl's change model could be reused)
- [~] Find and replace, with match case and regex — partly: nsm_find and nsm_replace with regex and case; no dialog
- [x] Word and character count in the status bar
- Spell checking with Hunspell dictionaries (Russian and English)
- [x] Auto-correct for quotes («» or “”), dashes and ellipses
- [~] Insert special characters (a table with Pobeda's Cyrillic punctuation) — partly: nsm_specials lists them; no picker
- [x] Ctrl+arrow word movement, Ctrl+Backspace word delete

## Objects
- Tables (ODF `table:table`) with cell editing, shared with Siberia's
  cell code
- Images (PNG/JPEG via Omsk decoding) inline in the text
- [~] Hyperlinks, and footnotes and endnotes — partly: links and footnotes in .fodt, HTML (numbered notes) and Markdown, in the document model and its files (more.su); the window draws one plain style
- [~] A table of contents generated from headings — partly: nsm_toc and nsm_toc_text (dot leaders); not inserted as a field
- Page headers and footers with page numbers

## Pages and output
- Page layout view: paper size, margins, orientation
- Print to PDF (a small PDF writer with embedded TrueType subsets)
- [x] Export to HTML, Markdown and plain text
- [x] Import of Markdown and HTML
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
- [~] Round-trip tests: LibreOffice-written `.fodt` files load and save
  without losing content — partly: its own .fodt files round-trip exactly; no LibreOffice-written samples yet
- Golden rendering tests of wrapping across fonts and sizes

## Writing aids
- [~] An outline mode: collapse and move sections by their headings — partly: nsm_outline and nsm_move_section move a heading with its section; no outline view
- Focus mode (only the current paragraph bright) and a full-screen
  typewriter mode
- [~] Smart Home/End, and paragraph movement with Ctrl+Up/Down — partly: Ctrl+Up/Down move by paragraph; nsm_smart_home is not bound yet
- [~] Templates for letters, reports and notes — partly: letter, report and notes (nsm_template); no dialog
- Comments in the margin and tracked changes (ODF change tracking)
- [~] Compare two documents and show the differences — partly: nsm_compare: a paragraph diff; no view
- [x] Document statistics: reading time, sentence length, repeated words
- A thesaurus and grammar hints (through a Lenin model, opt-in)
- Typing of dead-key accents and a compose sequence

## Document structure
- Sections with columns
- [~] Fields: date, page count, file name, cross-references — partly: {date} {title} {author} {file} {words} {paragraphs} filled by nsm_fill_fields; no page count
- Bookmarks and internal links
- Bibliography entries and citations
- [~] Line numbering for code or legal text — partly: line numbers in text output (nsm_text); not in the window
- Drop caps and paragraph borders or shading
- [~] Tab stops with leaders (for tables of contents) — partly: dot leaders in the contents text; no tab stops in paragraphs

## Files, further
- Keep unknown ODF content on round trip: elements Novosibirsk does not
  understand are written back untouched
- [x] Document properties (title, author, keywords) in `office:meta`
- [x] RTF import and export
- Embedded fonts or a font-missing warning with a substitute
- Password-protected documents
- [x] A command-line converter: `novosibirsk.su --convert in.fodt out.txt`
  without a window (the engine already runs headless)
