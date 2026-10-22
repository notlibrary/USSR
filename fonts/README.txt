POBEDA 3.5 — original vector-drawn monospace font

Output: fonts/pobeda.ttf
Source: build_pobeda.py
Requires Python 3 and fontTools.

Build:
    python build_pobeda.py              writes pobeda.ttf next to the script
    python build_pobeda.py out.ttf      writes it elsewhere

Glyph outlines are drawn by this script with FontTools TTGlyphPen; no existing font is loaded or copied.
Includes ASCII and Russian Cyrillic, square code punctuation, custom lowercase к/м, and Д/д with two diagonal base hatches.
Early custom font design; not yet a full professional type family.

Version 3.5 (usability):
- Marked as fixed pitch (post.isFixedPitch, Panose monospaced), so terminals and editors list it with the monospace fonts.
- Family name is plain "Pobeda": a new version replaces the installed font instead of adding "Pobeda 3.x" next to it.
- One line height (900/-250, no gap) in hhea, OS/2 typo and OS/2 win metrics, so it spaces the same on every platform.
- Box drawing ─│┌┐└┘├┤┬┴┼ ═║╔╗╚╝╠╣╦╩╬, blocks █▀▄▌▐ and shades ░▒▓ fill the whole cell and join up, for console UIs.
- Russian text punctuation: «» „“ ‘’ – … № ° ± × ² ≈ and no-break space.
- Round stroke ends are quadratic curves instead of 64-sided polygons, all contours wind the same way and are flagged as overlapping: smoother edges, no seams, and a third of the file size.
- Capital Т keeps its corner tick again (a duplicate entry had replaced it with a plain T).

Version 3.2: enlarged the two diagonal base hatches on Cyrillic Д and д so they remain visibly distinct at normal text sizes.

I am bad font designer AI is worse I am sorry if it damages your unique aesthetic pleasure and sense of beauty
