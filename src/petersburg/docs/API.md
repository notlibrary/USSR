# Petersburg API

Load the API with `$include "petersburg.su"`. Native functions are dispatched
inside the interpreter; scripts hold integer handles rather than C pointers.
All creation functions return a handle. Mutators return integer zero, getters
return their value, and invalid arguments/handles fail the USSR command with
a diagnostic.

| Function | Arguments | Result |
|---|---|---|
| `createwindow(title, width, height)` | String, positive integers (160..8192 by 120..8192) | Window handle |
| `label(window, text)` | Window handle, string | Label handle |
| `button(window, text)` | Window handle, string | Button handle |
| `textbox(window, initial_text)` | Window handle, string | Text-box handle |
| `checkbox(window, text)` | Window handle, string | Checkbox handle |
| `listbox(window, items)` | Window handle, pipe-separated string | List handle |
| `combobox(window, items)` | Window handle, pipe-separated string | Combo-box handle |
| `image(window, path)` | Window handle, P6 PPM path | Image handle |
| `pack(widget, expand)` | Handle, boolean | Zero; expands vertically when true |
| `settext(widget, text)` / `gettext(widget)` | Handle and optional string | Zero / current string |
| `getselection(widget)` | List or combo-box handle | Current selected item |
| `setchecked(widget, checked)` / `getchecked(widget)` | Checkbox handle and boolean | Zero / boolean |
| `on(widget, event, callback_name)` | Handle, `"click"` or `"change"`, string | Zero |
| `show(window)` / `close(window)` | Window handle | Zero |
| `destroy(widget)` | Widget or window handle | Zero |
| `run()` | No arguments | Zero; activates the scheduler-integrated UI |
| `messagebox(title, text)` | Two strings | Zero after the modal dialog closes |

Widgets are laid out as a vertical flow in creation order with fixed margins
and spacing. Their widths follow the window size. Expanded widgets share
positive remaining vertical space. The current renderer uses OpenGL 2.1
compatibility drawing, with a built-in bitmap font; text input accepts ASCII.

The `on` callback must name a one-parameter USSR definition. It receives the
widget handle. Native GLFW callbacks only enqueue events; `scheduler.c` drains
them between VM instruction slices and schedules handlers through
`ussr_sched_spawn_func`.

## Image format

`image` currently loads binary P6 PPM data with a maximum component value of
255. Other image formats are rejected instead of silently ignored.
