# Omsk

Omsk makes image thumbnails, written in pure USSR: the O of the
**ROUND stack**. Novgorod gives it every posted picture and shows the
200x200 thumbnail it returns in the picture's place.

| Layer | Name | Job |
|-------|------|-----|
| R | [Ryazan](../ryazan/) | TLS |
| O | Omsk | image thumbnails |
| U | [Ukraine](../ukraine/) | HTTP server |
| N | [Novgorod](../novgorod/) | the forum |
| D | [Dubna](../dubna/) | PostgreSQL client |

```USSR
$include "src/omsk/omsk.su"

init(ret): argc argv [
    read_bytes(v): "picture.png"
    omsk_thumb(t): v 200          # [w h tw th jpeg-bytes], or null
    at(jpeg): t 4
    write_bytes(_): "thumb.jpg" jpeg
    set(ret): 0
]
```

or from the shell: `ussr src/omsk/examples/thumb.su picture.png thumb.jpg`.

## What it reads

- **PNG**: greyscale, RGB, palette, greyscale with alpha and RGBA, at
  every bit depth (1, 2, 4, 8, 16); transparent pixels go on white. Not
  interlaced (Adam7).
- **JPEG**: baseline and extended sequential Huffman, greyscale or YCbCr,
  1x/2x/4x sampling (4:4:4, 4:2:2, 4:2:0 ...), restart intervals. Not
  progressive, arithmetic coded, lossless or CMYK.

That covers everything a browser's canvas produces, which is what Novgorod
receives. The thumbnail keeps the picture's proportions and fits the box
(a 2048x1536 picture becomes 200x150); a smaller picture keeps its size.
It is always a baseline JPEG, quality 85, without chroma subsampling.

## How it works in an interpreter

- A JPEG is decoded at 1/8, 1/4 or 1/2 of its size when that is still
  at least the thumbnail, with a reduced inverse DCT as in libjpeg's
  scaled decoding (1/8 needs only the DC coefficients). Colour planes that
  were subsampled are decoded at twice that scale, so they keep the
  thumbnail's resolution.
- A PNG is inflated and unfiltered row by row; only the last 32 KiB and
  the rows the thumbnail samples are kept.
- Each thumbnail pixel averages up to 4x4 samples spread over its share of
  the picture.
- USSR vectors grow only at the end, so every table is built in order with
  `push` and read with `at`.

On one core of a desktop PC: a 2048x1536 JPEG of 1.3 MB in about 4 s, a
640x480 one in 2 s; a 2048x1536 RGBA PNG in about 12 s (Paeth rows cost the
most). The scheduler's time slices keep a server like Novgorod answering
other requests meanwhile. `omsk_max_pixels` (2048x2048 by default) refuses
anything larger, so a tiny file that claims a huge size cannot keep it busy.

See [docs/API.md](docs/API.md) for the functions and
[tests/TESTPLAN.md](tests/TESTPLAN.md) for how it is checked.
