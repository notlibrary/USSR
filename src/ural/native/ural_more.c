/* ural_more.c - versions, constraints, licenses, SHA-256 and box.uno
 * helpers for Ural refine pass 1. Pure functions: the unit tests call
 * them without a database or a network. */
#include "ural_more.h"
#include "ural_manifest.h"

#include <ctype.h>
#include <openssl/evp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int compare_core(const unsigned char *a, const unsigned char *b,
                        const unsigned char *a_end, const unsigned char *b_end)
{
    while (a < a_end && b < b_end) {
        if (isdigit(*a) && isdigit(*b)) {
            const unsigned char *as, *bs;
            size_t al, bl;
            while (a < a_end && *a == '0') ++a;
            while (b < b_end && *b == '0') ++b;
            as = a;
            bs = b;
            while (a < a_end && isdigit(*a)) ++a;
            while (b < b_end && isdigit(*b)) ++b;
            al = (size_t)(a - as);
            bl = (size_t)(b - bs);
            if (al != bl) return al < bl ? -1 : 1;
            if (al != 0) {
                int cmp = memcmp(as, bs, al);
                if (cmp != 0) return cmp < 0 ? -1 : 1;
            }
            continue;
        }
        if (*a != *b) return *a < *b ? -1 : 1;
        ++a;
        ++b;
    }
    if (a == a_end && b == b_end) return 0;
    return a == a_end ? -1 : 1;
}

int ural_version_compare(const char *left, const char *right)
{
    const char *ld = strchr(left, '-');
    const char *rd = strchr(right, '-');
    const unsigned char *le = (const unsigned char *)(ld ? ld : left + strlen(left));
    const unsigned char *re = (const unsigned char *)(rd ? rd : right + strlen(right));
    int c = compare_core((const unsigned char *)left, (const unsigned char *)right, le, re);
    if (c != 0) return c;
    /* the same release numbers: no suffix is newer than any suffix */
    if (ld == NULL && rd == NULL) return 0;
    if (ld == NULL) return 1;
    if (rd == NULL) return -1;
    return compare_core((const unsigned char *)ld + 1, (const unsigned char *)rd + 1,
                        (const unsigned char *)ld + strlen(ld),
                        (const unsigned char *)rd + strlen(rd));
}

int ural_version_valid(const char *v)
{
    const unsigned char *p = (const unsigned char *)v;
    int digits = 0;
    if (p == NULL || !isdigit(*p)) return 0;
    for (;;) {
        digits = 0;
        while (isdigit(*p)) { ++p; ++digits; }
        if (digits == 0) return 0;
        if (*p == '.') { ++p; continue; }
        break;
    }
    if (*p == '\0') return 1;
    if (*p != '-') return 0;
    ++p;
    if (*p == '\0') return 0;
    for (; *p; ++p)
        if (!((*p >= 'a' && *p <= 'z') || isdigit(*p) || *p == '.'))
            return 0;
    return 1;
}

static void version_numbers(const char *v, long n[3])
{
    int i = 0;
    n[0] = n[1] = n[2] = 0;
    while (i < 3 && *v && *v != '-') {
        if (isdigit((unsigned char)*v)) {
            n[i] = strtol(v, (char **)&v, 10);
            ++i;
        }
        if (*v == '.') ++v;
        else if (*v && *v != '-' && !isdigit((unsigned char)*v)) break;
    }
}

int ural_version_bump(const char *version, const char *part, char *out, size_t cap)
{
    long n[3];
    if (!ural_version_valid(version)) return -1;
    version_numbers(version, n);
    if (strcmp(part, "major") == 0) { ++n[0]; n[1] = 0; n[2] = 0; }
    else if (strcmp(part, "minor") == 0) { ++n[1]; n[2] = 0; }
    else if (strcmp(part, "patch") == 0) {
        /* 0.4.0-alpha -> 0.4.0: the release of a pre-release */
        if (strchr(version, '-') == NULL) ++n[2];
    }
    else return -1;
    return snprintf(out, cap, "%ld.%ld.%ld", n[0], n[1], n[2]) < (int)cap ? 0 : -1;
}

static int is_op_char(char c)
{
    return c == '<' || c == '>' || c == '=' || c == '^' || c == '~' || c == '*' || c == ' ';
}

