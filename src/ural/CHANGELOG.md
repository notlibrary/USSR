# Changelog

## 0.2.1-alpha (repository and startup fixes)
- Make FTP uploads address absolute server-root paths rather than paths relative
  to the FTP account's home directory.
- Include the repository index URL and HTTP status in index-fetch errors.
- Seed the per-user database from the bundled `ussr.boxes` template on first
  use, and add `ural db-path` to locate the active client database.

## 0.2.0-alpha (native package manager)
- Added the standalone `ural` executable with SQLite trust/index/install
  records, HTTPS repository lookup, dependency-first install, update and
  uninstall, package search/listing, ZIP pack/extract, FNV-1a checks, and FTP
  upload with a plaintext-credentials warning.
- Added explicit public HTTPS URL input to `pack`; versioned metadata sidecars
  keep package versions independently installable while retaining the
  requested `<name>-box.uno` alias.
- Added CMake dependency discovery and static-library preference for SQLite,
  cURL, OpenSSL, and zlib; added portable manifest/archive tests and the
  remaining initial `highlight` and `fonts` package manifests.
- Updated `ural.su` to call the standalone program through USSR's external
  command interface.

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
