# KGB API

`$include "src/kgb/kgb.su"` (relative to the including file). Its `init` is
inside `$ifdef __MAIN__`, so including it adds functions only.

## Running commands

| Call | Result |
|------|--------|
| `kgb_main(code): args` | runs one command line, a vector of strings without the program name (`["--kb" "zoo" "where" "legs=2"]`); prints what the command prints, errors on stderr as `kgb: ...`; returns 0, 1 or 2 like the command line |
| `kgb_usage(_):` | prints the help |

`kgb_main` sets the globals below, finds the root and scans the tree on
every call, so a program may call it again after changing files itself.

## After a scan

| Global | |
|--------|-|
| `kgb_root` | the root folder |
| `kgb_objects` | vector of object paths below the root, in tree order |
| `kgb_cats` | vector of category paths |

`kgb_scan(_):` refills `kgb_objects` and `kgb_cats` from `kgb_root`.

| Call | Result |
|------|--------|
| `kgb_object(p): name` | an object's path from its path or a unique tail of it, or `null` (the reason is printed) |
| `kgb_category(p): name` | the same for categories; `""` is the root |
| `kgb_objects_in(v): cat` | the objects under a category |
| `kgb_ancestors(v): path` | the categories above, nearest first, without the root |
| `kgb_facts(v): path` | the facts of one folder: a vector of `KgbFact` |
| `kgb_values(v): obj key` | the object's values of key, else the nearest category's |
| `kgb_keys(v): obj` | its property keys, own and inherited, in order |
| `kgb_links_of(v): text` | the `[[link]]` targets in a fact |

`KgbFact` fields: `owner` (folder path), `file` (file name), `line` (0-based
line number in the file), `text` (without the list marker), `section` (the
`##` heading above it, or `""`).

| Call | Result |
|------|--------|
| `kgb_fact_text(t): line` | a list item's text, or `null` |
| `kgb_key(k): text` | a property's key in lower case, or `null`; `kgb_value(v): text` its value |
| `kgb_append_fact(_): obj text` | appends `- text` to the object's target file |
| `kgb_replace_fact(_): fact text` / `kgb_remove_fact(_): fact` | rewrite or drop that fact's line |