int ural_dependency_split(const char *dep, char *name, size_t cap,
                          const char **constraint)
{
    size_t n = 0;
    while (dep[n] && !is_op_char(dep[n])) ++n;
    if (n == 0 || n >= cap) return -1;
    memcpy(name, dep, n);
    name[n] = '\0';
    if (!ural_manifest_validate_component(name)) return -1;
    *constraint = dep + n;
    while (**constraint == ' ') ++*constraint;
    return 0;
}

/* one clause: its operator and version (in a buffer); 0 or -1 */
static int clause_parse(const char *s, size_t len, char op[3], char *ver, size_t cap)
{
    size_t i = 0, k = 0;
    while (i < len && s[i] == ' ') ++i;
    while (len > i && s[len - 1] == ' ') --len;
    op[0] = op[1] = op[2] = '\0';
    if (i == len) { op[0] = '*'; ver[0] = '\0'; return 0; }
    if (s[i] == '*' && i + 1 == len) { op[0] = '*'; ver[0] = '\0'; return 0; }
    if (s[i] == '>' || s[i] == '<') {
        op[0] = s[i++];
        if (i < len && s[i] == '=') op[1] = s[i++];
    } else if (s[i] == '=') {
        op[0] = '=';
        ++i;
        if (i < len && s[i] == '=') ++i;
    } else if (s[i] == '^' || s[i] == '~') {
        op[0] = s[i++];
    } else {
        op[0] = '=';
    }
    while (i < len && s[i] == ' ') ++i;
    if (len - i + 1 > cap) return -1;
    for (; i < len; ++i) ver[k++] = s[i];
    ver[k] = '\0';
    return ural_version_valid(ver) ? 0 : -1;
}

static int clause_holds(const char *v, const char op[3], const char *w)
{
    int c;
    long n[3];
    char upper[64];
    if (op[0] == '*') return 1;
    c = ural_version_compare(v, w);
    if (op[0] == '=') return c == 0;
    if (op[0] == '>') return op[1] == '=' ? c >= 0 : c > 0;
    if (op[0] == '<') return op[1] == '=' ? c <= 0 : c < 0;
    if (c < 0) return 0;
    version_numbers(w, n);
    if (op[0] == '^') {
        if (n[0] > 0) snprintf(upper, sizeof upper, "%ld.0.0-0", n[0] + 1);
        else if (n[1] > 0) snprintf(upper, sizeof upper, "0.%ld.0-0", n[1] + 1);
        else snprintf(upper, sizeof upper, "0.0.%ld-0", n[2] + 1);
    } else {
        snprintf(upper, sizeof upper, "%ld.%ld.0-0", n[0], n[1] + 1);
    }
    return ural_version_compare(v, upper) < 0;
}

static int constraint_walk(const char *version, const char *constraint, int check_only)
{
    const char *p = constraint;
    for (;;) {
        const char *comma = strchr(p, ',');
        size_t len = comma ? (size_t)(comma - p) : strlen(p);
        char op[3], ver[64];
        if (clause_parse(p, len, op, ver, sizeof ver) != 0) return check_only ? 0 : -1;
        if (!check_only && !clause_holds(version, op, ver)) return 0;
        if (comma == NULL) break;
        p = comma + 1;
    }
    return 1;
}

int ural_constraint_valid(const char *constraint)
{
    return constraint_walk(NULL, constraint, 1);
}

int ural_constraint_satisfied(const char *version, const char *constraint)
{
    if (!ural_version_valid(version)) return 0;
    return constraint_walk(version, constraint, 0) == 1;
}

static const char *const spdx_ids[] = {
    "UNSPECIFIED", "MIT", "MIT-0", "Apache-2.0", "BSD-2-Clause", "BSD-3-Clause",
    "BSD-3-Clause-Clear", "0BSD", "ISC", "Zlib", "Unlicense", "CC0-1.0",
    "CC-BY-4.0", "CC-BY-SA-4.0", "MPL-2.0", "EPL-2.0", "LGPL-2.1-only",
    "LGPL-2.1-or-later", "LGPL-3.0-only", "LGPL-3.0-or-later", "GPL-2.0-only",
    "GPL-2.0-or-later", "GPL-3.0-only", "GPL-3.0-or-later", "AGPL-3.0-only",
    "AGPL-3.0-or-later", "BSL-1.0", "Artistic-2.0", "OFL-1.1", "WTFPL",
    "Python-2.0", "PostgreSQL", "curl", "OpenSSL", NULL
};

