#include "ural_manifest.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct reader {
    const char *p;
} reader_t;

static void set_error(char *err, size_t cap, const char *message)
{
    if (err != NULL && cap > 0)
        snprintf(err, cap, "%s", message);
}

static void skip_space(reader_t *r)
{
    while (isspace((unsigned char)*r->p))
        ++r->p;
}

static char *read_identifier(reader_t *r)
{
    const char *start;
    size_t length;
    char *result;

    if (!(isalpha((unsigned char)*r->p) || *r->p == '_'))
        return NULL;
    start = r->p++;
    while (isalnum((unsigned char)*r->p) || *r->p == '_')
        ++r->p;
    length = (size_t)(r->p - start);
    result = (char *)malloc(length + 1);
    if (result != NULL) {
        memcpy(result, start, length);
        result[length] = '\0';
    }
    return result;
}

static char *read_string(reader_t *r)
{
    const char *start;
    size_t length;
    char *result;
    if (*r->p != '`')
        return NULL;
    start = ++r->p;
    while (*r->p != '\0' && *r->p != '`' && *r->p != '\n' && *r->p != '\r')
        ++r->p;
    if (*r->p != '`')
        return NULL;
    length = (size_t)(r->p++ - start);
    result = (char *)malloc(length + 1);
    if (result != NULL) {
        memcpy(result, start, length);
        result[length] = '\0';
    }
    return result;
}

static int skip_value(reader_t *r)
{
    int depth;
    skip_space(r);
    if (*r->p == '`') {
        char *value = read_string(r);
        if (value == NULL)
            return -1;
        free(value);
        return 0;
    }
    if (*r->p == '[' || *r->p == '{') {
        char open = *r->p++;
        char close = open == '[' ? ']' : '}';
        depth = 1;
        while (*r->p != '\0' && depth > 0) {
            if (*r->p == '`') {
                char *value = read_string(r);
                if (value == NULL)
                    return -1;
                free(value);
                continue;
            }
            if (*r->p == open)
                ++depth;
            else if (*r->p == close)
                --depth;
            ++r->p;
        }
        return depth == 0 ? 0 : -1;
    }
    while (*r->p != '\0' && *r->p != ',' && *r->p != '}' && !isspace((unsigned char)*r->p))
        ++r->p;
    return 0;
}

int ural_manifest_validate_component(const char *text)
{
    const unsigned char *p = (const unsigned char *)text;
    if (p == NULL || *p == '\0' || *p == '.' || *p == '-')
        return 0;
    for (; *p; ++p) {
        if (!((*p >= 'a' && *p <= 'z') || (*p >= '0' && *p <= '9') ||
              *p == '-' || *p == '_' || *p == '.'))
            return 0;
    }
    return 1;
}

static int append_dependency(ural_manifest_t *m, char *value)
{
    char **items;
    if (!ural_manifest_validate_component(value))
        return -1;
    items = (char **)realloc(m->dependencies,
                              (m->dependency_count + 1) * sizeof(*items));
    if (items == NULL)
        return -1;
    m->dependencies = items;
    m->dependencies[m->dependency_count++] = value;
    return 0;
}

static int read_dependencies(reader_t *r, ural_manifest_t *m)
{
    skip_space(r);
    if (*r->p++ != '[')
        return -1;
    skip_space(r);
    while (*r->p != ']') {
        char *item = read_string(r);
        if (item == NULL || append_dependency(m, item) != 0) {
            free(item);
            return -1;
        }
        skip_space(r);
        if (*r->p == ']')
            break;
        if (*r->p++ != ',')
            return -1;
        skip_space(r);
    }
    ++r->p;
    return 0;
}

static int set_field(char **field, reader_t *r)
{
    char *value = read_string(r);
    if (value == NULL)
        return -1;
    free(*field);
    *field = value;
    return 0;
}

