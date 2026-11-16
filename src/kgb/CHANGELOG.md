# Changelog

## 0.2.0
Refine pass 1 from REFINE.md; the new code is `more.su`.
- A query engine behind `where`: `and`/`or`/`not` with parentheses,
  regular expressions (`key~/re/i`), link paths (`hunts->color=grey`),
  quantities with units (`mass>2kg` against `mass: 2500 g`), dates,
  `sort by KEY [desc]`, `limit`, `offset`, `show K1,K2`, aggregates
  (`count`, `sum`, `avg`, `min`, `max`) and `group by`. Old `where`
  queries give the same answers.
- `--format text|json|csv|tsv` for query results.
- Saved queries (`query save NAME ...`, `query ls`, `query rm`, `q NAME`)
  in `.kgb-queries`, and `sql "select ... from CAT where ... order by ...
  limit N"` with `count(*)`, `sum()`, `avg()`, `min()`, `max()`.
- Objects: `diff`, `compare`, `random`, `cp`, `merge` (relinks
  references), `set --where EXPR KEY VALUE`, `aka:` aliases resolving
  names.
- `import` from CSV (quoted fields) or JSON arrays of objects.
- Links: `orphans`, `hubs [N]`, `path A B`, `closure OBJ KEY`.
- Exports: `dot` (Graphviz) and `html DIR` (a static site).
- Quality: `lint` (near-duplicate keys) and `validate` (category
  schemas `!key: type`, required and typed).
- Markdown: YAML front matter and checkbox items read as properties.
- History, when `.kgb` says `journal: on` (or `KGB_JOURNAL=1`): `undo`
  of the last write and `log`.
- `tests/kgb_more_test.su` covers it all (golden output); the old
  `tests/kgb_test.su` output is unchanged.

## 0.1.0
- First version: a Markdown knowledge base on folders. A folder without
  subfolders is an object, its `.md` list items are facts, `key: value`
  facts are properties, other folders are categories whose facts objects
  inherit, `[[name]]` links objects.
- Commands: `init tree ls cats show new add edit rm get set unset find where
  table props type is mv rename del links refs stats check dump`; `where`
  takes conditions (`key`, `!key`, `=`, `!=`, `~`, `<`, `>`, `<=`, `>=`)
  joined by `and`/`or`; `--kb`, `--in`, `--file`; the root found through
  `$KGB_HOME` or a `.kgb` file above the current folder.
