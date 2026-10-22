"""Build the Pobeda monospace TrueType font (requires FontTools)."""

import sys
from math import cos, pi, sin, sqrt
from pathlib import Path

from fontTools.fontBuilder import FontBuilder
from fontTools.pens.ttGlyphPen import TTGlyphPen
from fontTools.ttLib import newTable
from fontTools.ttLib.tables._g_l_y_f import flagOverlapSimple
from fontTools.ttLib.tables.O_S_2f_2 import Panose


VERSION = "3.5"
# Written next to this script unless a path is given: python build_pobeda.py out.ttf
OUTPUT = Path(sys.argv[1]) if len(sys.argv) > 1 else Path(__file__).with_name("pobeda.ttf")
ADVANCE = 740
STROKE = 92

# One line box from descent to ascent, shared by every metric table, so
# box drawing and block characters touch the cells above and below.
ASCENT = 900
DESCENT = -250
# Centre of the box-drawing lines: half way up a capital (y=3.5 on the grid).
BOX_Y = 130 + 3.5 * 86

# Stroke paths use a compact character grid: x=0..4, baseline y=0, cap y=7.
CAPS = {
    "A": [[(0, 0), (0, 6), (2, 7), (4, 6), (4, 0)], [(0, 3), (4, 3)]],
    "B": [[(0, 0), (0, 7), (2.7, 7), (4, 6), (4, 4.2), (3, 3.5), (0, 3)],
          [(2.7, 3), (4, 2.2), (4, 1), (3, 0), (0, 0)]],
    "C": [[(4, 6.5), (3, 7), (1, 7), (0, 6), (0, 1), (1, 0), (3, 0), (4, .5)]],
    "D": [[(0, 0), (0, 7), (2.5, 7), (4, 5.7), (4, 1.3), (2.5, 0), (0, 0)]],
    "E": [[(4, 7), (0, 7), (0, 0), (4, 0)], [(0, 3.5), (3, 3.5)]],
    "F": [[(0, 0), (0, 7), (4, 7)], [(0, 3.5), (3, 3.5)]],
    "G": [[(4, 6.5), (3, 7), (1, 7), (0, 6), (0, 1), (1, 0), (4, 0), (4, 3), (2.5, 3)]],
    "H": [[(0, 0), (0, 7)], [(4, 0), (4, 7)], [(0, 3.5), (4, 3.5)]],
    "I": [[(0, 7), (4, 7)], [(2, 7), (2, 0)], [(0, 0), (4, 0)]],
    "J": [[(4, 7), (4, 1), (3, 0), (1, 0), (0, 1)]],
    "K": [[(0, 0), (0, 7)], [(4, 7), (0, 3.5), (4, 0)]],
    "L": [[(0, 7), (0, 0), (4, 0)]],
    "M": [[(0, 0), (0, 7), (2, 4), (4, 7), (4, 0)]],
    "N": [[(0, 0), (0, 7), (4, 0), (4, 7)]],
    "O": [[(1, 0), (0, 1), (0, 6), (1, 7), (3, 7), (4, 6), (4, 1), (3, 0), (1, 0)]],
    "P": [[(0, 0), (0, 7), (3, 7), (4, 6), (4, 4), (3, 3.5), (0, 3.5)]],
    "Q": [[(1, 0), (0, 1), (0, 6), (1, 7), (3, 7), (4, 6), (4, 1), (3, 0), (1, 0)],
          [(2.5, 1.5), (4.5, -.5)]],
    "R": [[(0, 0), (0, 7), (3, 7), (4, 6), (4, 4), (3, 3.5), (0, 3.5)],
          [(2.5, 3.5), (4, 0)]],
    "S": [[(4, 6.5), (3, 7), (1, 7), (0, 6), (0, 4.5), (1, 3.5), (3, 3.5),
           (4, 2.5), (4, 1), (3, 0), (1, 0), (0, .5)]],
    "T": [[(0, 7), (4, 7)], [(2, 7), (2, 0)]],
    "U": [[(0, 7), (0, 1), (1, 0), (3, 0), (4, 1), (4, 7)]],
    "V": [[(0, 7), (1.5, 0), (2.5, 0), (4, 7)]],
    "W": [[(0, 7), (0, 0), (2, 2.5), (4, 0), (4, 7)]],
    "X": [[(0, 7), (4, 0)], [(4, 7), (0, 0)]],
    "Y": [[(0, 7), (2, 4), (4, 7)], [(2, 4), (2, 0)]],
    "Z": [[(0, 7), (4, 7), (0, 0), (4, 0)]],
}

