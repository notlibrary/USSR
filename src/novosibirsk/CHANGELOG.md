# Changelog

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
