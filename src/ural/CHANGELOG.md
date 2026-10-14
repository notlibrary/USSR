# Changelog

## 0.1.1-alpha (local database layer)
- Added a reusable SQLite C layer for opening and initializing `ussr.boxes`.
- Added trusted HTTPS repository add/remove operations and read-only listing APIs.
- Added a native unit test for database initialization, duplicate trust entries,
  rejection of plain HTTP repositories, removal, and empty package listing.
- Kept interpreter command registration and package installation explicitly out
  of scope for this increment.

## 0.1.0-alpha (UNO manifest correction)
- Changed `box.uno` from JSON to a single-quoted USSR UNO `Package{...}` literal.
- Updated all seven starter package manifests and documented type registration.
- Added the SQLite schema and drafted the USSR-level API.
