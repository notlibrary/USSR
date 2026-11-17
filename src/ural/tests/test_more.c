/* Unit tests for ural_more.c: version order and bumps, dependency
 * constraints, SPDX ids, SHA-256, and the box.uno helpers. */
#include "ural_manifest.h"
#include "ural_more.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures = 0;

static void expect(int ok, const char *what)
{
    if (!ok) {
        fprintf(stderr, "FAIL %s\n", what);
        ++failures;
    }
}

int main(void)
{
    char out[64], name[64], sha[65];
    const char *c;
    char **files = NULL;
    size_t count = 0;
    char *text;
    FILE *f;
    ural_manifest_t m;
    char err[256];

    /* order */
    expect(ural_version_compare("0.10.0", "0.9.0") > 0, "0.10.0 > 0.9.0");
    expect(ural_version_compare("1.0.0", "1.0.0") == 0, "1.0.0 = 1.0.0");
    expect(ural_version_compare("0.4.0-alpha", "0.4.0") < 0, "pre-release before release");
    expect(ural_version_compare("0.4.0-alpha", "0.4.0-beta") < 0, "alpha before beta");
    expect(ural_version_compare("0.4.0-rc.10", "0.4.0-rc.9") > 0, "rc.10 after rc.9");
    expect(ural_version_compare("1.2", "1.2.0") < 0, "1.2 before 1.2.0");
    expect(ural_version_valid("0.5.0-alpha"), "valid with suffix");
    expect(!ural_version_valid("v1.0"), "v1.0 is not");
    expect(!ural_version_valid("1..0"), "1..0 is not");
    expect(!ural_version_valid("1.0-"), "an empty suffix is not");

    /* bumps */
    expect(ural_version_bump("0.4.2", "patch", out, sizeof out) == 0 && !strcmp(out, "0.4.3"), "patch");
    expect(ural_version_bump("0.4.2", "minor", out, sizeof out) == 0 && !strcmp(out, "0.5.0"), "minor");
    expect(ural_version_bump("0.4.2", "major", out, sizeof out) == 0 && !strcmp(out, "1.0.0"), "major");
    expect(ural_version_bump("0.5.0-alpha", "patch", out, sizeof out) == 0 && !strcmp(out, "0.5.0"), "patch of a pre-release");
    expect(ural_version_bump("0.4.2", "tiny", out, sizeof out) != 0, "unknown part");

    /* constraints */
    expect(ural_dependency_split("ryazan>=0.1.0", name, sizeof name, &c) == 0 &&
           !strcmp(name, "ryazan") && !strcmp(c, ">=0.1.0"), "split with constraint");
    expect(ural_dependency_split("library", name, sizeof name, &c) == 0 && c[0] == '\0', "split plain");
    expect(ural_dependency_split("Bad>=1", name, sizeof name, &c) != 0, "bad name");
    expect(ural_constraint_satisfied("0.2.0", ">=0.1.0"), ">=");
    expect(!ural_constraint_satisfied("0.0.9", ">=0.1.0"), ">= fails");
    expect(ural_constraint_satisfied("0.4.9", "^0.4.1"), "^0.4.1 takes 0.4.9");
    expect(!ural_constraint_satisfied("0.5.0", "^0.4.1"), "^0.4.1 refuses 0.5.0");
    expect(ural_constraint_satisfied("1.9.0", "^1.2.0"), "^1.2.0 takes 1.9.0");
    expect(!ural_constraint_satisfied("2.0.0-alpha", "^1.2.0"), "^1.2.0 refuses 2.0.0-alpha");
    expect(ural_constraint_satisfied("0.2.5", "~0.2.1"), "~0.2.1 takes 0.2.5");
    expect(!ural_constraint_satisfied("0.3.0", "~0.2.1"), "~0.2.1 refuses 0.3.0");
    expect(ural_constraint_satisfied("1.5.0", ">=1.0.0, <2.0.0"), "a range");
    expect(!ural_constraint_satisfied("2.0.0", ">=1.0.0,<2.0.0"), "a range's end");
    expect(ural_constraint_satisfied("3.1.4", "*"), "any");
    expect(ural_constraint_satisfied("0.1.0", "=0.1.0") && ural_constraint_satisfied("0.1.0", "==0.1.0"), "equal");
    expect(ural_constraint_valid(">=0.1.0, <1") && !ural_constraint_valid(">=x"), "valid constraints");

    /* manifests may now carry constraints */
    expect(ural_manifest_parse("'Package{name=`a`,version=`0.1.0`,description=``,entry=`a.su`,"
                               "dependencies=[`library`,`ryazan>=0.1.0`],files=[`a.su`],"
                               "license=`MIT`,minimum_ussr=`0.1.0`,source=``}'",
                               &m, err, sizeof err) == 0 && m.dependency_count == 2,
           "a manifest with a constraint");
    ural_manifest_free(&m);
    memset(&m, 0, sizeof m);
    expect(ural_manifest_parse("'Package{name=`a`,version=`0.1.0`,description=``,entry=`a.su`,"
                               "dependencies=[`ryazan>=nope`],files=[],"
                               "license=`MIT`,minimum_ussr=`0.1.0`,source=``}'",
                               &m, err, sizeof err) != 0,
           "a bad constraint is refused");
    ural_manifest_free(&m);

    /* licenses */
    expect(ural_spdx_known("MIT") && ural_spdx_known("GPL-3.0-or-later") && ural_spdx_known("UNSPECIFIED"), "known ids");
    expect(!ural_spdx_known("mit") && !ural_spdx_known("GPL"), "unknown ids");

    /* SHA-256 of "abc" */
    f = fopen("ural-test-more.txt", "wb");
    if (f) { fputs("abc", f); fclose(f); }
    expect(ural_sha256_file("ural-test-more.txt", sha) == 0 &&
           !strcmp(sha, "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"), "sha256 abc");
    remove("ural-test-more.txt");
    expect(ural_sha256_file("ural-test-missing.txt", sha) != 0, "sha256 of no file");

    /* box.uno helpers */
    text = "'Package{name=`k`,version=`0.1.0`,description=`files=[x]`,entry=`k.su`,"
           "dependencies=[],files=[`k.su`, `README.md`,`docs/API.md`],license=`MIT`}'";
    expect(ural_manifest_files(text, &files, &count) == 0 && count == 3 &&
           !strcmp(files[2], "docs/API.md"), "files list");
    ural_free_list(files, count);
    {
        char *bumped = ural_manifest_with_version(text, "0.2.0");
        expect(bumped != NULL && strstr(bumped, "version=`0.2.0`") != NULL &&
               strstr(bumped, "description=`files=[x]`") != NULL, "version replaced");
        free(bumped);
    }

    if (failures == 0) puts("test_more: all passed");
    return failures ? 1 : 0;
}
