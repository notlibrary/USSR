#ifndef URAL_MANIFEST_H
#define URAL_MANIFEST_H

#include <stddef.h>

typedef struct ural_manifest {
    char *name;
    char *version;
    char *description;
    char *entry;
    char **dependencies;
    size_t dependency_count;
    char *license;
    char *minimum_ussr;
    char *source;
} ural_manifest_t;

int ural_manifest_parse(const char *text, ural_manifest_t *manifest,
                        char *err, size_t err_size);
void ural_manifest_free(ural_manifest_t *manifest);
int ural_manifest_validate_component(const char *text);

#endif
