# Tver — refinement ideas

**Today:** a two-panel file manager on Petersburg widgets (0.5.0-alpha).
Tab switches panels and Enter opens a folder. Insert marks entries, with
marks remembered per folder. F5 copies (recursively) and F6 moves; both
refuse to overwrite. F7 makes a folder. F8 deletes empty folders and
files only, after a second press to confirm. Held arrows repeat. There
is intentionally no viewer or editor yet, and rename and recursive
delete are out of scope today.

## File operations
- Rename (Shift+F6 or F2) in place
- Recursive delete with a clear confirmation listing counts and sizes
  (to trash first, if Kremlin gets one)
- Overwrite policy dialog: overwrite / skip / rename / overwrite older,
  "for all"
- Progress dialog with cancel for long copies, plus background copies
- Partial-failure report: which items failed and why (TESTPLAN lists
  it)
- Make an empty file (Shift+F4)
- Symlink and hard-link creation, and showing link targets
- Attributes and permissions (chmod, read-only, hidden) editing
- Compare directories (sync view: newer, missing, different by size or
  hash)
- Synchronise directories one-way or both ways
- Archive support: browse ZIP (and `.ural`) files as folders, pack and
  unpack

## Viewing and editing
- F3 viewer: text with encoding detection, hex view, and images through
  Omsk
- F4 opens the file in Mbl (in Mbltishi) or the system editor
- Quick view panel: the other panel shows a preview of the file under
  the cursor

## Navigation
- Drive and root selector (Alt+F1/Alt+F2), plus Windows drive roots and
  UNC paths
- Path entry box: type a path and go
- Directory history (back/forward) and a hotlist of bookmarks (Ctrl+D)
- Quick search: type letters to jump, with filter mode
- Sort by name, extension, size or date, with reverse; directories first
  as an option
- Columns: size, date and attributes in a detail view; a brief view
  with several columns
- Show or hide hidden files
- Folder size calculation (Space on a folder)
- Swap panels (Ctrl+U), and make both panels the same folder
- Mouse: double-click to open, plus drag and drop between panels

## Selection
- Select by pattern (`+` / `-` with `*.su`), invert selection (`*`),
  select all
- Show marked count and total size in the status line

## Integration
- A built-in command line under the panels that runs USSR shell
  commands in the active directory (Ctrl+O to show output)
- Open files with their associations as Kremlin does (`.su` runs, `.dxf`
  opens Tula)
- Run a Kremlin launcher (`.kl`) from Tver
- FTP/SFTP panels, and Ural repository browsing as a virtual file system
- User menu (F2) of custom commands in a `~/.tver` file
- Configurable key bindings

## Look
- Function key bar at the bottom (1 Help ... 10 Quit)
- Colour schemes (classic blue Norton/Far style)
- Icons by file type
- Remember panel paths and settings across runs

## Tests
- Large directories (10 000 entries) for speed
- Permission errors, symlink loops, Windows drive roots (open TESTPLAN
  items)
- Unicode and odd file names (pipes, backslashes) in automated tests
