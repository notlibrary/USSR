# Changelog

## 0.1.0-alpha (UNO manifest correction)
- Changed `box.uno` from JSON to a single-quoted USSR UNO `Package{...}` literal.
- Updated all seven starter package manifests and the example manifest.
- Documented that the `Package` struct must be registered before decoding.

## 0.1.0-alpha
- Defined `box.uno/1` package metadata format.
- Added SQLite schema for trusted servers, installed packages, package files,
  and repository indexes.
- Added initial package manifests for the seven requested bootstrap packages.
- Added USSR-level API wrapper draft and security requirements.
- Native SQLite, transport, and install commands remain integration work.
