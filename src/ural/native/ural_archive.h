#ifndef URAL_ARCHIVE_H
#define URAL_ARCHIVE_H

#include <stddef.h>

typedef int (*ural_file_callback)(void *context, const char *relative_path);

/* Hashes file bytes with 64-bit FNV-1a; returns 0 on success. */
int ural_hash_file(const char *path, char out_hex[17]);

/* Create/extract the supported ZIP subset (stored or raw DEFLATE files). */
int ural_archive_pack(const char *directory, const char *archive,
                      char *err, size_t err_size);
int ural_archive_extract(const char *archive, const char *directory,
                         ural_file_callback on_file, void *context,
                         char *err, size_t err_size);

/* Validate a slash-separated archive-relative path. */
int ural_archive_path_is_safe(const char *path);

#endif