int ural_spdx_known(const char *id)
{
    size_t i;
    if (id == NULL) return 0;
    for (i = 0; spdx_ids[i] != NULL; ++i)
        if (strcmp(spdx_ids[i], id) == 0) return 1;
    return 0;
}

int ural_sha256_file(const char *path, char out_hex[65])
{
    unsigned char buffer[65536], digest[EVP_MAX_MD_SIZE];
    unsigned int len = 0, i;
    size_t got;
    FILE *f = fopen(path, "rb");
    EVP_MD_CTX *ctx;
    int rc = -1;
    if (f == NULL) return -1;
    ctx = EVP_MD_CTX_new();
    if (ctx != NULL && EVP_DigestInit_ex(ctx, EVP_sha256(), NULL) == 1) {
        rc = 0;
        while ((got = fread(buffer, 1, sizeof buffer, f)) > 0)
            if (EVP_DigestUpdate(ctx, buffer, got) != 1) { rc = -1; break; }
        if (ferror(f)) rc = -1;
        if (rc == 0 && EVP_DigestFinal_ex(ctx, digest, &len) != 1) rc = -1;
    }
    EVP_MD_CTX_free(ctx);
    fclose(f);
    if (rc != 0) return -1;
    for (i = 0; i < len && i < 32; ++i)
        snprintf(out_hex + 2 * i, 3, "%02x", digest[i]);
    out_hex[64] = '\0';
    return 0;
}

void ural_free_list(char **items, size_t count)
{
    size_t i;
    for (i = 0; i < count; ++i) free(items[i]);
    free(items);
}

/* the start of field key= at the top level of a box.uno text */
static const char *field_start(const char *text, const char *key)
{
    size_t kl = strlen(key);
    const char *p = text;
    int quoted = 0;
    for (; *p; ++p) {
        if (*p == '`') { quoted = !quoted; continue; }
        if (quoted) continue;
        if ((p == text || p[-1] == '{' || p[-1] == ',' || p[-1] == ' ') &&
            strncmp(p, key, kl) == 0 && p[kl] == '=')
            return p + kl + 1;
    }
    return NULL;
}

int ural_manifest_files(const char *text, char ***files, size_t *count)
{
    const char *p = field_start(text, "files");
    char **items = NULL;
    size_t n = 0;
    *files = NULL;
    *count = 0;
    if (p == NULL) return 0;
    while (*p == ' ') ++p;
    if (*p != '[') return -1;
    ++p;
    for (;;) {
        const char *end;
        char **grown, *item;
        while (*p == ' ' || *p == ',') ++p;
        if (*p == ']') break;
        if (*p != '`') { ural_free_list(items, n); return -1; }
        end = strchr(p + 1, '`');
        if (end == NULL) { ural_free_list(items, n); return -1; }
        grown = (char **)realloc(items, (n + 1) * sizeof(*grown));
        item = (char *)malloc((size_t)(end - p));
        if (grown == NULL || item == NULL) {
            free(item);
            ural_free_list(grown ? grown : items, n);
            return -1;
        }
        items = grown;
        memcpy(item, p + 1, (size_t)(end - p - 1));
        item[end - p - 1] = '\0';
        items[n++] = item;
        p = end + 1;
    }
    *files = items;
    *count = n;
    return 0;
}

char *ural_manifest_with_version(const char *text, const char *version)
{
    const char *p = field_start(text, "version");
    const char *end;
    size_t head, vl = strlen(version), tail;
    char *out;
    if (p == NULL || *p != '`') return NULL;
    end = strchr(p + 1, '`');
    if (end == NULL) return NULL;
    head = (size_t)(p + 1 - text);
    tail = strlen(end);
    out = (char *)malloc(head + vl + tail + 1);
    if (out == NULL) return NULL;
    memcpy(out, text, head);
    memcpy(out + head, version, vl);
    memcpy(out + head + vl, end, tail + 1);
    return out;
}
