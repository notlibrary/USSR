#!/usr/bin/env python3
"""Writes src/moscow/arch/amd64/fbfont.inc: the console's 8x16 bitmap font
for a framebuffer (UEFI has no VGA text mode, so no VGA font), the 256
characters of code page 437 - what the kernel's console writes, as VGA
text mode shows it - drawn from Pobeda (fonts/pobeda.ttf), the project's
own monospace font. A row is a byte, its top bit the leftmost pixel.

    python src/moscow/tools/fbfont.py [--preview out.png]

Needs Pillow. The .inc is in the repository: building Moscow does not
run this."""
import os
import sys
from PIL import Image, ImageDraw, ImageFont

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, "..", "..", ".."))
TTF = os.path.join(ROOT, "fonts", "pobeda.ttf")
OUT = os.path.join(ROOT, "src", "moscow", "arch", "amd64", "fbfont.inc")
W, H = 8, 16


def cp437(code):
    if code == 0:
        return " "
    if 32 <= code < 127:
        return chr(code)
    if code == 127:
        return "⌂"
    try:
        return bytes([code]).decode("cp437")
    except UnicodeDecodeError:
        return "?"


def glyphs():
    # the size whose cell is 8 wide; the baseline so that descenders fit
    font = None
    for size in range(20, 8, -1):
        f = ImageFont.truetype(TTF, size)
        if f.getlength("M") <= W + 0.01:
            font = f
            break
    ascent, descent = font.getmetrics()
    top = max(0, (H - (ascent + descent)) // 2)
    from fontTools.ttLib import TTFont
    have = set(TTFont(TTF).getBestCmap())
    out = []
    for code in range(256):
        ch = cp437(code)
        if box(ch) is None and ord(ch) not in have:
            ch = "█"            # not in Pobeda: a block, as VGA's console shows it
        img = Image.new("L", (W * 4, H * 4), 0)
        d = ImageDraw.Draw(img)
        # blocks and box drawing fill the cell: drawn by hand
        rows = box(ch)
        if rows is None:
            d.text((0, top), ch, font=font, fill=255)
            rows = []
            for y in range(H):
                bits = 0
                for x in range(W):
                    if img.getpixel((x, y)) >= 110:
                        bits |= 0x80 >> x
                rows.append(bits)
        out.append(rows)
    return out


def box(ch):
    """The cell-filling characters: blocks, shades and the single and
    double box-drawing lines, exact so that they join up."""
    full = [0xFF] * H
    if ch == "█":
        return full
    if ch == "▀":
        return [0xFF] * 8 + [0] * 8
    if ch == "▄":
        return [0] * 8 + [0xFF] * 8
    if ch == "▌":
        return [0xF0] * H
    if ch == "▐":
        return [0x0F] * H
    if ch in "░▒▓":
        pat = {"░": (0x88, 0x22), "▒": (0xAA, 0x55), "▓": (0xEE, 0xBB)}[ch]
        return [pat[y % 2] for y in range(H)]
    # box drawing: which arms, single (1) or double (2)
    arms = BOX.get(ch)
    if arms is None:
        return None
    up, down, left, right = arms
    rows = [0] * H
    mid_y, mid_x = 7, 3          # the single line's row and column
    def hline(y, x0, x1):
        for x in range(x0, x1 + 1):
            rows[y] |= 0x80 >> x
    def vline(x, y0, y1):
        for y in range(y0, y1 + 1):
            rows[y] |= 0x80 >> x
    for kind, (y0, y1) in ((up, (0, mid_y)), (down, (mid_y, H - 1))):
        if kind == 1:
            vline(mid_x, y0, y1)
        elif kind == 2:
            vline(mid_x - 1, y0, y1)
            vline(mid_x + 1, y0, y1)
    for kind, (x0, x1) in ((left, (0, mid_x)), (right, (mid_x, W - 1))):
        if kind == 1:
            hline(mid_y, x0, x1)
        elif kind == 2:
            hline(mid_y - 1, x0, x1)
            hline(mid_y + 1, x0, x1)
    return rows


# up, down, left, right: 0 none, 1 single, 2 double
BOX = {
    "─": (0, 0, 1, 1), "│": (1, 1, 0, 0), "┌": (0, 1, 0, 1), "┐": (0, 1, 1, 0),
    "└": (1, 0, 0, 1), "┘": (1, 0, 1, 0), "├": (1, 1, 0, 1), "┤": (1, 1, 1, 0),
    "┬": (0, 1, 1, 1), "┴": (1, 0, 1, 1), "┼": (1, 1, 1, 1),
    "═": (0, 0, 2, 2), "║": (2, 2, 0, 0), "╔": (0, 2, 0, 2), "╗": (0, 2, 2, 0),
    "╚": (2, 0, 0, 2), "╝": (2, 0, 2, 0), "╠": (2, 2, 0, 2), "╣": (2, 2, 2, 0),
    "╦": (0, 2, 2, 2), "╩": (2, 0, 2, 2), "╬": (2, 2, 2, 2),
}


def main():
    font = glyphs()
    lines = ["; fbfont.inc - the console's 8x16 font on a framebuffer: code page 437,",
             "; drawn from Pobeda by src/moscow/tools/fbfont.py (generated: edit that).",
             "fbfont:"]
    for code, rows in enumerate(font):
        lines.append("        db " + ", ".join("0x%02x" % r for r in rows) + "  ; %d" % code)
    with open(OUT, "w", newline="\n") as f:
        f.write("\n".join(lines) + "\n")
    print("wrote", OUT)
    if "--preview" in sys.argv:
        path = sys.argv[sys.argv.index("--preview") + 1]
        img = Image.new("L", (16 * W * 3, 16 * H * 3), 0)
        for code, rows in enumerate(font):
            ox, oy = (code % 16) * W * 3, (code // 16) * H * 3
            for y, bits in enumerate(rows):
                for x in range(W):
                    if bits & (0x80 >> x):
                        for dy in range(3):
                            for dx in range(3):
                                img.putpixel((ox + x * 3 + dx, oy + y * 3 + dy), 255)
        img.save(path)
        print("preview", path)


if __name__ == "__main__":
    main()
