# Ural (`ural.su`) — USSR package manager bootstrap

Ural is the planned package manager for USSR. Its local package database is
`ussr.boxes` (SQLite); each package directory contains `box.uno` metadata.
The initial package set is `ural`, `russia`, `mbl`, `ukraine`, `library`,
`docs`, and `tests`.

## Package metadata

`box.uno` is a UTF-8 **USSR UNO literal**, not JSON. Each manifest is a
single-quoted UNO object literal whose type is `Package`; strings inside UNO
objects use backticks. The `Package` struct must be registered in USSR before
the literal is decoded. The fields are `name`, `version`, `description`,
`entry`, `dependencies` (a vector of package-name strings), `files` (a vector),
`license`, `minimum_ussr`, and `source`.

Example:

```text
'Package{name=`ukraine`,version=`0.1.0`,description=`HTTP server and web framework for USSR`,entry=`ukraine.su`,dependencies=[`library`],files=[],license=`UNSPECIFIED`,minimum_ussr=`0.1.0`,source=``}'
```

Ural should register the manifest type before decoding a manifest, using the
USSR UNO/struct facilities. Package names must match the directory and use a
normalized lowercase identifier.

## Database

`sql/ussr.boxes.sql` creates:
- `trusted_servers`: trusted repository URL allow-list.
- `packages`: installed package registry.
- `package_files`: files owned by each installed package.
- `repository_index`: package/version metadata, archive URL, and archive hash.

The `repository_index.archive_hash` field is required before an archive can be
installed. The installer must verify the archive bytes before extraction.
The server list is an explicit trust list, not merely a cache of URLs.

## USSR API and current implementation status

The desired source-level interface is:

```ussr
$include "ural.su"
install(code): "ukraine russia"
uninstall(code): "ukraine"
update(code): "ukraine"
```

This archive is **the first design/bootstrap scaffold**, not a finished,
working package manager. The public wrappers in `ural.su` call `ural_*`
native commands that must be integrated into the USSR interpreter. This
package does not pretend those commands are already stock built-ins, and it
does not yet fetch or extract remote archives.

## Implementation order

1. Add native SQLite bridge commands (`ural_init`, `ural_install`, etc.) to
   the interpreter, linked against SQLite (`-lsqlite3` on POSIX; the Windows
   build must link an available SQLite library).
2. Add cross-platform HTTPS transport using the project's `ukraine.su` socket
   / HTTP stack or a separately approved TLS backend. Plain sockets alone
   must not be used for trusted package downloads.
3. Implement a repository index format and signature/hash verification.
4. Implement safe archive extraction with path traversal protections.
5. Implement install/uninstall/update transactions and rollback.
6. Add end-to-end tests using a local test repository before enabling remote
   repositories by default.

## Security requirements

- Do not execute package scripts during installation.
- Reject paths that escape the target package directory (`..`, absolute paths,
  drive prefixes, symlink escapes).
- Verify package archive hash and, before public deployment, repository
  signatures using pinned trust keys.
- Use SQLite transactions for registry changes.
- Never remove files that are not recorded as owned by the package.
- Do not silently downgrade packages; require an explicit version selection.
- Avoid running as administrator/root for ordinary package operations.

## Bootstrap package set

| Name | Purpose |
|---|---|
| `ural` | USSR package manager |
| `russia` | Bootstrapped USSR interpreter |
| `mbl` | Vi-like console text editor |
| `ukraine` | HTTP server and web framework |
| `library` | USSR standard library |
| `docs` | USSR manual, specifications, and design documents |
| `tests` | USSR test collection |

Versions and dependency lists in these initial `box.uno` files are placeholders
to establish the metadata contract, not declarations that all packages have
already been published.

## 0.1.1-alpha development increment: local database layer

This increment adds `native/ural_db.c` / `native/ural_db.h`, a small SQLite
layer for opening/initializing `ussr.boxes`, managing the trusted HTTPS server
allow-list, and listing installed packages or trusted servers through callbacks.
The layer uses prepared statements for URL mutations and rejects non-HTTPS
URLs when adding trusted repositories. It is intentionally independent of the
interpreter so it can be tested before the USSR command-dispatch integration.

Build and run the native unit test with `make test` (requires a C compiler,
SQLite development headers/library, and `pkg-config` optionally). On Windows,
configure `SQLITE_CFLAGS` and `SQLITE_LIBS` for the SQLite distribution in use.

**Scope note:** this is a tested local-DB foundation, not yet a complete package
manager. The `ural_*` commands called by `ural.su` still need to be registered
in USSR's command dispatcher and wired to this layer. Network fetching, UNO
manifest decoding, archive verification/extraction, dependency resolution, and
transactional install/uninstall/update remain future work. No remote package is
installed by this increment.
