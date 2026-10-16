# Ural package manager

Ural is a standalone cross-platform package manager and repository maintenance
tool. It does not run a server daemon. The same `ural` executable installs
packages, edits the local trust list, builds repository artifacts, and uploads
them.

## Package format

Each package directory contains `box.uno`, a UTF-8 USSR UNO literal with a
`Package` object:

```text
'Package{name=`ukraine`,version=`0.1.0`,description=`HTTP server and web framework for USSR`,entry=`ukraine.su`,dependencies=[`library`],files=[],license=`UNSPECIFIED`,minimum_ussr=`0.1.0`,source=``}'
```

`name`, `version`, and `dependencies` are required. Package names and versions
use lowercase letters, digits, dots, underscores, or hyphens; names cannot
contain path separators. `entry` is optional and, when present, must name a
file included in the package archive. The archive is a ZIP file named
`<name>-<version>.ural`; the repository also contains `<name>-box.uno` and
`ussr.boxes`.

The `ussr.boxes` SQLite database contains the trusted repository list, package
index, installed-package records, and each installed package's owned files.
The repository index records absolute HTTPS URLs and 64-bit FNV-1a hashes for
both the manifest and archive. FNV-1a detects accidental corruption; it is
**not cryptographic authentication**. HTTPS certificate verification and an
explicit trusted-server list are required, but repository signatures and
publisher identity are not implemented.

## Commands

```text
ural init
ural server add https://packages.example.org/USSR
ural server list
ural search ukraine
ural install ukraine russia
ural update ukraine
ural list
ural uninstall ukraine
ural pack ./repository https://packages.example.org/USSR
ural upload ./repository ftp://packages.example.org/var/www/USSR LOGIN PASSWORD
```

`pack` recursively searches for package manifests, creates `.ural` archives
and `*-box.uno` sidecars in the target directory, then updates that
directory's `ussr.boxes`. The public HTTPS URL is mandatory and independent of
the FTP filesystem path. `upload` transfers only top-level `.ural`,
`*-box.uno`, and `ussr.boxes` files from the target directory.

`install` reads only enabled trusted repositories, downloads their SQLite
index over verified HTTPS, chooses the newest available version, checks its
FNV-1a hashes, validates the manifest and archive paths, installs dependencies
first, and records every extracted file. Packages are installed per user under
`~/.ussr/packages` on POSIX or `%LOCALAPPDATA%\USSR\packages` on Windows.
Updates preserve the previous directory under the per-user `backups` folder;
Ural intentionally does not recursively delete untracked package data.
`uninstall` removes only recorded package-owned files and leaves unrelated
files untouched. Package scripts are never executed.

`upload` uses **plain FTP by design**. It prints a warning because FTP exposes
credentials and package contents on the network. Prefer a protected network
or a later secure transport when handling real credentials. Passwords passed
as command-line arguments may also be visible to local process inspectors.

## USSR source wrapper

Include `ural.su` after installing `ural` and placing it on `PATH`:

```ussr
$include "ural.su"

install(code): "ukraine russia"
```

The wrapper invokes the standalone executable through USSR's external-command
facility; it does not add a package-specific VM opcode or interpreter daemon.
Repository maintenance is available through `pack(code): target_dir public_url`
and `upload(code): target_dir ftp_url login password`.

## Build

The native program requires SQLite, cURL, OpenSSL, and zlib development
libraries. CMake is the recommended cross-platform build:

```sh
cmake -S src/ural -B build/ural -DURAL_STATIC_DEPS=ON
cmake --build build/ural
ctest --test-dir build/ural --output-on-failure
```

`URAL_STATIC_DEPS=ON` prefers static libraries. Supply the platform's static
dependency prefix through `CMAKE_PREFIX_PATH` (and, where needed,
`SQLite3_LIBRARY`) to produce a static-dependency build. The project build
files for the USSR interpreter do not automatically build Ural; this package
has its own CMake project. A Makefile is also provided for POSIX environments
with `pkg-config`.

The Windows MSI and release archives contain the `ural` executable and a blank,
initialized `ussr.boxes` client database. `make dist` builds Ural and adds both
files to the Automake source distribution. The Linux release bundle is produced
by `make -f ussr-ubuntu.mk dist`; it reads the default version from
`configure.ac` and writes `ussr-linux-bin-<version>.tar.gz`. Set
`VERSION=<version>` to override it for a release tag. The bundled database is a
clean starting template, not a populated repository index; Ural creates its
working database under the user's per-user state directory on first use.

## Initial package collection

The bootstrap list contains nine package manifests: `ural`, `russia`, `mbl`,
`ukraine`, `library`, `docs`, `tests`, `highlight`, and `fonts`. These manifests
define the initial package catalog; they do not claim that all nine archives
have been published or that placeholder package contents are complete.

## Tests

The CTest suite covers SQLite initialization and trusted-server rules, package
manifest parsing and validation, archive round-tripping, FNV-1a, and unsafe
archive path rejection. Network operations require a configured HTTPS or FTP
repository and are not exercised against public servers by the unit tests.
