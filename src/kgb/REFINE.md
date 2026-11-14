# KGB — refinement ideas

**Today:** a Markdown knowledge base kept in folders. A leaf folder is an
object, its `.md` list items are facts, `key: value` facts are
properties, and inner folders are categories whose facts objects inherit.
`[[links]]` join objects. There are 29 commands (`init tree ls cats show
new add edit rm get set unset find where table props type is mv rename del
links refs stats check dump`). `where` takes conditions joined by
`and`/`or`.

## Query language
- Parentheses and `not` in `where`: `where (legs>2 or flies=yes) and
  not extinct`
- Regex matching: `key~/^meo+w$/i`
- Conditions through links: `where hunts->legs=4`
- Aggregates: `count`, `sum`, `avg`, `min`, `max`, `group by KEY`
- `sort by KEY [desc]`, `limit N`, `offset N`
- Units-aware comparison: `mass>2kg` against `mass: 2500 g`
- Date values (`born: 1961-04-12`) with date comparisons
- Saved queries: `kgb query save NAME ...` and `kgb q NAME`
- A small SQL-like front end: `kgb sql "select name, legs from animals
  where legs > 2"`

## Data model
- Typed properties declared in a category file (`legs: int`, `born:
  date`, `site: url`), checked by `check`
- Required properties per category (`! legs`), reported when missing
- Multiple inheritance through `is: [[other/category]]` facts (tags)
- Aliases for objects (`aka: Felis catus`) that names resolve through
- Inverse relations declared once (`hunts` <-> `hunted by`) and shown
  in `refs`
- Units and quantities as first-class values
- Lists inside a value (`colors: black, white`), split on request
- Fact provenance: `source:` per fact or per file, shown by `show
  --sources`

## Output and export
- `--format json|csv|tsv|uno|yaml` for `table`, `where`, `get` and `dump`
- Export to a static HTML site with link navigation, served through
  Ukraine
- Graphviz/DOT export of links and the category tree
- `kgb diff A B`: how two objects differ, property by property
- `kgb compare A B C`: a side-by-side table
- `kgb random [CAT]` for flash-card study

## Editing
- `kgb import` from CSV or JSON: one row per object, columns as
  properties
- `kgb cp OBJ PATH` to clone an object
- Batch edits: `kgb set --where "legs=4" kind quadruped`
- An interactive picker for an ambiguous name, in place of an error
- `kgb open OBJ` in Mbl or `$EDITOR`
- Rename a link target everywhere when an object is renamed (`rename
  --relink`)
- Undo of the last write through a `.kgb/journal`

## Scale and speed
- An optional cache index (`.kgb/index.uno`) rebuilt when file times
  change, for bases with 10 000+ objects
- Full-text search with ranking (TF-IDF), stemming for Russian and
  English
- Watch mode: `kgb watch` re-runs a query when files change

## Integration
- A Ukraine HTTP API (`kgb serve`): GET objects, run queries, POST facts
- A Kremlin/Petersburg GUI browser: the tree on the left, facts on the
  right
- Lenin hook: ask a model to fill in missing properties, with every fact
  it adds marked as a proposal
- Git-aware: `kgb log OBJ` shows history from git when the base is a repo
- Shell completion of object names and keys for the USSR REPL

## Quality
- `check --fix` for repeated facts and broken links with an obvious
  target
- Lint rules: property key spelling (case, spaces), near-duplicate keys
- Locale-aware sorting (Cyrillic collation)
- Tests for large generated bases and for performance

## Reasoning over facts
- Rules: `kgb rule "X hunts Y and Y is small -> X is predator"` that
  derive facts on query, shown as derived
- Transitive relations declared once (`part of`, `located in`), used
  by `where` and `is`
- Path queries: how two objects are linked (`kgb path cat mouse`)
- Contradiction checks: two values for a key declared single-valued, or
  an own value clashing with a rule
- Confidence per fact (`?` suffix or `certainty:`), with filters on it
- Time-scoped facts (`capital: Bonn [1949..1990]`) and "as of" queries

## Markdown handling
- YAML front matter read as properties
- Markdown tables read as objects-by-row or properties-by-column
- Nested list items as sub-facts of their parent fact
- Checkbox items (`- [ ]`, `- [x]`) as boolean properties
- Images and attachments in an object folder listed by `show` (`del`
  already leaves other files alone)
- Obsidian and Logseq compatibility notes: `[[link#section]]`, tags
  (`#tag`), block references

## Housekeeping
- `kgb merge A B`: fold one object into another, relinking references
- `kgb split OBJ` into several objects by section
- `kgb orphan`: objects nothing links to; `kgb hubs`: the most linked
- Duplicate detection by similar names and overlapping properties
- Templates per category: `new` fills in the expected property keys
- A changelog of edits (`.kgb/log`) with who and when
- Read-only mode and a lock file for shared folders
- Config in `.kgb` (default file name, key spelling rules, ignored
  folders), since the file exists but is empty of settings
