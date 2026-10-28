# KGB test plan

Automated: `ussr tests/kgb_test.su` (also through russia.su in
`tests/run_all.sh`, checked against `tests/expected/kgb_test.su.txt`).
It makes a base in `tests/kgb-tmp` with `init`, `new` and `add` (paths
creating objects, unique names finding them), writes a category file and a
second object file by hand with headings, `*` and `1.` items, bold and a
note line, then checks:

- `tree`, `ls`, `cats` for the whole base and a branch;
- `show` with two files, a section and inherited facts; `get` own,
  inherited and missing values;
- `where` with `=`, `>`, `>=`, `<=`, `~`, `!=`, `!key`, a missing key, the
  `name` pseudo property, `and`, `or` and `--in`;
- `table` with all keys and chosen ones (a key with a space), `props` for
  the base and a branch, `type`, `is` by name and by path, true and false;
- `links`, `refs`, `find` ignoring case and with `--in`;
- `set` folding two values into one, `edit`, `rm`, a fact number out of
  range, the file's text afterwards; `unset` of an inherited, an own and a
  missing key;
- `new` inside an object (the note that it becomes a category), `check`
  finding a broken link and empty objects, `del`, `stats`;
- `mv` to a new category, refused into an object, back again with the empty
  category removed; `rename`; `dump` of a branch with sections from two
  files; errors for a category given as an object, unknown names, a bare
  unknown name for `add`, an existing path for `new`, an unknown command;
- deleting everything: the category left with facts turns into an object
  (and is said to), empty categories are removed.

By hand for 0.1.0 on Windows: `kgb init` with an absolute path, finding the
root through `.kgb` from a subfolder and through `KGB_HOME`, files with
CRLF line endings and a BOM.
