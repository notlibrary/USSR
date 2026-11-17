# Tver — refinement ideas

**Today:** a two-panel file manager on Petersburg widgets (0.5.0-alpha).
Tab switches panels and Enter opens a folder. Insert marks entries, with
marks remembered per folder. F5 copies (recursively) and F6 moves; both
refuse to overwrite. F7 makes a folder. F8 deletes empty folders and
files only, after a second press to confirm. Held arrows repeat. There
is intentionally no viewer or editor yet, and rename and recursive
delete are out of scope today.

**Refine pass 1 (0.6.0-alpha):** the engine in `more.su`, the keys in `keys.su`;
[x] done, [~] partly done. See CHANGELOG.md, `tests/tver_more_test.su` and
`tests/gui/tver_keys_test.su`.

## File operations
- [x] Rename (Shift+F6 or F2) in place
- [x] Recursive delete with a clear confirmation listing counts and sizes
  (to trash first, if Kremlin gets one)
- [~] Overwrite policy dialog: overwrite / skip / rename / overwrite older,
  "for all" — partly: tvm_copy_item takes overwrite, skip, rename or different (by size and content); no dialog, and no "older" (USSR has no file times yet)
- [~] Progress dialog with cancel for long copies, plus background copies — partly: tvm_job, tvm_job_step and a queue give progress and cancel; F5 and F6 still copy at once
- [~] Partial-failure report: which items failed and why (TESTPLAN lists
  it) — partly: tvm_job_report lists what failed and why for jobs
- [x] Make an empty file (Shift+F4)
- Symlink and hard-link creation, and showing link targets
- Attributes and permissions (chmod, read-only, hidden) editing
- [~] Compare directories (sync view: newer, missing, different by size or
  hash) — partly: tvm_compare: only left, only right, size, content; no newer (no file times)
- [~] Synchronise directories one-way or both ways — partly: tvm_sync copies what is missing one way or both; files that differ are reported, not copied
- [~] Archive support: browse ZIP (and `.ural`) files as folders, pack and
  unpack — partly: ZIP: Enter browses it as folders, F5 extracts, Alt+F5 packs (stored); no .ural

## Viewing and editing
- [x] F3 viewer: text with encoding detection, hex view, and images through
  Omsk
- [x] F4 opens the file in Mbl (in Mbltishi) or the system editor
- Quick view panel: the other panel shows a preview of the file under
  the cursor

## Navigation
- [~] Drive and root selector (Alt+F1/Alt+F2), plus Windows drive roots and
  UNC paths — partly: Alt+F1/Alt+F2 step through the drive roots; no UNC paths
- [x] Path entry box: type a path and go
- [x] Directory history (back/forward) and a hotlist of bookmarks (Ctrl+D)
- [~] Quick search: type letters to jump, with filter mode — partly: tvm_jump finds a name by its start, as a library call in more.su, no key or dialog yet
- [~] Sort by name, extension, size or date, with reverse; directories first
  as an option — partly: tvm_sort by name, extension or size, reversed, folders first, as a library call in more.su, no key or dialog yet; no date
- [~] Columns: size, date and attributes in a detail view; a brief view
  with several columns — partly: tvm_detail and tvm_brief, as a library call in more.su, no key or dialog yet
- [x] Show or hide hidden files
- [x] Folder size calculation (Space on a folder)
- [x] Swap panels (Ctrl+U), and make both panels the same folder
- Mouse: double-click to open, plus drag and drop between panels

## Selection
- [x] Select by pattern (`+` / `-` with `*.su`), invert selection (`*`),
  select all
- [~] Show marked count and total size in the status line — partly: marking by pattern, inverting and Ctrl+A show the count and size; Insert keeps its own line

## Integration
- [~] A built-in command line under the panels that runs USSR shell
  commands in the active directory (Ctrl+O to show output) — partly: Ctrl+O starts a command in the panel's folder; its output is not shown
- [x] Open files with their associations as Kremlin does (`.su` runs, `.dxf`
  opens Tula)
- [x] Run a Kremlin launcher (`.kl`) from Tver
- FTP/SFTP panels, and Ural repository browsing as a virtual file system
- [x] User menu (F2) of custom commands in a `~/.tver` file
- [~] Configurable key bindings — partly: "key NAME ACTION" lines are read into the settings; the keys do not follow them yet

## Look
- [x] Function key bar at the bottom (1 Help ... 10 Quit)
- Colour schemes (classic blue Norton/Far style)
- [~] Icons by file type — partly: tvm_kind names a file's kind; the list shows no icons
- Remember panel paths and settings across runs

## Tests
- [~] Large directories (10 000 entries) for speed — partly: 10 000 names sorted and searched in a test; not a folder of 10 000 files
- Permission errors, symlink loops, Windows drive roots (open TESTPLAN
  items)
- [x] Unicode and odd file names (pipes, backslashes) in automated tests

## Search and bulk tools
- [x] Find files (Alt+F7): by name pattern, size, date and text inside,
  with results sent to a panel
- [~] Multi-rename: patterns with counters, case changes, search and
  replace across marked names, with a preview — partly: tvm_rename_plan ([N] [E] [C:3], replace, case) and tvm_rename_apply (swaps, clashes), as a library call in more.su, no key or dialog yet
- [~] Checksums: compute and verify MD5/SHA-256 files for marked items — partly: tvm_sums and tvm_verify (SHA-256, MD5), as a library call in more.su, no key or dialog yet
- [~] Split and join large files — partly: tvm_split and tvm_join_parts, as a library call in more.su, no key or dialog yet
- [~] Compare two files by content, with a side-by-side diff — partly: tvm_diff and tvm_diff_text side by side, as a library call in more.su, no key or dialog yet
- [~] Duplicate finder by size and hash — partly: tvm_dupes by size and SHA-256, as a library call in more.su, no key or dialog yet
- [~] Disk usage view: folders ranked by size — partly: tvm_usage ranks folders by size, as a library call in more.su, no key or dialog yet
- Batch attribute and date changes ("touch")
- [~] Secure delete (overwrite) as a separate, clearly named action — partly: tvm_shred overwrites twice, then deletes, as a library call in more.su, no key or dialog yet

## Panels
- Tabs in each panel
- [~] A tree panel and an info panel (free space, file system, counts) — partly: tvm_info gives a folder's counts and size; no free space, no tree
- Free space and selection totals in a footer per panel
- [x] Filter the listing (`*.su`) and keep the filter per folder
- Colouring by type or age, and custom highlight rules
- Thumbnail view for image folders through Omsk
- Remember the cursor position per folder, as marks are remembered
- [~] Refresh on change (a watcher), and a manual refresh key (Ctrl+R) — partly: Ctrl+R refreshes both panels; no watcher
- [~] Long operations queue: several copies waiting in order — partly: tvm_queue runs jobs in order, as a library call in more.su, no key or dialog yet

## Reach
- [~] A terminal (TUI) version on the raw-terminal primitives, for Moscow's
  console and SSH sessions — partly: tvm_tui draws two panels with box glyphs; nothing runs it in a terminal yet
- KGB bases, Dubna KV stores and disk images (through Stalin's
  readers) as browsable virtual folders
- Plugin panels written in USSR: a function that lists, reads and
  writes entries
- Elevated operations: retry a refused action with administrator
  rights
