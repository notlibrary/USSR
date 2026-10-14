PRAGMA foreign_keys = ON;

CREATE TABLE IF NOT EXISTS trusted_servers (
    id INTEGER PRIMARY KEY,
    url TEXT NOT NULL UNIQUE,
    enabled INTEGER NOT NULL DEFAULT 1 CHECK (enabled IN (0,1)),
    added_at INTEGER NOT NULL DEFAULT (unixepoch())
);

CREATE TABLE IF NOT EXISTS packages (
    name TEXT PRIMARY KEY,
    version TEXT NOT NULL,
    description TEXT NOT NULL DEFAULT '',
    source_url TEXT NOT NULL,
    install_path TEXT NOT NULL,
    manifest_hash TEXT NOT NULL DEFAULT '',
    installed_at INTEGER NOT NULL DEFAULT (unixepoch()),
    updated_at INTEGER NOT NULL DEFAULT (unixepoch())
);

CREATE TABLE IF NOT EXISTS package_files (
    package_name TEXT NOT NULL REFERENCES packages(name) ON DELETE CASCADE,
    relative_path TEXT NOT NULL,
    file_hash TEXT NOT NULL DEFAULT '',
    PRIMARY KEY (package_name, relative_path)
);

CREATE TABLE IF NOT EXISTS repository_index (
    server_url TEXT NOT NULL,
    package_name TEXT NOT NULL,
    version TEXT NOT NULL,
    metadata_url TEXT NOT NULL,
    archive_url TEXT NOT NULL,
    archive_hash TEXT NOT NULL,
    PRIMARY KEY (server_url, package_name, version)
);

CREATE INDEX IF NOT EXISTS repository_index_name
    ON repository_index(package_name);
