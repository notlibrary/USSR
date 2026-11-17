# Packaging with Ural

A USSR package is a folder with a `box.uno` manifest. [Ural](../src/ural/README.md)
packs such folders into `.ural` archives, indexes them in a repository's
`ussr.boxes` file, and installs them with their dependencies.

## The manifest

`box.uno` holds one UNO `Package` value, on one line:

```
'Package{name=`kgb`,version=`0.2.0`,description=`...`,entry=`kgb.su`,dependencies=[`library`,`ryazan>=0.1.0`],files=[`kgb.su`,`README.md`],license=`MIT`,minimum_ussr=`0.1.0`,source=``}'
```

| Field | What |
|-------|------|
| `name` | lower case letters, digits, `-`, `_` and `.` |
| `version` | `N.N.N`, optionally with a pre-release suffix: `0.4.0-alpha` |
| `description` | one line, shown by `ural search` and `ural info` |
| `entry` | the file a user runs or includes; it must be in `files` |
| `dependencies` | package names, each optionally with a version constraint |
| `files` | what goes into the archive (empty: the whole folder) |
| `license` | an SPDX id (`MIT`, `Apache-2.0`, `GPL-3.0-or-later`, ...) or `UNSPECIFIED` |
| `minimum_ussr` | the oldest ussr the package runs on |
| `source` | where the source lives, if not in the repository |

### Versions and constraints

Versions compare number by number (`0.10.0` is newer than `0.9.0`), and a
pre-release comes before its release (`0.4.0-alpha` < `0.4.0-beta` <
`0.4.0`). A dependency is a name or a name plus a constraint:

| Constraint | Means |
|------------|-------|
| `kgb` or `kgb*` | any version |
| `kgb=0.2.0` | exactly 0.2.0 |
| `kgb>=0.2.0`, `>`, `<`, `<=` | compared with 0.2.0 |
| `kgb^1.2.0` | at least 1.2.0 and below 2.0.0 (for 0.x: below the next minor) |
| `kgb~0.2.1` | at least 0.2.1 and below 0.3.0 |
| `kgb>=0.1.0,<1.0.0` | every clause holds |

`ural satisfies 0.4.9 "^0.4.1"` and `ural compare A B` show how the rules
apply.

## Making and checking a package

```bash
ural new mytool              # mytool/: box.uno, mytool.su, README, CHANGELOG, docs, examples, tests
ural check mytool            # the manifest against the folder
ural version minor mytool    # 0.1.0 -> 0.2.0, and a CHANGELOG heading
```

`ural check` reports these as problems (and exits 1):
- a bad version;
- a license that is not an SPDX id;
- a dependency that does not parse;
- a listed file that is missing or points outside the package;
- an entry that is not in `files`.

It warns about `UNSPECIFIED` licenses and files in the folder that
`files` leaves out.

## Publishing

```bash
ural pack TARGET-DIR https://example.org/ussr/    # every box.uno under the current folder
ural upload TARGET-DIR ftp://example.org/ussr/ LOGIN PASSWORD
```

`pack` writes the `.ural` archives, a `NAME-VERSION-box.uno` beside each,
and the `ussr.boxes` index. `upload` sends them over FTP and reads the
index back over the public HTTPS address to check it. The Ural README
covers the server side: chroot, permissions and `local_umask`.

## Installing

```bash
ural server add https://example.org/ussr/
ural install mytool          # dependencies first, newest versions
ural verify                  # installed files against their recorded hashes
ural doctor                  # the database, files and dependency constraints
ural run mytool ARGS         # ussr runs the package's entry
```
