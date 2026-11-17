#ifndef URAL_MORE_H
#define URAL_MORE_H

#include <stddef.h>

/* Versions: numbers compare as numbers, "0.4.0-alpha" sorts before
 * "0.4.0" (a release has no suffix), then text compares byte by byte. */
int ural_version_compare(const char *left, const char *right);
/* 1 when version is N(.N)* with an optional -suffix of [a-z0-9.]. */
int ural_version_valid(const char *version);
/* "patch", "minor" or "major" bumped into out (the suffix dropped). */
int ural_version_bump(const char *version, const char *part, char *out, size_t cap);

/* "ryazan>=0.1.0" -> name "ryazan", constraint ">=0.1.0" (pointer into
 * dep, "" when there is none). 0, or -1 for a bad name. */
int ural_dependency_split(const char *dep, char *name, size_t cap,
                          const char **constraint);
/* Clauses joined by ',': =V ==V >V >=V <V <=V ^V ~V *; "" is any. */
int ural_constraint_valid(const char *constraint);
int ural_constraint_satisfied(const char *version, const char *constraint);

/* 1 for a known SPDX license id (or UNSPECIFIED). */
int ural_spdx_known(const char *id);

/* SHA-256 of a file as 64 lowercase hex digits; 0 on success. */
int ural_sha256_file(const char *path, char out_hex[65]);

/* The `files` list of a box.uno text (malloc'd strings). */
int ural_manifest_files(const char *text, char ***files, size_t *count);
void ural_free_list(char **items, size_t count);
/* A copy of a box.uno text with its version replaced, or NULL. */
char *ural_manifest_with_version(const char *text, const char *version);

#endif
