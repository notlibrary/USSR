#include "../native/ural_db.h"
#include <stdio.h>
#include <string.h>

static int count_rows(void *ctx, int columns, const char *const *names,
                      const char *const *values)
{
    int *count = (int *)ctx;
    (void)columns; (void)names; (void)values;
    ++*count;
    return 0;
}

#define CHECK(expr) do { if (!(expr)) { \
    fprintf(stderr, "FAIL %s:%d: %s (error: %s)\n", __FILE__, __LINE__, #expr, err); \
    if (stmt != NULL) sqlite3_finalize(stmt); \
    if (legacy != NULL) sqlite3_close(legacy); \
    ural_db_close(&db); remove(path); remove(legacy_path); return 1; } } while (0)

int main(void)
{
    const char *path = "ural-test-boxes.sqlite";
    const char *legacy_path = "ural-test-legacy.sqlite";
    char err[256] = {0};
    ural_db_t db = {0};
    sqlite3 *legacy = NULL;
    sqlite3_stmt *stmt = NULL;
    int rows = 0;
    int has_metadata_hash = 0;
    int step_rc;
    remove(path);
    remove(legacy_path);
    CHECK(ural_db_open(&db, path, err, sizeof err) == 0);
    CHECK(ural_db_add_trusted_server(&db, "https://packages.example.test/repo", err, sizeof err) == 0);
    CHECK(ural_db_add_trusted_server(&db, "https://packages.example.test/repo", err, sizeof err) == 0);
    CHECK(ural_db_add_trusted_server(&db, "http://insecure.example.test", err, sizeof err) != 0);
    CHECK(ural_db_add_trusted_server(&db, "https://user:password@example.test/repo", err, sizeof err) != 0);
    CHECK(ural_db_add_trusted_server(&db, "https://", err, sizeof err) != 0);
    CHECK(ural_db_list_trusted_servers(&db, count_rows, &rows, err, sizeof err) == 0);
    CHECK(rows == 1);
    CHECK(ural_db_remove_trusted_server(&db, "https://packages.example.test/repo", err, sizeof err) == 0);
    rows = 0;
    CHECK(ural_db_list_trusted_servers(&db, count_rows, &rows, err, sizeof err) == 0);
    CHECK(rows == 0);
    rows = 0;
    CHECK(ural_db_list_packages(&db, count_rows, &rows, err, sizeof err) == 0);
    CHECK(rows == 0);
    ural_db_close(&db);
    remove(path);
    CHECK(sqlite3_open(legacy_path, &legacy) == SQLITE_OK);
    CHECK(sqlite3_exec(legacy,
        "CREATE TABLE repository_index(server_url TEXT NOT NULL,package_name TEXT NOT NULL,"
        "version TEXT NOT NULL,metadata_url TEXT NOT NULL,archive_url TEXT NOT NULL,"
        "archive_hash TEXT NOT NULL,PRIMARY KEY(server_url,package_name,version))",
        NULL, NULL, NULL) == SQLITE_OK);
    CHECK(sqlite3_close(legacy) == SQLITE_OK);
    legacy = NULL;
    CHECK(ural_db_open(&db, legacy_path, err, sizeof err) == 0);
    CHECK(sqlite3_prepare_v2(db.handle, "PRAGMA table_info(repository_index)",
                             -1, &stmt, NULL) == SQLITE_OK);
    while ((step_rc = sqlite3_step(stmt)) == SQLITE_ROW) {
        const unsigned char *name = sqlite3_column_text(stmt, 1);
        if (name != NULL && strcmp((const char *)name, "metadata_hash") == 0)
            has_metadata_hash = 1;
    }
    CHECK(step_rc == SQLITE_DONE);
    step_rc = sqlite3_finalize(stmt);
    stmt = NULL;
    CHECK(step_rc == SQLITE_OK);
    CHECK(has_metadata_hash);
    ural_db_close(&db);
    remove(legacy_path);
    puts("ural_db tests: OK");
    return 0;
}