LOWERS = {
    "a": [[(4, 4.5), (3, 5), (1, 5), (0, 4), (0, 1), (1, 0), (3, 0), (4, 1), (4, 5)],
          [(4, 0), (4, 5)]],
    "b": [[(0, 7), (0, 0)], [(0, 4), (1, 5), (3, 5), (4, 4), (4, 1), (3, 0), (1, 0), (0, 1)]],
    "c": [[(4, 4.5), (3, 5), (1, 5), (0, 4), (0, 1), (1, 0), (3, 0), (4, .5)]],
    "d": [[(4, 7), (4, 0)], [(4, 4), (3, 5), (1, 5), (0, 4), (0, 1), (1, 0), (3, 0), (4, 1)]],
    "e": [[(0, 2.5), (4, 2.5), (4, 4), (3, 5), (1, 5), (0, 4), (0, 1), (1, 0), (3, 0), (4, .5)]],
    "f": [[(0, 0), (0, 6), (1, 7), (3, 7)], [(0, 4.5), (3, 4.5)]],
    "g": [[(4, 5), (4, -1), (3, -2), (1, -2), (0, -1)], [(4, 4), (3, 5), (1, 5), (0, 4), (0, 1), (1, 0), (3, 0), (4, 1)]],
    "h": [[(0, 7), (0, 0)], [(0, 3.5), (1, 5), (3, 5), (4, 4), (4, 0)]],
    "i": [[(2.5, 5), (2.5, 0)], [(2.5, 7)], [(1.7, 0), (3.3, 0)]],
    "j": [[(2, 5), (2, -1), (1, -2), (0, -2)], [(2, 7), (2.05, 7)]],
    "k": [[(0, 7), (0, 0)], [(4, 5), (0, 2.5), (4, 0)]],
    "l": [[(1.3, 7), (2.5, 7), (2.5, 0), (3.7, 0)]],
    "m": [[(0, 0), (0, 5)], [(0, 3.5), (1, 5), (2, 5), (2.5, 4), (2.5, 0)],
          [(2.5, 3.5), (3.5, 5), (4.5, 5), (5, 4), (5, 0)]],
    "n": [[(0, 0), (0, 5)], [(0, 3.5), (1, 5), (3, 5), (4, 4), (4, 0)]],
    "o": [[(1, 0), (0, 1), (0, 4), (1, 5), (3, 5), (4, 4), (4, 1), (3, 0), (1, 0)]],
    "p": [[(0, -2), (0, 5)], [(0, 4), (1, 5), (3, 5), (4, 4), (4, 1), (3, 0), (1, 0), (0, 1)]],
    "q": [[(4, -2), (4, 5)], [(4, 4), (3, 5), (1, 5), (0, 4), (0, 1), (1, 0), (3, 0), (4, 1)]],
    "r": [[(0, 0), (0, 5)], [(0, 3.5), (1, 5), (3.5, 5)]],
    "s": [[(4, 4.5), (3, 5), (1, 5), (0, 4), (1, 3), (3, 2), (4, 1), (3, 0), (1, 0), (0, .5)]],
    "t": [[(2, 7), (2, 1), (3, 0), (4, 0)], [(0, 5), (4, 5)]],
    "u": [[(0, 5), (0, 1), (1, 0), (3, 0), (4, 1), (4, 5)], [(4, 0), (4, 5)]],
    "v": [[(0, 5), (1.5, 0), (2.5, 0), (4, 5)]],
    "w": [[(0, 5), (0, 0), (2, 2), (4, 0), (4, 5)]],
    "x": [[(0, 5), (4, 0)], [(4, 5), (0, 0)]],
    "y": [[(0, 5), (1.5, 0), (2.5, 0), (4, 5)], [(2.5, 0), (1, -2), (0, -2)]],
    "z": [[(0, 5), (4, 5), (0, 0), (4, 0)]],
}

