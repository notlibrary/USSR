#ifndef URAL_DB_H
#define URAL_DB_H

#include <stddef.h>
#include <sqlite3.h>

typedef struct ural_db {
    sqlite3 *handle;
} ural_db_t;

typedef int (*ural_row_callback)(void *context, int columns,
                                 const char *const *names,
                                 const char *const *values);

/* Return 0 on success, -1 on error. Error text is copied to err when provided. */
int ural_db_open(ural_db_t *db, const char *path, char *err, size_t err_size);
void ural_db_close(ural_db_t *db);
int ural_db_add_trusted_server(ural_db_t *db, const char *url,
                               char *err, size_t err_size);
int ural_db_remove_trusted_server(ural_db_t *db, const char *url,
                                  char *err, size_t err_size);
int ural_db_list_packages(ural_db_t *db, ural_row_callback callback,
                          void *context, char *err, size_t err_size);
int ural_db_list_trusted_servers(ural_db_t *db, ural_row_callback callback,
                                 void *context, char *err, size_t err_size);
#endif
