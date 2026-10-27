# Kremlin API

Include `kremlin.su`. The engine needs no window: `kremlinreset` and the
functions below work in any script, which is how `tests/kremlin_test.su`
drives it. Names in the listing are what `list_dir` returns: folders end
with `/`.

| Function | Arguments | Result |
|---|---|---|
| `kremlinreset(r): root` | Desktop folder | Loads `root/.kremlin` and lists `root`; a missing root is created with the starter set (Welcome.txt, Documents, Apps with launchers, pins) |
| `kremlinopen(r): dir` | Folder | Shows `dir` |
| `kremlinup(r):` | - | Back to the folder above, selecting the folder left; `false` at the root |
| `kremlinrefresh(r):` | - | Lists the folder again, keeping the selection by name |
| `kremlinitems(r):` | - | The names shown, folders first (`sort=type` also groups files by kind) |
| `kremlinselected(r):` / `kremlinselect(r): name` | Name | The selected name or `""` / selects it (`false` when not shown) |
| `kremlinnewfolder(r): name` | Name | `""`, or why not; the new folder is selected |
| `kremlinnewfile(r): name` | Name | `""`, or why not; an empty file |
| `kremlinnewlauncher(r): name command console` | Name, command line, boolean | `""`, or why not; writes `name.kl` |
| `kremlinrename(r): old new` | Listed name, new name | `""`, or why not; a launcher keeps `.kl`, a pinned item stays pinned |
| `kremlindelete(r): name` | Listed name | `""`, or why not; files, and folders only when empty; unpins it |
| `kremlinduplicate(r): name` | Listed file | `""`, or why not; copies to `name copy.ext`, `name copy 2.ext`, ... |
| `kremlinpin(r): name` / `kremlinunpin(r): name` | Listed name | Adds or removes a panel pin and saves the settings |
| `kremlinplan(r): name` | Listed name | What opening it does: `[action target params hidden]`; action `"folder"`, `"launch"` or `"error"` (target is the message) |
| `kremlinopenname(r): name` | Listed name | Opens it (shows a folder, starts the rest); `""` or why not |
| `kremlinaction(r): action target` | Action, its name/pin index | Runs a menu action (`open system edit rename duplicate pin properties delete newfolder newfile newlauncher sort refresh folder-system run desktop wallpaper scale autostart help exit pin-open pin-show pin-remove`, or `path:<file>`) |
| `kremlinautostart(r): on` | Boolean | Writes or removes the sign-in file; a message saying what happened |
| `kremlininitialize(canvas): window` | Window | Adds the canvas, draws it, returns it |
| `kremlindraw(r):` | - | Redraws the canvas |
| `kremlinclick(r): x y` / `kremlindouble(r): x y` / `kremlinmenu(r): x y` | Canvas `"pointer"`, `"double"`, `"menu"` events | Select / open / context menu |
| `kremlinwheel(r): direction` | Canvas `"wheel"` event | Scrolls the icons (or the calendar's month) |
| `kremlinkey(r): key` / `kremlinkeyup(r): key` | Window `"key"`/`"keyup"` events | Keyboard input (GLFW key codes) |
| `kremlinkeybody(r): key` | GLFW key code | One key without the lock and redraw; `true` for a modifier |
| `kremlinfocus(r): focused` | Window `"focus"` event | Forgets held modifier keys |

Helpers the tests use: `km_listing dir sort`, `km_kind name`, `km_label
name`, `km_ext name`, `km_parent path`, `km_launcher_parse text`,
`km_split_command line`, `km_config_apply text` / `km_config_text`,
`km_metrics w h scale indir`, `km_icon_at x y metrics scroll count`,
`km_nav i key count rows cols`, `km_scroll_for i scroll count rows cols`,
`km_key_char key shift`, `km_weekday y m d`, `km_calendar y m`,
`km_type_ahead ch`, `km_run line`.

State lives in the hash namespace (callbacks are separate processes); see
the header of `kremlin.su` for the keys. `km_hits` holds one
`[x, y, width, height, tag]` vector per clickable area drawn (tags `start`,
`clock`, `back`, `pin:N`, `item:N` in a menu, `dialog-ok`, `dialog-cancel`,
`cal-prev`, `cal-next`, `cal-today`, `scroll-left`, `scroll-right`); icons
are found by position (`km_icon_at`). Setting `km_dryrun` to `true` after
`kremlinreset` records launches in `km_launched` instead of starting them.