EXTRA = {
    "А": CAPS["A"], "Б": [[(0, 0), (0, 7), (4, 7)], [(0, 3.5), (3, 3.5), (4, 2.5), (4, 1), (3, 0), (0, 0)]],
    "В": CAPS["B"], "Г": [[(0, 0), (0, 7), (4, 7)], [(2.9, 7), (3.8, 6.1)]], "Д": [[(.2, .8), (1, 7), (3, 7), (3.8, .8)],
          [(.2, .8), (.2, 0)], [(3.8, .8), (3.8, 0)],
          [(-.4, -2.0), (-.4, 0), (4.4, 0), (4.4, -2.0)]],
    "Т": [[(0, 7), (4, 7)], [(2, 7), (2, 0)], [(2.8, 7), (3.7, 6.1)]],
    "Е": CAPS["E"], "Ё": [*CAPS["E"], [(1, 8), (1.05, 8)], [(3, 8), (3.05, 8)]],
    "Ж": [[(0, 7), (4, 0)], [(4, 7), (0, 0)], [(2, 7), (2, 0)]],
    "З": [[(0, 6.5), (1, 7), (3, 7), (4, 6), (3, 4), (2, 3.5), (3, 3), (4, 1), (3, 0), (1, 0), (0, .5)]],
    "И": [[(0, 0), (0, 7)], [(0, 0), (4, 7)], [(4, 0), (4, 7)]], "Й": [[(0, 0), (0, 7)], [(0, 0), (4, 7)], [(4, 0), (4, 7)], [(1, 8), (2, 8.5), (3, 8)]],
    "К": CAPS["K"], "Л": [[(0, 0), (1, 7), (3, 7), (4, 0)]], "М": CAPS["M"], "Н": CAPS["H"],
    "О": CAPS["O"], "П": [[(0, 0), (0, 7), (4, 7), (4, 0)]], "Р": CAPS["P"],
    "С": CAPS["C"], "У": [[(0, 7), (2, 3.5), (4, 7)], [(2, 3.5), (.5, 0)]], "Ф": [[(2, 7), (2, 0)], [(2, 6), (0, 5), (0, 2), (2, 1), (4, 2), (4, 5), (2, 6)]],
    "Х": CAPS["X"], "Ц": [[(0, 7), (0, 0), (3, 0), (4, 1), (4, 7)], [(4, 1), (4, -1)]],
    "Ч": [[(0, 7), (0, 4), (1, 3), (4, 3)], [(4, 7), (4, 0)]],
    "Ш": [[(0, 7), (0, 0), (4, 0), (4, 7)], [(2, 7), (2, 0)]], "Щ": [[(0, 7), (0, 0), (3, 0), (4, 1), (4, 7)], [(2, 7), (2, 0)], [(4, 1), (4, -1)]],
    "Ъ": [[(0, 7), (1, 7), (1, 0)], [(1, 3.5), (3, 3.5), (4, 2.5), (4, 1), (3, 0), (1, 0)]],
    "Ы": [[(0, 7), (0, 0)], [(0, 3.5), (2, 3.5), (3, 2.5), (3, 1), (2, 0), (0, 0)], [(4, 7), (4, 0)]],
    "Ь": [[(0, 7), (0, 0)], [(0, 3.5), (3, 3.5), (4, 2.5), (4, 1), (3, 0), (0, 0)]],
    "Э": [[(0, 6.5), (1, 7), (3, 7), (4, 6), (4, 1), (3, 0), (1, 0), (0, .5)], [(1, 3.5), (4, 3.5)]],
    "Ю": [[(0, 0), (0, 7)], [(0, 3.5), (1.5, 3.5)], [(2, 1), (2, 6), (3, 7), (4, 6), (4, 1), (3, 0), (2, 1)]],
    "Я": [[(4, 0), (4, 7), (1, 7), (0, 6), (0, 4), (1, 3.5), (4, 3.5)], [(2.5, 3.5), (0, 0)]],
}

