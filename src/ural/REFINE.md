# Ural — refinement ideas

**Today:** a standalone native package manager (C, with SQLite, libcurl,
OpenSSL and zlib).
- Packages are `box.uno` manifests and `.ural` ZIP archives, indexed in
  a repository `ussr.boxes` SQLite file.
- Commands: `init`, `db-path`, `server add/list`, `search`, `install`
  (dependencies first, newest version), `update` (keeping a backup),
  `list`, `uninstall` (owned files only), `pack` and `upload` (plain
  FTP).
- HTTPS is verified with a trusted-server list, and integrity is checked
  with FNV-1a (not cryptographic).
- `ural.su` is a thin wrapper that calls the program.

## Security
- Signed repositories: an Ed25519 signature of `ussr.boxes` and a
  publisher key per package, checked on install
- SHA-256 hashes in place of (or next to) FNV-1a
- A trust-on-first-use key pin per server
- Key rotation and revocation lists
- Reproducible packs (sorted ZIP entries, fixed times) so hashes match
  across machines
- `ural audit`: list installed packages with known advisories

## Dependencies
- Version constraints in `dependencies` (`ryazan>=0.1.0`, `^0.4`,
  `~0.2.1`) and a real resolver with conflict messages
- `minimum_ussr` enforced against the running ussr version
- A lock file (`box.lock`) for a project: exact versions and hashes
- Optional and dev dependencies
- `ural tree` showing the dependency graph, and `ural why PKG`
- Install from a local `.ural` file or a directory (`ural install
  ./src/kgb`)
- Install a specific version (`ural install kgb@0.1.0`), with pin and
  hold
- Uninstall of packages nothing else needs (`ural autoremove`)

## Project workflow
- `ural new NAME` scaffolds a package (box.uno, README, CHANGELOG,
  docs/API.md, examples/hello.su, tests/TESTPLAN.md, matching the
  layout most subprojects use)
- `ural check` validates a manifest: the `files` list exists, `entry`
  is in it, there are no stray files, and the version was bumped
- `ural run PKG [args]` runs a package's entry with its include paths set
- Include-path integration: `$include <kgb>` resolving to installed
  packages
- `ural publish` = pack + upload + verify in one step
- Version bump helper (`ural version patch|minor|major`) updating
  box.uno and CHANGELOG

## Transport
- FTPS/SFTP or HTTPS PUT/WebDAV upload in place of plain FTP
- Mirrors and fallback servers, plus a download cache with resume
- Parallel downloads
- Offline mode from the cache
- Proxy settings

## Metadata
- Fill in `license` (every manifest says UNSPECIFIED) and validate
  SPDX ids
- `authors`, `homepage`, `repository` and `keywords` fields, for search
- `ural info PKG`: README excerpt, versions, files, size
- A changelog shown on `update`
- A static HTML index of a repository for browsing

## Interface
- `--json` output for every command
- Progress bars, plus quiet and verbose levels
- Shell completion for package names
- A Petersburg GUI (or a Kremlin app) for browsing and installing
- Do more of the work in `ural.su` (or port Ural to USSR) so Moscow can
  use it without the native program

## Tests
- End-to-end tests with a local HTTPS repository (Ukraine and Ryazan
  serving `ussr.boxes`)
- Corruption tests: a bad hash, a truncated ZIP, path traversal in an
  archive, a manifest naming files outside the package
- Upgrade/downgrade/rollback tests
