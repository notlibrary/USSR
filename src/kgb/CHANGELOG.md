# Changelog

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