CYRILLIC_LOWER = {
    "а": [[(4, 4.5), (3, 5), (1, 5), (0, 4), (0, 1), (1, 0), (3, 0), (4, 1)],
            [(4, 1.7), (4, 5)], [(4.0, 0.9), (4.75, -0.1)]], "б": [[(4, 6.5), (3, 7), (1, 7), (0, 6), (0, 1), (1, 0), (3, 0), (4, 1), (4, 4), (3, 5), (1, 5), (0, 4)]],
    "в": [[(0, 0), (0, 5)], [(0, 4), (1, 5), (3, 5), (4, 4), (3, 3), (0, 3)], [(2.5, 3), (4, 2), (4, 1), (3, 0), (0, 0)]],
    "г": [[(0, 0), (0, 5), (4, 5)], [(2.9, 5), (3.8, 4.1)]], "д": [[(.2, .8), (1, 5), (3, 5), (3.8, .8)],
          [(.2, .8), (.2, 0)], [(3.8, .8), (3.8, 0)],
          [(-.4, -2.0), (-.4, 0), (4.4, 0), (4.4, -2.0)]],
    "е": LOWERS["e"], "ё": [*LOWERS["e"], [(1, 6), (1.05, 6)], [(3, 6), (3.05, 6)]],
    "ж": [[(0, 5), (4, 0)], [(4, 5), (0, 0)], [(2, 5), (2, 0)]],
    "з": [[(0, 4.5), (1, 5), (3, 5), (4, 4), (3, 3), (2, 2.5), (3, 2), (4, 1), (3, 0), (1, 0), (0, .5)]],
    "и": [[(0, 0), (0, 5)], [(0, 0), (4, 5)], [(4, 0), (4, 5)]], "й": [[(0, 0), (0, 5)], [(0, 0), (4, 5)], [(4, 0), (4, 5)], [(1, 6), (2, 6.5), (3, 6)]],
    "к": [[(0, 0), (0, 5)], [(4, 5), (0, 2.5), (4, 0)]], "л": [[(0, 0), (1, 5), (3, 5), (4, 0)]], "м": [[(0, 0), (0, 5), (2, 2.7), (4, 5), (4, 0)]], "н": [[(0, 0), (0, 5)], [(4, 0), (4, 5)], [(0, 2.5), (4, 2.5)]],
    "о": LOWERS["o"], "п": [[(0, 0), (0, 5), (4, 5), (4, 0)]], "р": LOWERS["p"],
    "с": LOWERS["c"], "т": [[(0, 5), (4, 5)], [(2, 5), (2, 0)], [(2.8, 5), (3.7, 4.1)]],
    "у": LOWERS["y"], "ф": [[(2, 7), (2, -2)], [(2, 4.5), (0, 4), (0, 1), (2, 0), (4, 1), (4, 4), (2, 4.5)]],
    "х": LOWERS["x"], "ц": [[(0, 5), (0, 0), (3, 0), (4, 1), (4, 5)], [(4, 1), (4, -1)]],
    "ч": [[(0, 5), (0, 3), (1, 2.5), (4, 2.5)], [(4, 5), (4, 0)]],
    "ш": [[(0, 5), (0, 0), (4, 0), (4, 5)], [(2, 5), (2, 0)]], "щ": [[(0, 5), (0, 0), (3, 0), (4, 1), (4, 5)], [(2, 5), (2, 0)], [(4, 1), (4, -1)]],
    "ъ": [[(0, 5), (1, 5), (1, 0)], [(1, 2.5), (3, 2.5), (4, 1.5), (4, 1), (3, 0), (1, 0)]],
    "ы": [[(0, 5), (0, 0)], [(0, 2.5), (2, 2.5), (3, 1.5), (3, 1), (2, 0), (0, 0)], [(4, 5), (4, 0)]],
    "ь": [[(0, 5), (0, 0)], [(0, 2.5), (3, 2.5), (4, 1.5), (4, 1), (3, 0), (0, 0)]],
    "э": [[(0, 4.5), (1, 5), (3, 5), (4, 4), (4, 1), (3, 0), (1, 0), (0, .5)], [(1, 2.5), (4, 2.5)]],
    "ю": [[(0, 0), (0, 5)], [(0, 2.5), (1.5, 2.5)], [(2, 1), (2, 4), (3, 5), (4, 4), (4, 1), (3, 0), (2, 1)]],
    "я": [[(4, 0), (4, 5), (1, 5), (0, 4), (0, 3), (1, 2.5), (4, 2.5)], [(2.5, 2.5), (0, 0)]],
}

# Punctuation and signs common in Russian text and console output.
TEXT_EXTRA = {
    0x00A0: [],                                                     # no-break space
    0x00AB: [[(2, 4.5), (.5, 2.5), (2, .5)], [(4, 4.5), (2.5, 2.5), (4, .5)]],    # «
    0x00BB: [[(0, 4.5), (1.5, 2.5), (0, .5)], [(2, 4.5), (3.5, 2.5), (2, .5)]],   # »
    0x00B0: [[(2, 7), (1.2, 6.4), (1.2, 5.6), (2, 5), (2.8, 5.6), (2.8, 6.4), (2, 7)]],  # °
    0x00B1: [[(0, 4), (4, 4)], [(2, 6), (2, 2)], [(0, .5), (4, .5)]],  # ±
    0x00B2: [[(1, 6.8), (1.5, 7.3), (2.7, 7.3), (3.2, 6.8), (3.2, 6.3), (1, 4.5), (3.2, 4.5)]],  # ²
    0x00D7: [[(.5, 5), (3.5, 1)], [(3.5, 5), (.5, 1)]],             # ×
    0x2013: [[(.5, 3), (3.5, 3)]],                                  # –
    0x2018: [[(2.2, 5.6), (1.8, 7)]],                               # ‘
    0x2019: [[(2.2, 7), (1.8, 5.6)]],                               # ’
    0x201C: [[(1.2, 5.6), (.8, 7)], [(3.2, 5.6), (2.8, 7)]],        # “
    0x201D: [[(1.2, 7), (.8, 5.6)], [(3.2, 7), (2.8, 5.6)]],        # ”
    0x201E: [[(1.2, 1), (.8, -.4)], [(3.2, 1), (2.8, -.4)]],        # „
    0x2026: [[(.3, 0), (.35, 0)], [(2, 0), (2.05, 0)], [(3.7, 0), (3.75, 0)]],  # …
    0x2116: [[(0, 0), (0, 7), (2.4, 0), (2.4, 7)],                  # №
             [(3.7, 5.5), (3.1, 5), (3.1, 3.5), (3.7, 3), (4.3, 3.5), (4.3, 5), (3.7, 5.5)],
             [(3.1, 1.6), (4.3, 1.6)]],
    0x2248: [[(0, 4.2), (1, 5), (3, 3.4), (4, 4.2)], [(0, 1.8), (1, 2.6), (3, 1), (4, 1.8)]],  # ≈
}


