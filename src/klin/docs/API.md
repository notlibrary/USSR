# Klin API

Include `klin.su`. The engine needs no window: `klinreset`, `klinpress` and
the getters work in any script, which is how `tests/klin_test.su` drives it.

| Function | Arguments | Result |
|---|---|---|
| `klinreset(r): mode` | `"casual"`, `"sci"` or `"prog"` | Clears the calculator (degrees, base 10) |
| `klinmode(r): mode` | Mode name | Switches mode, keeping the value on the display (truncated to an integer for `"prog"`) |
| `klinpress(r): key` | Key label | Presses one key, by its label (see below) |
| `klindisplay(r):` | - | Main display text (`"Error"` after an error) |
| `klinexpr(r):` | - | Expression line: the pending expression, the last `... =`, or the error message |
| `klinvalue(r):` | - | Current value: real in casual/sci, 64-bit integer in prog |
| `klinbasetext(r): v base` | Integer, 16/10/8/2 | `v` in that base; 16, 8 and 2 show the 64-bit two's complement |
| `klininitialize(canvas): window` | Window | Adds the mode toolbar and the canvas; returns the canvas |
| `klindraw(r):` | - | Redraws the canvas |
| `klinclick(r): canvas x y` | Canvas "pointer" event | Presses the key under the point |
| `klinkey(r): window key` / `klinkeyup(r): window key` | Window "key"/"keyup" events | Keyboard input (GLFW key codes) |

Key labels: digits `0`-`9` (and `A`-`F` in hex), `.`, `+ - * /`, `=`, `AC`,
`<-` (backspace), `+/-`, `(` and `)`, `%` (casual: x / 100); sci: `SIN COS
TAN ASIN ACOS ATAN LN LOG E^X 10^X X^2 X^Y SQRT 1/X N! |X| PI E DEG`
(toggles degrees/radians); prog: `HEX DEC OCT BIN AND OR XOR NOT SHL SHR
MOD ROL ROR SW16 SW32 SW64`.

State lives in the hash namespace (callbacks are separate processes):
`klin_mode klin_base klin_angle klin_toks klin_entry klin_cur klin_hascur
klin_hist klin_err klin_open` and, for the GUI, `klin_window klin_canvas
klin_hits klin_modebuttons klin_shift`. `klin_hits` holds one
`[x, y, width, height, label]` vector per key on the canvas.
