#include "ural_db.h"
#include <stdio.h>
#include <string.h>

static void set_error(char *dst, size_t cap, const char *message)
{
    if (dst == NULL || cap == 0) return;
    snprintf(dst, cap, "%s", message != NULL ? message : "unknown SQLite error");
}

static int exec_sql(sqlite3 *db, const char *sql, char *err, size_t cap)
{
    char *message = NULL;
    int rc = sqlite3_exec(db, sql, NULL, NULL, &message);
    if (rc != SQLITE_OK) {
        set_error(err, cap, message != NULL ? message : sqlite3_errmsg(db));
        sqlite3_free(message);
        return -1;
    }
    return 0;
}

static int has_column(sqlite3 *db, const char *table, const char *column)
{
    sqlite3_stmt *stmt = NULL;
    char sql[128];
    int found = 0;
    snprintf(sql, sizeof sql, "PRAGMA table_info(%s)", table);
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK)
        return -1;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        const unsigned char *name = sqlite3_column_text(stmt, 1);
        if (name != NULL && strcmp((const char *)name, column) == 0) {
            found = 1;
            break;
        }
    }
    sqlite3_finalize(stmt);
    return found;
}

int ural_db_open(ural_db_t *db, const char *path, char *err, size_t cap)
{
    static const char schema[] =
        "PRAGMA foreign_keys=ON;"
        "CREATE TABLE IF NOT EXISTS trusted_servers ("
        " id INTEGER PRIMARY KEY, url TEXT NOT NULL UNIQUE,"
        " enabled INTEGER NOT NULL DEFAULT 1 CHECK(enabled IN (0,1)),"
        " added_at INTEGER NOT NULL DEFAULT (strftime('%s','now')));"
        "CREATE TABLE IF NOT EXISTS packages ("
        " name TEXT PRIMARY KEY, version TEXT NOT NULL,"
        " description TEXT NOT NULL DEFAULT '', source_url TEXT NOT NULL,"
        " install_path TEXT NOT NULL, manifest_hash TEXT NOT NULL DEFAULT '',"
        " installed_at INTEGER NOT NULL DEFAULT (strftime('%s','now')),"
        " updated_at INTEGER NOT NULL DEFAULT (strftime('%s','now')));"
        "CREATE TABLE IF NOT EXISTS package_files ("
        " package_name TEXT NOT NULL REFERENCES packages(name) ON DELETE CASCADE,"
        " relative_path TEXT NOT NULL, file_hash TEXT NOT NULL DEFAULT '',"
        " PRIMARY KEY(package_name,relative_path));"
        "CREATE TABLE IF NOT EXISTS repository_index ("
        " server_url TEXT NOT NULL, package_name TEXT NOT NULL, version TEXT NOT NULL,"
        " metadata_url TEXT NOT NULL, archive_url TEXT NOT NULL, archive_hash TEXT NOT NULL,"
        " metadata_hash TEXT NOT NULL DEFAULT '',"
        " PRIMARY KEY(server_url,package_name,version));"
        "CREATE TABLE IF NOT EXISTS repository_config ("
        " server_url TEXT PRIMARY KEY, public_url TEXT NOT NULL);"
        "CREATE INDEX IF NOT EXISTS repository_index_name ON repository_index(package_name);";
    int rc;
    if (db == NULL || path == NULL || path[0] == '\0') {
        set_error(err, cap, "database path is required");
        return -1;
    }
    db->handle = NULL;
    rc = sqlite3_open_v2(path, &db->handle,
                         SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, NULL);
    if (rc != SQLITE_OK) {
        set_error(err, cap, db->handle != NULL ? sqlite3_errmsg(db->handle) : "cannot open database");
        if (db->handle != NULL) sqlite3_close(db->handle);
        db->handle = NULL;
        return -1;
    }
    sqlite3_busy_timeout(db->handle, 5000);
    if (exec_sql(db->handle, schema, err, cap) != 0) {
        ural_db_close(db);
        return -1;
    }
    rc = has_column(db->handle, "repository_index", "metadata_hash");
    if (rc < 0 || (rc == 0 && exec_sql(db->handle,
        "ALTER TABLE repository_index ADD COLUMN metadata_hash TEXT NOT NULL DEFAULT ''",
        err, cap) != 0)) {
        if (rc < 0)
            set_error(err, cap, sqlite3_errmsg(db->handle));
        ural_db_close(db);
        return -1;
    }
    return 0;
}