def path_points(points):
    return [(90 + x * 82 * 1.30, 130 + y * 86) for x, y in points]


def rounded_node(pen, x, y, radius):
    # A circle as eight quadratic arcs: all points off-curve, the on-curve
    # points implied half way between them. Clockwise like every other
    # contour, so overlapping parts never cancel under the nonzero rule.
    reach = radius / cos(pi / 8)
    pen.qCurveTo(*[(round(x + reach * cos(-2 * pi * i / 8)),
                    round(y + reach * sin(-2 * pi * i / 8))) for i in range(8)], None)
    pen.closePath()


def polygon(pen, points):
    """Draws a closed polygon clockwise, whatever order the points are in."""
    area = sum(x1 * y2 - x2 * y1
               for (x1, y1), (x2, y2) in zip(points, points[1:] + points[:1]))
    if area > 0:
        points = points[::-1]
    pen.moveTo(points[0])
    for point in points[1:]:
        pen.lineTo(point)
    pen.closePath()


def stroke_path(pen, points, radius=STROKE / 2):
    pts = path_points(points)
    for x, y in pts:
        rounded_node(pen, x, y, radius)
    for (x1, y1), (x2, y2) in zip(pts, pts[1:]):
        dx, dy = x2 - x1, y2 - y1
        length = sqrt(dx * dx + dy * dy)
        if length == 0:
            continue
        nx, ny = -dy * radius / length, dx * radius / length
        polygon(pen, [(round(x1 - nx), round(y1 - ny)), (round(x2 - nx), round(y2 - ny)),
                      (round(x2 + nx), round(y2 + ny)), (round(x1 + nx), round(y1 + ny))])


def rect(pen, x1, y1, x2, y2):
    polygon(pen, [(round(x1), round(y1)), (round(x1), round(y2)),
                  (round(x2), round(y2)), (round(x2), round(y1))])


# Box drawing (U+2500...) and blocks (U+2580...) fill the whole cell and
# are not centred. A box entry lists the arms leaving the centre: lowercase
# l r u d for light lines, uppercase for double lines.
BOX = {
    0x2500: "lr", 0x2502: "ud", 0x250C: "rd", 0x2510: "ld", 0x2514: "ru",
    0x2518: "lu", 0x251C: "rud", 0x2524: "lud", 0x252C: "lrd", 0x2534: "lru",
    0x253C: "lrud",
    0x2550: "LR", 0x2551: "UD", 0x2554: "RD", 0x2557: "LD", 0x255A: "RU",
    0x255D: "LU", 0x2560: "RUD", 0x2563: "LUD", 0x2566: "LRD", 0x2569: "LRU",
    0x256C: "LRUD",
}
MIDDLE = (ASCENT + DESCENT) / 2
BLOCKS = {
    0x2580: (0, MIDDLE, ADVANCE, ASCENT),            # upper half
    0x2584: (0, DESCENT, ADVANCE, MIDDLE),           # lower half
    0x2588: (0, DESCENT, ADVANCE, ASCENT),           # full block
    0x258C: (0, DESCENT, ADVANCE / 2, ASCENT),       # left half
    0x2590: (ADVANCE / 2, DESCENT, ADVANCE, ASCENT),  # right half
}
SHADES = {0x2591: 1, 0x2592: 2, 0x2593: 3}  # light, medium, dark


