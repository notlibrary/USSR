# Novosibirsk

Novosibirsk is a small document editor written in USSR on a Petersburg
canvas. It was `siberia.su --pad` until Siberia was split into the
spreadsheet ([Siberia](../siberia/README.md)) and this. Start it from the
repository root with a GLFW-enabled build:

    ussr src/novosibirsk/novosibirsk.su              # empty document
    ussr src/novosibirsk/novosibirsk.su notes.fodt   # open or start one

## Files

Documents are OpenDocument text in its flat, single-XML form (`.fodt`),
which LibreOffice, Calligra and other ODF programs open and save directly,
or plain `.txt`. Zip-packed `.odt` (and `.docx`) are refused with a
message; save them as flat ODF in LibreOffice first. A name without an
extension gets `.fodt`.

## Editing

Type to write; any script works (input arrives through Petersburg's
`"char"` event). Arrows, Home/End (of the wrapped line), Ctrl+Home/End,
Page Up/Down, Enter, Backspace, Delete; Tab inserts four spaces; a click
places the cursor. **FONT** lists the TrueType families installed on the
system (Windows `Fonts` folders, `/usr/share/fonts`, `~/.fonts`, macOS
font folders); **-**/**+** step the size through 8..72 pt; **B** and **I**
(Ctrl+B, Ctrl+I) pick the bold and italic faces. Font, size and style
apply to the whole document and are saved as its default paragraph style.

Ctrl+N new, Ctrl+O open, Ctrl+S save, Ctrl+Shift+S save as, Ctrl+Q quit,
F1 help. New and Open ask before unsaved changes are lost.

## Implementation

All of Novosibirsk is USSR code in `novosibirsk.su`: the document and its
cursor, line wrapping, the `.fodt` reader and writer, the system font
scan and the pad's window. It defines `SB_PAD` and includes
`../siberia/siberia.su` for the shared parts (helpers, XML, the file
dialog, the event handlers), so its state and functions keep Siberia's
`sb_` prefix. The engine works without a window and is what
`tests/novosibirsk_test.su` checks; `tests/gui/siberia_test.su` drives
both windows. Outline text comes from Petersburg's
`loadfont`/`canvasfonttext`, a small TrueType reader in `src/ttf.h`.
