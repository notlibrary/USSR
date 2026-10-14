#include "../native/ural_db.h"
#include <stdio.h>
#include <string.h>
#include <unistd.h>

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
    ural_db_close(&db); unlink(path); return 1; } } while (0)

int main(void)
{
    const char *path = "ural-test-boxes.sqlite";
    char err[256] = {0};
    ural_db_t db = {0};
    int rows = 0;
    unlink(path);
    CHECK(ural_db_open(&db, path, err, sizeof err) == 0);
    CHECK(ural_db_add_trusted_server(&db, "https://packages.example.test/repo", err, sizeof err) == 0);
    CHECK(ural_db_add_trusted_server(&db, "https://packages.example.test/repo", err, sizeof err) == 0);
    CHECK(ural_db_add_trusted_server(&db, "http://insecure.example.test", err, sizeof err) != 0);
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
    unlink(path);
    puts("ural_db tests: OK");
    return 0;
}
