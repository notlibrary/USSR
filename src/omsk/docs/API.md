# Omsk API

`$include "src/omsk/omsk.su"` (it includes `src/libs/strings.su`). Images
are byte vectors (`read_bytes`, `write_bytes`) or base64 text.

| Call | Result |
|------|--------|
| `omsk_thumb(t): bytes side` | `[w h tw th jpeg]`: the picture's size, the thumbnail's size and the thumbnail as JPEG bytes; `null` when the picture cannot be read |
| `omsk_thumb_base64(t): text side` | the same for base64 text, with the JPEG as base64 |
| `omsk_info(i): bytes` | `["png" w h]` or `["jpeg" w h]` from the header alone; `null` for anything else |
| `omsk_fit(d): w h side` | `[tw th]`: the thumbnail size, proportions kept, at most `side` on each edge, never larger than the picture |
| `omsk_base64(s): bytes` | base64 text |
| `omsk_unbase64(v): text` | bytes, or `null` when it is not base64 (padding only at the end) |
| `omsk_error(s):` | why the last `omsk_thumb` gave `null`, e.g. `progressive or arithmetic JPEG is not supported` |

`omsk_max_pixels` (a global, 4194304 = 2048x2048 by default) is the
largest `w * h` that `omsk_thumb` decodes; set it after the first Omsk
call, or before with `global(_): omsk_max_pixels`.

Lower-level pieces, for tests and tools: `omsk_png(rgb): bytes tw th` and
`omsk_jpeg(rgb): bytes tw th` give the thumbnail as `tw * th * 3` RGB
values, row by row; `omsk_encode(jpeg): rgb w h` writes RGB values as a
baseline JPEG.