def draw_box(pen, arms):
    cx, cy = ADVANCE / 2, BOX_Y
    edge = {"l": 0, "r": ADVANCE, "u": ASCENT, "d": DESCENT}
    if arms.islower():
        half = STROKE / 2
        for arm in arms:
            if arm in "lr":
                rect(pen, edge[arm], cy - half, cx + (half if arm == "l" else -half), cy + half)
            else:
                rect(pen, cx - half, edge[arm], cx + half, cy + (half if arm == "d" else -half))
        return

    # Double lines: each arm is two rails, m either side of the centre line.
    # A rail stops at the rail of a neighbouring arm on its side (an inner
    # corner), runs straight through when the opposite arm continues it, and
    # otherwise turns the outer corner at -m.
    arms = arms.lower()
    w = STROKE * 0.55
    m = STROKE * 0.75
    caps = w / 2
    for arm in arms:
        sign = 1 if arm in "ru" else -1
        opposite = {"l": "r", "r": "l", "u": "d", "d": "u"}[arm]
        sides = (("u", 1), ("d", -1)) if arm in "lr" else (("r", 1), ("l", -1))
        for side, side_sign in sides:
            if side in arms:
                stop = m
            elif opposite in arms:
                stop = 0
            else:
                stop = -m
            offset = side_sign * m
            # At a corner the rail runs on by half its width to fill the
            # joint; a straight-through rail meets the other half exactly.
            centre = cx if arm in "lr" else cy
            end = centre + sign * stop - (sign * caps if stop else 0)
            if arm in "lr":
                rect(pen, edge[arm], cy + offset - caps, end, cy + offset + caps)
            else:
                rect(pen, cx + offset - caps, edge[arm], cx + offset + caps, end)


def draw_shade(pen, level):
    # A 4x8 grid per glyph with `level` cells of every 2x2 group filled;
    # the pattern repeats exactly across neighbouring glyphs.
    cols, rows = 4, 8
    cw, ch = ADVANCE / cols, (ASCENT - DESCENT) / rows
    lit = [(0, 0), (1, 1), (1, 0)][:level]
    for i in range(cols):
        for j in range(rows):
            if (i % 2, j % 2) in lit:
                rect(pen, i * cw, DESCENT + j * ch, (i + 1) * cw, DESCENT + (j + 1) * ch)


def center_glyph(glyph):
    if not glyph.coordinates:
        return
    left = min(x for x, _ in glyph.coordinates)
    right = max(x for x, _ in glyph.coordinates)
    glyph.coordinates.translate((round((ADVANCE - (right - left)) / 2 - left), 0))


def draw_glyph(glyph_name, paths, special=None):
    pen = TTGlyphPen(None)
    if special == "space":
        return pen.glyph()
    if special == "colon":
        for y in (315, 565):
            half = 41
            pen.moveTo((300 - half, y - half))
            pen.lineTo((300 + half, y - half))
            pen.lineTo((300 + half, y + half))
            pen.lineTo((300 - half, y + half))
            pen.closePath()
        return pen.glyph()
    if special == "bracket_left":
        stroke_path(pen, [(3.5, 7), (.5, 7), (.5, 0), (3.5, 0)], 34)
        stroke_path(pen, [(2.4, 5.8), (1.6, 5.8)], 19)
        stroke_path(pen, [(2.4, 1.2), (1.6, 1.2)], 19)
        return pen.glyph()
    if special == "bracket_right":
        stroke_path(pen, [(.5, 7), (3.5, 7), (3.5, 0), (.5, 0)], 34)
        stroke_path(pen, [(2.4, 5.8), (1.6, 5.8)], 19)
        stroke_path(pen, [(2.4, 1.2), (1.6, 1.2)], 19)
        return pen.glyph()
    if special == "apostrophe":
        stroke_path(pen, [(1.45, 7.1), (1.25, 5.65)], 40)
        stroke_path(pen, [(1.45, 6.3), (2.15, 6.3)], 22)
        return pen.glyph()
    for path in paths:
        stroke_path(pen, path)
    return pen.glyph()


def glyph_id(codepoint):
    return f"uni{codepoint:04X}"