void ural_db_close(ural_db_t *db)
{
    if (db != NULL && db->handle != NULL) {
        sqlite3_close(db->handle);
        db->handle = NULL;
    }
}

static int server_change(ural_db_t *db, const char *url, int add,
                         char *err, size_t cap)
{
    sqlite3_stmt *stmt = NULL;
    const char *sql = add
        ? "INSERT INTO trusted_servers(url) VALUES(?1) ON CONFLICT(url) DO UPDATE SET enabled=1"
        : "DELETE FROM trusted_servers WHERE url=?1";
    int rc;
    if (db == NULL || db->handle == NULL || url == NULL || url[0] == '\0') {
        set_error(err, cap, "database and non-empty URL are required");
        return -1;
    }
    /* Reject whitespace/control characters and non-HTTPS repository URLs. */
    if (add) {
        const unsigned char *p = (const unsigned char *)url;
        if (strncmp(url, "https://", 8) != 0) {
            set_error(err, cap, "trusted repository URL must use https://");
            return -1;
        }
        if (url[8] == '\0' || url[8] == '/' || strchr(url, '?') != NULL ||
            strchr(url, '#') != NULL || strchr(url + 8, '@') != NULL) {
            set_error(err, cap, "trusted repository URL must have a host and no credentials, query, or fragment");
            return -1;
        }
        for (; *p; ++p) {
            if (*p <= 0x20 || *p == 0x7f) {
                set_error(err, cap, "repository URL contains whitespace or control characters");
                return -1;
            }
        }
    }
    rc = sqlite3_prepare_v2(db->handle, sql, -1, &stmt, NULL);
    if (rc == SQLITE_OK) rc = sqlite3_bind_text(stmt, 1, url, -1, SQLITE_TRANSIENT);
    if (rc == SQLITE_OK) rc = sqlite3_step(stmt);
    if (rc != SQLITE_DONE) {
        set_error(err, cap, sqlite3_errmsg(db->handle));
        sqlite3_finalize(stmt);
        return -1;
    }
    sqlite3_finalize(stmt);
    return 0;
}

int ural_db_add_trusted_server(ural_db_t *db, const char *url, char *err, size_t cap)
{ return server_change(db, url, 1, err, cap); }

int ural_db_remove_trusted_server(ural_db_t *db, const char *url, char *err, size_t cap)
{ return server_change(db, url, 0, err, cap); }

static int list_query(ural_db_t *db, const char *sql, ural_row_callback cb,
                      void *context, char *err, size_t cap)
{
    sqlite3_stmt *stmt = NULL;
    int rc, i, count;
    if (db == NULL || db->handle == NULL || cb == NULL) {
        set_error(err, cap, "database and callback are required");
        return -1;
    }
    rc = sqlite3_prepare_v2(db->handle, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) { set_error(err, cap, sqlite3_errmsg(db->handle)); return -1; }
    count = sqlite3_column_count(stmt);
    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
        const char **names = (const char **)sqlite3_malloc((size_t)count * sizeof(*names));
        const char **values = (const char **)sqlite3_malloc((size_t)count * sizeof(*values));
        int callback_result;
        if (names == NULL || values == NULL) {
            sqlite3_free((void *)names); sqlite3_free((void *)values);
            set_error(err, cap, "out of memory"); sqlite3_finalize(stmt); return -1;
        }
        for (i = 0; i < count; ++i) {
            names[i] = sqlite3_column_name(stmt, i);
            values[i] = (const char *)sqlite3_column_text(stmt, i);
            if (values[i] == NULL) values[i] = "";
        }
        callback_result = cb(context, count, names, values);
        sqlite3_free((void *)names); sqlite3_free((void *)values);
        if (callback_result != 0) { sqlite3_finalize(stmt); return callback_result; }
    }
    if (rc != SQLITE_DONE) set_error(err, cap, sqlite3_errmsg(db->handle));
    sqlite3_finalize(stmt);
    return rc == SQLITE_DONE ? 0 : -1;
}

int ural_db_list_packages(ural_db_t *db, ural_row_callback cb, void *ctx,
                          char *err, size_t cap)
{
    return list_query(db, "SELECT name,version,description,source_url,install_path,installed_at,updated_at FROM packages ORDER BY name", cb, ctx, err, cap);
}

int ural_db_list_trusted_servers(ural_db_t *db, ural_row_callback cb, void *ctx,
                                 char *err, size_t cap)
{
    return list_query(db, "SELECT url,enabled,added_at FROM trusted_servers ORDER BY url", cb, ctx, err, cap);
}
