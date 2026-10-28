# KGB

KGB, the Knowledge Giga Base, is a command line manager for a knowledge base
of facts kept in plain Markdown files and folders. There is no database file
and no index: the folders are the database, so any editor, `git`, `grep` or
file manager works on it too.

## The two rules

- A folder **with no subfolders** is an **object**. The Markdown files in it
  (`*.md`) are lists of facts about that object.
- Every **other folder** is a **category**, a classifier: the path from the
  root to an object is its type. `animals/mammals/cat` is a cat that is a
  mammal that is an animal.

```
zoo/                      the root (kgb init zoo makes zoo/.kgb)
  animals/                category
    birds/                category
      birds.md            facts every bird inherits
      penguin/            object
        penguin.md
        habitat.md
      sparrow/            object
        sparrow.md
    mammals/              category
      cat/                object
        cat.md
```

`cat.md` is ordinary Markdown:

```markdown
# cat

- legs: 4
- sound: meow
- hunts [[mouse]]
```

- A **fact** is one list item: `- `, `* `, `+ ` or `1. `.
- A fact `key: value` is also a **property** (`**key**: value` works too).
  A key may repeat: `color: black` and `color: white` are two values.
- `# Title` heads a file; `## Section` and deeper headings group the facts
  under them. Any other line is a note that kgb reads past and keeps.
- Facts in a **category's** `.md` files are **inherited** by every object
  below it. An object's own value of a key hides the inherited one
  (`penguin` says `flies: no` over `birds.md`'s `flies: yes`). The root's
  own files (a README) are not inherited.
- `[[name]]` or `[[path/to/name]]` (also `[[name|label]]`) **links** to
  another object.
- Names starting with `.` are ignored.

What follows from the rules, and kgb tells you when it happens: making an
object inside another object turns the outer one into a category whose
facts the new one inherits; removing the last object of a category that has
facts of its own turns it into an object. Categories left empty by `mv` or
`del` are removed, since an empty folder would be an object without facts.

## Commands

    ussr src/kgb/kgb.su [--kb DIR] COMMAND ...

In the USSR shell, `alias(_): "kgb" "ussr" "/path/to/src/kgb/kgb.su"` in
`moscow.su` makes it `kgb(_): "ls"`; in another shell an alias of its own
makes it `kgb ls`. The examples below use the short form. Objects and categories are named
by their path or by any tail of it that is unique: `cat`, `mammals/cat` and
`animals/mammals/cat` are the same object. Arguments after the fact or value
position are joined with spaces, so quotes are optional there.

| Command | |
|---------|-|
| `init [DIR]` | make DIR (default `.`) a knowledge base: writes `DIR/.kgb` |
| `tree [CAT]` | the classification, objects with their fact counts |
| `ls [CAT]` / `cats [CAT]` | object / category paths, one a line |
| `show OBJ` | the facts numbered, by file and section, then the inherited ones |
| `new PATH` | make an object, its folders and `<name>.md` |
| `add OBJ FACT...` | append a fact; a new `PATH` with a slash is created |
| `edit OBJ N FACT...` | replace fact N (the number `show` gives) |
| `rm OBJ N` | remove fact N |
| `get OBJ KEY` | the values of a property, own or inherited |
| `set OBJ KEY VALUE...` | the object's only value of KEY becomes VALUE |
| `unset OBJ KEY` | remove the object's own KEY facts |
| `find TEXT...` | facts containing TEXT, ignoring case |
| `where COND... [or COND...]` | objects matching the conditions |
| `table [CAT] [KEY...]` | a Markdown table of objects and properties (all keys by default) |
| `props [CAT]` | property keys and how many objects have them |
| `type OBJ` | `animals > mammals > cat` |
| `is OBJ CAT` | `yes` and status 0 when OBJ is classified under CAT, else `no` and 1 |
| `mv OBJ CAT` | reclassify: move the object into CAT (made if new) |
| `rename OBJ NAME` | rename an object in its category |
| `del OBJ` | delete the object's `.md` files and its folder (other files stay) |
| `links OBJ` / `refs OBJ` | objects OBJ links to / facts that link to OBJ |
| `stats` | counts of objects, categories, facts, properties, files |
| `check` | shared names, objects or files without facts, repeated facts, broken or ambiguous links |
| `dump [CAT]` | the whole base, or a branch, as one Markdown document |

Options: `--kb DIR` the root; `--in CAT` limits `find`, `where` and `dump`;
`--file F` the file (`F` or `F.md`) that `add` and `set` write to. Without
`--file`, new facts go to the object's first `.md` file. `--` ends options.

The root is `--kb`, else `$KGB_HOME`, else the nearest folder at or above
the current one with a `.kgb` file, else the current folder, so inside a
base kgb works from any subfolder, like git.

### Conditions

`where` keeps the objects for which every condition of a group holds; `or`
starts another group (`and` between conditions may be written, it changes
nothing). Quote conditions with `<`, `>` or `!` for your shell.

| Condition | Holds when |
|-----------|-----------|
| `key` / `!key` | the object has / has not got the property |
| `key=v` | a value equals v (as numbers if both are, else ignoring case) |
| `key!=v` | no value equals v |
| `key~v` | a value contains v, ignoring case |
| `key>n` `key<n` `key>=n` `key<=n` | a value is a number that compares so |

Inherited values count. When an object has no property of that name,
`name` is its folder name and `path` its path, so `where name=cat` and
`where path~birds` work.

```bash
kgb where legs=2 flies=yes            # animals/birds/sparrow
kgb where "legs>3" or color~gre       # cat, mouse
kgb where "!color" --in animals
kgb is cat mammals && echo mammal
kgb table birds legs flies "lives in"
```

Exit status: 0 done (or found, or true), 1 nothing found (or false, or
`check` found problems), 2 a usage error or a name that is not there.

## Files kgb writes

kgb changes only the lines it is asked to: `edit` and `set` keep a list
item's indent and marker, `rm` drops the one line, `add` appends after the
file's last line (a heading gets a blank line after it). A file kgb rewrites
gets LF line endings and loses a UTF-8 BOM, which it reads past.

Library use, tests: `kgb_main(code): args` runs one command line from a
vector of strings; see [docs/API.md](docs/API.md). `ussr tests/kgb_test.su`
builds a base in `tests/kgb-tmp`, runs every command and removes it.