int ural_manifest_parse(const char *text, ural_manifest_t *m,
                        char *err, size_t err_size)
{
    reader_t r;
    unsigned int seen = 0;
    if (text == NULL || m == NULL) {
        set_error(err, err_size, "manifest text and output are required");
        return -1;
    }
    memset(m, 0, sizeof(*m));
    r.p = text;
    skip_space(&r);
    if (*r.p == '\'')
        ++r.p;
    skip_space(&r);
    if (strncmp(r.p, "Package", 7) != 0) {
        set_error(err, err_size, "manifest must be a Package UNO object");
        goto fail;
    }
    r.p += 7;
    skip_space(&r);
    if (*r.p++ != '{') {
        set_error(err, err_size, "expected '{' after Package");
        goto fail;
    }
    skip_space(&r);
    while (*r.p != '}' && *r.p != '\0') {
        char *key = read_identifier(&r);
        if (key == NULL) {
            set_error(err, err_size, "invalid package field name");
            goto fail;
        }
        skip_space(&r);
        if (*r.p++ != '=') {
            free(key);
            set_error(err, err_size, "expected '=' after package field");
            goto fail;
        }
        skip_space(&r);
        if (strcmp(key, "name") == 0) {
            if ((seen & 1u) || set_field(&m->name, &r) != 0) goto malformed_field;
            seen |= 1u;
        } else if (strcmp(key, "version") == 0) {
            if ((seen & 2u) || set_field(&m->version, &r) != 0) goto malformed_field;
            seen |= 2u;
        } else if (strcmp(key, "description") == 0) {
            if ((seen & 4u) || set_field(&m->description, &r) != 0) goto malformed_field;
            seen |= 4u;
        } else if (strcmp(key, "entry") == 0) {
            if ((seen & 8u) || set_field(&m->entry, &r) != 0) goto malformed_field;
            seen |= 8u;
        } else if (strcmp(key, "dependencies") == 0) {
            if ((seen & 16u) || read_dependencies(&r, m) != 0) goto malformed_field;
            seen |= 16u;
        } else if (strcmp(key, "license") == 0) {
            if ((seen & 32u) || set_field(&m->license, &r) != 0) goto malformed_field;
            seen |= 32u;
        } else if (strcmp(key, "minimum_ussr") == 0) {
            if ((seen & 64u) || set_field(&m->minimum_ussr, &r) != 0) goto malformed_field;
            seen |= 64u;
        } else if (strcmp(key, "source") == 0) {
            if ((seen & 128u) || set_field(&m->source, &r) != 0) goto malformed_field;
            seen |= 128u;
        } else if (skip_value(&r) != 0) {
            free(key);
            set_error(err, err_size, "invalid package field value");
            goto fail;
        }
        free(key);
        skip_space(&r);
        if (*r.p == '}')
            break;
        if (*r.p++ != ',') {
            set_error(err, err_size, "expected ',' between package fields");
            goto fail;
        }
        skip_space(&r);
        continue;
malformed_field:
        free(key);
        set_error(err, err_size, "invalid or duplicate package field");
        goto fail;
    }
    if (*r.p++ != '}') {
        set_error(err, err_size, "unterminated Package object");
        goto fail;
    }
    skip_space(&r);
    if (*r.p == '\'')
        ++r.p;
    skip_space(&r);
    if (*r.p != '\0' || (seen & 19u) != 19u ||
        !ural_manifest_validate_component(m->name) ||
        !ural_manifest_validate_component(m->version)) {
        set_error(err, err_size, "manifest has trailing data or invalid required fields");
        goto fail;
    }
    return 0;
fail:
    ural_manifest_free(m);
    return -1;
}

void ural_manifest_free(ural_manifest_t *m)
{
    size_t i;
    if (m == NULL)
        return;
    free(m->name);
    free(m->version);
    free(m->description);
    free(m->entry);
    for (i = 0; i < m->dependency_count; ++i)
        free(m->dependencies[i]);
    free(m->dependencies);
    free(m->license);
    free(m->minimum_ussr);
    free(m->source);
    memset(m, 0, sizeof(*m));
}
