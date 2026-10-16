#include "../native/ural_manifest.h"

#include <stdio.h>
#include <string.h>

int main(void)
{
    const char *valid =
        "'Package{name=`ukraine`,version=`1.2.0`,description=`HTTP server`,"
        "entry=`ukraine.su`,dependencies=[`library`,`russia`],"
        "files=[],license=`UNSPECIFIED`,minimum_ussr=`0.1.0`,source=``}'";
    const char *duplicate =
        "Package{name=`one`,name=`two`,version=`1.0`,dependencies=[]}";
    const char *unsafe =
        "Package{name=`../escape`,version=`1.0`,dependencies=[]}";
    ural_manifest_t manifest;
    char err[128] = {0};
    if (ural_manifest_parse(valid, &manifest, err, sizeof err) != 0) {
        fprintf(stderr, "could not parse valid manifest: %s\n", err);
        return 1;
    }
    if (strcmp(manifest.name, "ukraine") != 0 ||
        strcmp(manifest.version, "1.2.0") != 0 ||
        manifest.dependency_count != 2 ||
        strcmp(manifest.dependencies[0], "library") != 0 ||
        strcmp(manifest.dependencies[1], "russia") != 0) {
        ural_manifest_free(&manifest);
        fputs("manifest fields were not decoded correctly\n", stderr);
        return 1;
    }
    ural_manifest_free(&manifest);
    if (ural_manifest_parse(duplicate, &manifest, err, sizeof err) == 0) {
        ural_manifest_free(&manifest);
        fputs("duplicate field was accepted\n", stderr);
        return 1;
    }
    if (ural_manifest_parse(unsafe, &manifest, err, sizeof err) == 0) {
        ural_manifest_free(&manifest);
        fputs("unsafe package name was accepted\n", stderr);
        return 1;
    }
    puts("ural_manifest tests: OK");
    return 0;
}
