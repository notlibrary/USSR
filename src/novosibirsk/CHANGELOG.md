# Changelog

## 0.5.0
Big documents. 100,000 paragraphs (7.5 MB of text) open in about 0.4 s,
the first frame is drawn in about 0.4 s, and typing, Enter, Backspace and
moving the cursor take milliseconds. Before, every frame took seconds and
opening took a minute.
- The status line's word and character counts were recounted over the
  whole document on every frame. They are now kept up to date paragraph
  by paragraph, and counted in one native pass when a document is opened.
- Each paragraph's wrapped lines are cached. The cache is keyed by its
  text, the page width and the font, so a frame wraps only paragraphs
  that changed. Words are found natively (`re_all`) rather than a byte
  at a time.
- Opening splits the text natively, and cleans only lines with tabs or
  control characters. Saving joins it in one pass (`vjoin`).
- Enter and Backspace across paragraphs change the paragraph and span
  lists in place (`vinsert`, `vremove`) rather than rebuilding them.
- Undo keeps a copy of the paragraph list (`vcopy`) instead of copying
  every paragraph's text. Paragraphs are replaced, not changed, so the
  copy stays right; an undo step also brings back the counts.
- Without the vector toolkit (Moscow, an older ussr) the old paths are
  kept.

## 0.4.0
- The toolbar's OPEN (a folder picture), SAVE (a diskette) and SAVE AS
  show Petersburg's file dialog (documents, plain text or all files),
  through Siberia's shared toolbar.
- The FILE menu (New, Open..., Save, Save As..., the last 5 documents from
  `~/.novosibirsk-recent`, Exit) comes from Siberia's shared toolbar.

## 0.3.0
- `fonts.su`: fonts on parts of the text.
  - Select text with Shift and the arrows, Home/End or Page Up/Down, a
    Shift+click, or Ctrl+A. FONT, B and I (Ctrl+B, Ctrl+I) then give only
    the selection its family, bold or italic; with nothing selected they
    set the document's font, as before.
  - Mixed text is measured and drawn piece by piece in each span's font,
    so it wraps and places the caret correctly.
  - Typing, Delete or Backspace over a selection replaces it.
  - The spans follow edits, splits and joins, and undo keeps them.
  - A `.fodt` with spans is written through the rich writer and read back
    with them.
- Help (F1 or HELP) is a box drawn over the page.
- Holding an arrow key keeps moving and scrolls the page.
- `tests/gui/novosibirsk_fonts_test.su`.

## 0.2.0
Refine pass 1 from REFINE.md; the new code is `more.su`.
- A richer document beside the pad's plain paragraphs: character
  formatting on ranges (bold, italic, underline, strikethrough, sub and
  superscript, font, size, colour, highlight), links and footnotes;
  paragraph alignment, indents, spacing, page breaks; headings 1-6;
  bulleted and numbered lists; title, author and keywords.
- Flat ODF out and in with automatic styles, `text:h`, `text:list`,
  `text:a`, `text:note` and `office:meta`; its own files round-trip
  exactly, and the pad still reads them.
- HTML, Markdown, plain text (with line numbers) and RTF out; Markdown,
  HTML and RTF in.
- Find and replace with regex and case; statistics (words, characters,
  sentences, words per sentence, reading time, repeated words);
  autocorrect of quotes («» or “”), dashes, ellipses and ©/™; special
  characters; word movement and smart Home; contents with dot leaders;
  outline section moves; comparing two documents; letter, report and
  notes templates; fields ({date}, {title}, {author}, {file}, {words}).
- In the window: Ctrl+Z and Ctrl+Y undo and redo (typing grouped),
  Ctrl+Left/Right by word, Ctrl+Backspace deletes a word, Ctrl+Up/Down by
  paragraph, autocorrect while typing, and the word and character count
  in the status bar.
- `ussr src/novosibirsk/novosibirsk.su --convert in out` (.fodt .txt .md
  .html .rtf) and `--stats file` need no window.
- `tests/novosibirsk_more_test.su` (83 checks); `tests/novosibirsk_test.su`
  is unchanged.

## 0.1.0
- First version: the document editor split from Siberia's `--pad`, with
  system fonts, `.fodt` and `.txt`.