def build():
    OUTPUT.parent.mkdir(parents=True, exist_ok=True)
    unicodes = (list(range(32, 127)) + [0x2014, 0x0401, 0x0451] + list(range(0x0410, 0x0450))
                + sorted(TEXT_EXTRA) + sorted(BOX) + sorted(BLOCKS) + sorted(SHADES))
    glyph_names = [".notdef"] + [glyph_id(code) for code in unicodes]
    fb = FontBuilder(1000, isTTF=True)
    fb.setupGlyphOrder(glyph_names)

    cmap = {code: glyph_id(code) for code in unicodes}
    fb.setupCharacterMap(cmap)

    notdef_paths = [[(0, 0), (0, 7), (4, 7), (4, 0), (0, 0)],
                    [(0, 0), (4, 7)], [(4, 0), (0, 7)]]
    outlines = {".notdef": draw_glyph(".notdef", notdef_paths)}
    full_cell = set()
    for code in unicodes:
        char = chr(code)
        if code in BOX or code in BLOCKS or code in SHADES:
            pen = TTGlyphPen(None)
            if code in BOX:
                draw_box(pen, BOX[code])
            elif code in BLOCKS:
                rect(pen, *BLOCKS[code])
            else:
                draw_shade(pen, SHADES[code])
            outlines[glyph_id(code)] = pen.glyph()
            full_cell.add(glyph_id(code))
        elif code in TEXT_EXTRA:
            outlines[glyph_id(code)] = draw_glyph(glyph_id(code), TEXT_EXTRA[code])
        elif char == " ":
            outlines[glyph_id(code)] = draw_glyph(glyph_id(code), [], "space")
        elif char == ":":
            outlines[glyph_id(code)] = draw_glyph(glyph_id(code), [], "colon")
        elif char == "[":
            outlines[glyph_id(code)] = draw_glyph(glyph_id(code), [], "bracket_left")
        elif char == "]":
            outlines[glyph_id(code)] = draw_glyph(glyph_id(code), [], "bracket_right")
        elif char == "'":
            outlines[glyph_id(code)] = draw_glyph(glyph_id(code), [], "apostrophe")
        elif char in CAPS:
            outlines[glyph_id(code)] = draw_glyph(glyph_id(code), CAPS[char])
        elif char in LOWERS:
            outlines[glyph_id(code)] = draw_glyph(glyph_id(code), LOWERS[char])
        elif char in EXTRA:
            outlines[glyph_id(code)] = draw_glyph(glyph_id(code), EXTRA[char])
        elif char in CYRILLIC_LOWER:
            outlines[glyph_id(code)] = draw_glyph(glyph_id(code), CYRILLIC_LOWER[char])
        elif char.isascii() and char.isdigit():
            digit_paths = {
                "0": CAPS["O"], "1": [[(1.3, 5.5), (2.5, 7), (2.5, 0)], [(1.4, 0), (3.6, 0)]],
                "2": [[(0, 5.5), (1, 7), (3, 7), (4, 6), (4, 5), (0, 0), (4, 0)]],
                "3": [[(0, 6.5), (1, 7), (3, 7), (4, 6), (3, 4), (2, 3.5), (3, 3), (4, 1), (3, 0), (1, 0), (0, .5)]],
                "4": [[(3.5, 0), (3.5, 7), (0, 2.5), (4, 2.5)]],
                "5": [[(4, 7), (0, 7), (0, 3.5), (3, 3.5), (4, 2.5), (4, 1), (3, 0), (1, 0), (0, .5)]],
                "6": [[(4, 6.5), (3, 7), (1, 7), (0, 6), (0, 1), (1, 0), (3, 0), (4, 1), (4, 2.5), (3, 3.5), (0, 3.5)]],
                "7": [[(0, 7), (4, 7), (1, 0)]],
                "8": [[(1, 3.5), (0, 4.5), (0, 6), (1, 7), (3, 7), (4, 6), (4, 4.5), (3, 3.5), (1, 3.5), (0, 2.5), (0, 1), (1, 0), (3, 0), (4, 1), (4, 2.5), (3, 3.5)]],
                "9": [[(4, 3.5), (1, 3.5), (0, 4.5), (0, 6), (1, 7), (3, 7), (4, 6), (4, 1), (3, 0), (1, 0), (0, .5)]],
            }
            outlines[glyph_id(code)] = draw_glyph(glyph_id(code), digit_paths[char])
        else:
            punctuation = {
                "!": [[(2, 7), (2, 2)], [(2, 0), (2.05, 0)]],
                '"': [[(1, 7), (1, 5.5)], [(3, 7), (3, 5.5)]],
                "#": [[(1, 7), (0, 0)], [(4, 7), (3, 0)], [(0, 2.2), (4, 2.2)], [(0, 4.8), (4, 4.8)]],
                "$": [[(4, 6.5), (3, 7), (1, 7), (0, 6), (1, 4), (3, 3), (4, 2), (3, 0), (1, 0), (0, .5)], [(2, 8), (2, -1)]],
                "%": [[(0, 6), (1, 7), (2, 6), (1, 5), (0, 6)], [(4, 1), (3, 2), (2, 1), (3, 0), (4, 1)], [(0, 0), (4, 7)]],
                "&": [[(4, 0), (1, 4), (0, 5), (1, 7), (3, 7), (4, 6), (0, 1), (1, 0), (4, 0)]],
                "(": [[(3, 7), (2.2, 6), (1.4, 4.8), (1, 3.5), (1.4, 2.2), (2.2, 1), (3, 0)]],
                ")": [[(1, 7), (1.8, 6), (2.6, 4.8), (3, 3.5), (2.6, 2.2), (1.8, 1), (1, 0)]],
                "*": [[(2, 6), (2, 2)], [(0, 5), (4, 3)], [(4, 5), (0, 3)]],
                "+": [[(0, 3), (4, 3)], [(2, 5), (2, 1)]],
                ",": [[(2, 1), (1, -1)]], "-": [[(0, 3), (4, 3)]],
                ".": [[(2, 0), (2.05, 0)]], "/": [[(0, 0), (4, 7)]],
                "\\": [[(4, 0), (0, 7)]],
                ";": [[(2, 4), (2.05, 4)], [(2, 1), (1, -1)]],
                "<": [[(4, 6), (0, 3.5), (4, 1)]], "=": [[(0, 4), (4, 4)], [(0, 2), (4, 2)]],
                ">": [[(0, 6), (4, 3.5), (0, 1)]], "?": [[(0, 5.5), (1, 7), (3, 7), (4, 6), (4, 5), (2, 3.5), (2, 2)], [(2, 0), (2.05, 0)]],
                "@": [[(4, 1), (3, 0), (1, 0), (0, 1), (0, 6), (1, 7), (4, 7), (4, 2), (2, 2), (2, 5), (4, 5)]],
                "^": [[(0, 4), (2, 7), (4, 4)]], "_": [[(0, -1), (4, -1)]],
                "`": [[(1, 7), (2, 6)]], "{": [[(3, 7), (2, 6), (2, 4), (1, 3.5), (2, 3), (2, 1), (3, 0)]],
                "|": [[(2, 8), (2, -1)]], "}": [[(1, 7), (2, 6), (2, 4), (3, 3.5), (2, 3), (2, 1), (1, 0)]],
                "~": [[(0, 3), (1, 4), (3, 2), (4, 3)]],
                "\u2014": [[(-.7, 3), (4.7, 3)]],
            }
            outlines[glyph_id(code)] = draw_glyph(glyph_id(code), punctuation.get(char, []))

    for name, glyph in outlines.items():
        if name not in full_cell:
            center_glyph(glyph)
        # Strokes overlap by design; tell rasterizers (notably macOS) so
        # they do not draw seams where they cross.
        if glyph.numberOfContours > 0:
            glyph.flags[0] |= flagOverlapSimple

    fb.setupGlyf(outlines)
    fb.setupHorizontalMetrics({
        name: (ADVANCE, min(x for x, _ in glyph.coordinates) if glyph.coordinates else 0)
        for name, glyph in outlines.items()
    })
    # hhea, OS/2 typo and OS/2 win metrics all describe the same line, so
    # every platform uses the same line height and box drawing joins up.
    fb.setupHorizontalHeader(ascent=ASCENT, descent=DESCENT, lineGap=0)
    fb.font["head"].fontRevision = float(VERSION)
    fb.setupOS2(
        sTypoAscender=ASCENT, sTypoDescender=DESCENT, sTypoLineGap=0,
        usWinAscent=ASCENT, usWinDescent=-DESCENT, sxHeight=560, sCapHeight=732,
        version=4, usWeightClass=400, usWidthClass=5, achVendID="USSR",
        xAvgCharWidth=ADVANCE,
        fsSelection=0x40 | 0x80,  # REGULAR | USE_TYPO_METRICS
        # Latin 1, Cyrillic.
        ulCodePageRange1=(1 << 0) | (1 << 2),
        panose=_panose_monospace(),
    )
    fb.setupNameTable({
        # The family name carries no version, so an update replaces the
        # installed font instead of appearing as a second family.
        "familyName": "Pobeda",
        "styleName": "Regular",
        "uniqueFontIdentifier": f"USSR;Pobeda-Regular;{VERSION}",
        "fullName": "Pobeda Regular",
        "psName": "Pobeda-Regular",
        "version": f"Version {VERSION}",
        "manufacturer": "USSR",
        "designer": "USSR",
        "description": "Monospaced steel-office type for code, with Cyrillic support.",
    })
    # isFixedPitch is what terminals and editors check to offer the font
    # in their monospace font lists.
    fb.setupPost(isFixedPitch=1, underlinePosition=-150, underlineThickness=STROKE)
    fb.setupMaxp()
    # Smooth at every size: the outlines carry no hinting instructions.
    gasp = newTable("gasp")
    gasp.gaspRange = {0xFFFF: 0x000A}  # GRIDFIT_OFF | DOGRAY | SYMMETRIC_SMOOTHING
    fb.font["gasp"] = gasp
    fb.save(OUTPUT)
    print(f"Created {OUTPUT} ({OUTPUT.stat().st_size:,} bytes, {len(unicodes)} characters)")


def _panose_monospace():
    panose = Panose()
    panose.bFamilyType = 2   # Latin text
    panose.bProportion = 9   # monospaced
    return panose


if __name__ == "__main__":
    build()
