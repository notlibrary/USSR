# Ural native tests

Run `cmake -S src/ural -B build/ural`, `cmake --build build/ural`, and
`ctest --test-dir build/ural --output-on-failure`, or use `make test` where
`pkg-config` is available.

- `test_ural_db` checks database/schema initialization, trusted HTTPS URL
  validation, duplicate insertion/removal, legacy schema migration, and the
  empty installed-package list.
- `test_ural_manifest` checks valid UNO package fields, dependency parsing,
  duplicate fields, and invalid package names.
- `test_ural_archive` checks ZIP packing/extraction, empty files, FNV-1a, and
  rejection of path traversal in archive entries.

Live HTTPS/FTP tests are intentionally not run as part of the unit suite;
they require a configured test repository. FTP uploads print a warning because
credentials and file contents are transmitted without encryption.
