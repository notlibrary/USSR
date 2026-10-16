#if !defined(_WIN32) && !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200809L
#endif

#include "ural_archive.h"

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <zlib.h>

#ifdef _WIN32
#include <direct.h>
#include <io.h>
#include <windows.h>
#define URAL_MKDIR(path) _mkdir(path)
#define URAL_UNLINK(path) _unlink(path)
#else
#include <dirent.h>
#include <unistd.h>
#define URAL_MKDIR(path) mkdir(path, 0755)
#define URAL_UNLINK(path) unlink(path)
#endif

#define ZIP_LOCAL_SIGNATURE 0x04034b50u
#define ZIP_CENTRAL_SIGNATURE 0x02014b50u
#define ZIP_END_SIGNATURE 0x06054b50u
#define ZIP_LIMIT (128u * 1024u * 1024u)

typedef struct zip_entry {
    char *name;
    uint32_t crc;
    uint32_t compressed_size;
    uint32_t size;
    uint32_t offset;
} zip_entry_t;

typedef struct zip_buffer {
    unsigned char *data;
    size_t size;
    size_t capacity;
} zip_buffer_t;

static char *duplicate_string(const char *text)
{
    size_t length = strlen(text) + 1;
    char *copy = (char *)malloc(length);
    if (copy != NULL)
        memcpy(copy, text, length);
    return copy;
}

static void set_error(char *err, size_t cap, const char *message)
{
    if (err != NULL && cap > 0)
        snprintf(err, cap, "%s", message);
}

static int buffer_append(zip_buffer_t *buffer, const void *data, size_t size)
{
    unsigned char *grown;
    size_t capacity;
    if (size > SIZE_MAX - buffer->size)
        return -1;
    if (buffer->size + size > buffer->capacity) {
        capacity = buffer->capacity ? buffer->capacity : 65536;
        while (capacity < buffer->size + size) {
            if (capacity > SIZE_MAX / 2)
                return -1;
            capacity *= 2;
        }
        grown = (unsigned char *)realloc(buffer->data, capacity);
        if (grown == NULL)
            return -1;
        buffer->data = grown;
        buffer->capacity = capacity;
    }
    memcpy(buffer->data + buffer->size, data, size);
    buffer->size += size;
    return 0;
}

static int write_u16(FILE *file, uint16_t value)
{
    unsigned char b[2] = {(unsigned char)value, (unsigned char)(value >> 8)};
    return fwrite(b, 1, sizeof b, file) == sizeof b ? 0 : -1;
}

static int write_u32(FILE *file, uint32_t value)
{
    unsigned char b[4] = {(unsigned char)value, (unsigned char)(value >> 8),
                          (unsigned char)(value >> 16), (unsigned char)(value >> 24)};
    return fwrite(b, 1, sizeof b, file) == sizeof b ? 0 : -1;
}

#ifdef _WIN32
static int ascii_case_equal(const char *text, const char *word, size_t length)
{
    size_t i;
    for (i = 0; i < length; ++i) {
        unsigned char a = (unsigned char)text[i];
        unsigned char b = (unsigned char)word[i];
        if (a >= 'a' && a <= 'z') a = (unsigned char)(a - 'a' + 'A');
        if (b >= 'a' && b <= 'z') b = (unsigned char)(b - 'a' + 'A');
        if (a != b)
            return 0;
    }
    return 1;
}
#endif

static int read_u16(FILE *file, uint16_t *value)
{
    unsigned char b[2];
    if (fread(b, 1, sizeof b, file) != sizeof b)
        return -1;
    *value = (uint16_t)(b[0] | ((uint16_t)b[1] << 8));
    return 0;
}

static int read_u32(FILE *file, uint32_t *value)
{
    unsigned char b[4];
    if (fread(b, 1, sizeof b, file) != sizeof b)
        return -1;
    *value = (uint32_t)b[0] | ((uint32_t)b[1] << 8) |
             ((uint32_t)b[2] << 16) | ((uint32_t)b[3] << 24);
    return 0;
}

static char *join_path(const char *left, const char *right)
{
    size_t a = strlen(left), b = strlen(right);
    while (a > 1 && (left[a - 1] == '/' || left[a - 1] == '\\')
#ifdef _WIN32
           && !(a == 3 && left[1] == ':')
#endif
    )
        --a;
    char *path = (char *)malloc(a + b + 2);
    if (path == NULL)
        return NULL;
    memcpy(path, left, a);
#ifdef _WIN32
    if (a != 0 && left[a - 1] != '\\' && left[a - 1] != '/')
        path[a++] = '\\';
#else
    if (a != 0 && left[a - 1] != '/')
        path[a++] = '/';
#endif
    memcpy(path + a, right, b + 1);
    return path;
}

static int make_directories(const char *path)
{
    char *copy = duplicate_string(path);
    char *p;
    if (copy == NULL)
        return -1;
    for (p = copy + 1; *p; ++p) {
        if (*p == '/' || *p == '\\') {
            char saved = *p;
            *p = '\0';
            if (URAL_MKDIR(copy) != 0 && errno != EEXIST) {
                free(copy);
                return -1;
            }
            *p = saved;
        }
    }
    if (URAL_MKDIR(copy) != 0 && errno != EEXIST) {
        free(copy);
        return -1;
    }
    free(copy);
    return 0;
}

int ural_archive_path_is_safe(const char *path)
{
    const char *p, *component;
    if (path == NULL || path[0] == '\0' || path[0] == '/' || path[0] == '\\')
        return 0;
    if (strchr(path, ':') != NULL || strchr(path, '\\') != NULL)
        return 0;
    component = path;
    for (p = path; ; ++p) {
        if (*p == '\0' || *p == '/') {
            size_t length = (size_t)(p - component);
            if (length == 0 || (length == 1 && component[0] == '.') ||
                (length == 2 && component[0] == '.' && component[1] == '.'))
                return 0;
#ifdef _WIN32
            if (component[length - 1] == '.' || component[length - 1] == ' ')
                return 0;
            {
                size_t base_length = 0;
                while (base_length < length && component[base_length] != '.')
                    ++base_length;
                if ((base_length == 3 &&
                     (ascii_case_equal(component, "CON", 3) ||
                      ascii_case_equal(component, "PRN", 3) ||
                      ascii_case_equal(component, "AUX", 3) ||
                      ascii_case_equal(component, "NUL", 3))) ||
                    (base_length == 4 &&
                     (ascii_case_equal(component, "COM", 3) ||
                      ascii_case_equal(component, "LPT", 3)) &&
                     component[3] >= '1' && component[3] <= '9'))
                    return 0;
            }
#endif
            if (*p == '\0')
                break;
            component = p + 1;
        }
        if ((unsigned char)*p < 0x20)
            return 0;
    }
    return 1;
}

int ural_hash_file(const char *path, char out_hex[17])
{
    FILE *file;
    unsigned char buffer[32768];
    static const char hex[] = "0123456789abcdef";
    size_t count, i;
    int digit;
    uint64_t hash = UINT64_C(14695981039346656037);
    if (path == NULL || out_hex == NULL)
        return -1;
    file = fopen(path, "rb");
    if (file == NULL)
        return -1;
    while ((count = fread(buffer, 1, sizeof buffer, file)) != 0) {
        for (i = 0; i < count; ++i) {
            hash ^= buffer[i];
            hash *= UINT64_C(1099511628211);
        }
    }
    if (ferror(file)) {
        fclose(file);
        return -1;
    }
    fclose(file);
    for (digit = 15; digit >= 0; --digit) {
        out_hex[digit] = hex[hash & 0x0fu];
        hash >>= 4;
    }
    out_hex[16] = '\0';
    return 0;
}

static int grow_entries(zip_entry_t **entries, size_t *count, size_t *capacity,
                        zip_entry_t **out)
{
    zip_entry_t *grown;
    size_t next = *capacity ? *capacity * 2 : 32;
    if (*count == *capacity) {
        grown = (zip_entry_t *)realloc(*entries, next * sizeof(**entries));
        if (grown == NULL)
            return -1;
        *entries = grown;
        *capacity = next;
    }
    *out = &(*entries)[(*count)++];
    memset(*out, 0, sizeof(**entries));
    return 0;
}

static int pack_file(const char *root, const char *relative, FILE *out,
                     zip_entry_t **entries, size_t *count, size_t *capacity)
{
    char *source = join_path(root, relative);
    FILE *input = NULL;
    unsigned char *raw = NULL;
    zip_buffer_t compressed = {0};
    z_stream stream;
    zip_entry_t *entry;
    struct stat st;
    uint32_t crc;
    size_t name_length = strlen(relative);
    int result = -1;
    size_t suffix_len = name_length;
    if ((suffix_len >= 5 && strcmp(relative + suffix_len - 5, ".ural") == 0) ||
        (suffix_len >= 8 && strcmp(relative + suffix_len - 8, "-box.uno") == 0) ||
        strcmp(relative, "ussr.boxes") == 0)
        return 0;
    if (source == NULL || stat(source, &st) != 0 || st.st_size < 0 ||
        (uint64_t)st.st_size > ZIP_LIMIT || name_length > UINT16_MAX)
        goto done;
    input = fopen(source, "rb");
    if (input == NULL)
        goto done;
    raw = (unsigned char *)malloc(st.st_size ? (size_t)st.st_size : 1);
    if (raw == NULL || fread(raw, 1, (size_t)st.st_size, input) != (size_t)st.st_size ||
        ferror(input))
        goto done;
    crc = (uint32_t)crc32(0L, raw, (uInt)st.st_size);
    memset(&stream, 0, sizeof stream);
    if (deflateInit2(&stream, Z_DEFAULT_COMPRESSION, Z_DEFLATED, -MAX_WBITS,
                     8, Z_DEFAULT_STRATEGY) != Z_OK)
        goto done;
    stream.next_in = raw;
    stream.avail_in = (uInt)st.st_size;
    {
        int zrc;
        do {
        unsigned char chunk[32768];
        stream.next_out = chunk;
        stream.avail_out = sizeof chunk;
        zrc = deflate(&stream, Z_FINISH);
        if (zrc != Z_OK && zrc != Z_STREAM_END) {
            deflateEnd(&stream);
            goto done;
        }
        if (buffer_append(&compressed, chunk, sizeof chunk - stream.avail_out) != 0) {
            deflateEnd(&stream);
            goto done;
        }
        } while (zrc != Z_STREAM_END);
    }
    if (deflateEnd(&stream) != Z_OK || compressed.size > UINT32_MAX ||
        ftell(out) < 0 || (unsigned long)ftell(out) > UINT32_MAX ||
        grow_entries(entries, count, capacity, &entry) != 0)
        goto done;
    entry->name = (char *)malloc(name_length + 1);
    if (entry->name == NULL)
        goto done;
    memcpy(entry->name, relative, name_length + 1);
    entry->crc = crc;
    entry->size = (uint32_t)st.st_size;
    entry->compressed_size = (uint32_t)compressed.size;
    entry->offset = (uint32_t)ftell(out);
    if (write_u32(out, ZIP_LOCAL_SIGNATURE) || write_u16(out, 20) ||
        write_u16(out, 0) || write_u16(out, 8) || write_u16(out, 0) ||
        write_u16(out, 0) || write_u32(out, crc) ||
        write_u32(out, entry->compressed_size) || write_u32(out, entry->size) ||
        write_u16(out, (uint16_t)name_length) || write_u16(out, 0) ||
        fwrite(relative, 1, name_length, out) != name_length ||
        fwrite(compressed.data, 1, compressed.size, out) != compressed.size)
        goto done;
    result = 0;
done:
    if (input != NULL)
        fclose(input);
    free(source);
    free(raw);
    free(compressed.data);
    return result;
}

static int pack_walk(const char *root, const char *relative, FILE *out,
                     zip_entry_t **entries, size_t *count, size_t *capacity)
{
    char *directory = relative[0] ? join_path(root, relative) : duplicate_string(root);
    int result = 0;
    if (directory == NULL)
        return -1;
#ifdef _WIN32
    {
        char *pattern = join_path(directory, "*");
        WIN32_FIND_DATAA data;
        HANDLE find;
        if (pattern == NULL) { free(directory); return -1; }
        find = FindFirstFileA(pattern, &data);
        free(pattern);
        if (find == INVALID_HANDLE_VALUE) { free(directory); return -1; }
        do {
            char *child_rel, *child_full;
            size_t n;
            DWORD attr = data.dwFileAttributes;
            if (!strcmp(data.cFileName, ".") || !strcmp(data.cFileName, "..") ||
                (attr & FILE_ATTRIBUTE_REPARSE_POINT))
                continue;
            n = strlen(relative) + strlen(data.cFileName) + 2;
            child_rel = (char *)malloc(n);
            if (child_rel == NULL) { result = -1; break; }
            snprintf(child_rel, n, "%s%s%s", relative,
                     relative[0] ? "/" : "", data.cFileName);
            child_full = join_path(root, child_rel);
            if (child_full == NULL) { free(child_rel); result = -1; break; }
            if (attr & FILE_ATTRIBUTE_DIRECTORY)
                result = pack_walk(root, child_rel, out, entries, count, capacity);
            else
                result = pack_file(root, child_rel, out, entries, count, capacity);
            free(child_full);
            free(child_rel);
            if (result != 0) break;
        } while (FindNextFileA(find, &data));
        FindClose(find);
    }
#else
    {
        DIR *dir = opendir(directory);
        struct dirent *item;
        if (dir == NULL) { free(directory); return -1; }
        while ((item = readdir(dir)) != NULL) {
            char *child_rel, *child_full;
            struct stat st;
            size_t n;
            if (!strcmp(item->d_name, ".") || !strcmp(item->d_name, ".."))
                continue;
            n = strlen(relative) + strlen(item->d_name) + 2;
            child_rel = (char *)malloc(n);
            if (child_rel == NULL) { result = -1; break; }
            snprintf(child_rel, n, "%s%s%s", relative,
                     relative[0] ? "/" : "", item->d_name);
            child_full = join_path(root, child_rel);
            if (child_full == NULL || lstat(child_full, &st) != 0) {
                free(child_full); free(child_rel); result = -1; break;
            }
            if (S_ISLNK(st.st_mode)) {
                free(child_full); free(child_rel); continue;
            }
            result = S_ISDIR(st.st_mode)
                ? pack_walk(root, child_rel, out, entries, count, capacity)
                : S_ISREG(st.st_mode)
                    ? pack_file(root, child_rel, out, entries, count, capacity)
                    : 0;
            free(child_full);
            free(child_rel);
            if (result != 0) break;
        }
        closedir(dir);
    }
#endif
    free(directory);
    return result;
}

int ural_archive_pack(const char *directory, const char *archive,
                      char *err, size_t err_size)
{
    FILE *out;
    zip_entry_t *entries = NULL;
    size_t count = 0, capacity = 0, i;
    long central_start, central_end;
    int result = -1;
    if (directory == NULL || archive == NULL) {
        set_error(err, err_size, "package directory and archive path are required");
        return -1;
    }
    out = fopen(archive, "wb");
    if (out == NULL) {
        set_error(err, err_size, "cannot create archive");
        return -1;
    }
    if (pack_walk(directory, "", out, &entries, &count, &capacity) != 0 ||
        (central_start = ftell(out)) < 0 || count > UINT16_MAX)
        goto fail;
    for (i = 0; i < count; ++i) {
        size_t n = strlen(entries[i].name);
        if (write_u32(out, ZIP_CENTRAL_SIGNATURE) || write_u16(out, 20) ||
            write_u16(out, 20) || write_u16(out, 0) || write_u16(out, 8) ||
            write_u16(out, 0) || write_u16(out, 0) ||
            write_u32(out, entries[i].crc) ||
            write_u32(out, entries[i].compressed_size) ||
            write_u32(out, entries[i].size) ||
            write_u16(out, (uint16_t)n) || write_u16(out, 0) ||
            write_u16(out, 0) || write_u16(out, 0) || write_u16(out, 0) ||
            write_u32(out, 0) || write_u32(out, entries[i].offset) ||
            fwrite(entries[i].name, 1, n, out) != n)
            goto fail;
    }
    central_end = ftell(out);
    if (central_end < 0 || write_u32(out, ZIP_END_SIGNATURE) ||
        write_u16(out, 0) || write_u16(out, 0) ||
        write_u16(out, (uint16_t)count) || write_u16(out, (uint16_t)count) ||
        write_u32(out, (uint32_t)(central_end - central_start)) ||
        write_u32(out, (uint32_t)central_start) || write_u16(out, 0) ||
        fflush(out) != 0)
        goto fail;
    result = 0;
    goto done;
fail:
    set_error(err, err_size, "failed to write ZIP archive (ZIP64 is unsupported)");
done:
    if (fclose(out) != 0 && result == 0) {
        result = -1;
        set_error(err, err_size, "failed to close ZIP archive");
    }
    if (result != 0)
        URAL_UNLINK(archive);
    for (i = 0; i < count; ++i)
        free(entries[i].name);
    free(entries);
    return result;
}

static int extract_entry(FILE *in, const char *root, uint16_t method,
                         uint32_t compressed_size, uint32_t size, uint32_t expected_crc,
                         const char *name, ural_file_callback callback, void *context)
{
    unsigned char *compressed = NULL, *raw = NULL;
    char *path = NULL, *parent = NULL;
    z_stream stream;
    uLong crc;
    int result = -1;
    if (!ural_archive_path_is_safe(name) || size > ZIP_LIMIT ||
        compressed_size > ZIP_LIMIT || (method != 0 && method != 8))
        return -1;
    compressed = (unsigned char *)malloc(compressed_size ? compressed_size : 1);
    raw = (unsigned char *)malloc(size ? size : 1);
    path = join_path(root, name);
    if (compressed == NULL || raw == NULL || path == NULL ||
        fread(compressed, 1, compressed_size, in) != compressed_size)
        goto done;
    if (method == 0) {
        if (compressed_size != size)
            goto done;
        memcpy(raw, compressed, size);
    } else {
        memset(&stream, 0, sizeof stream);
        stream.next_in = compressed;
        stream.avail_in = compressed_size;
        stream.next_out = raw;
        stream.avail_out = size ? size : 1;
        if (inflateInit2(&stream, -MAX_WBITS) != Z_OK)
            goto done;
        if (inflate(&stream, Z_FINISH) != Z_STREAM_END ||
            stream.total_out != size || stream.total_in != compressed_size) {
            inflateEnd(&stream);
            goto done;
        }
        inflateEnd(&stream);
    }
    crc = crc32(0L, raw, (uInt)size);
    if ((uint32_t)crc != expected_crc)
        goto done;
    parent = (char *)malloc(strlen(path) + 1);
    if (parent == NULL)
        goto done;
    strcpy(parent, path);
    {
        char *separator = strrchr(parent, '/');
#ifdef _WIN32
        char *backslash = strrchr(parent, '\\');
        if (backslash != NULL && (separator == NULL || backslash > separator))
            separator = backslash;
#endif
        if (separator != NULL && separator >= parent + strlen(root))
            *separator = '\0';
    }
    if (strcmp(path, parent) != 0) {
        if (make_directories(parent) != 0)
            goto done;
    } else if (make_directories(root) != 0) {
        goto done;
    }
    {
        FILE *out = fopen(path, "wb");
        if (out == NULL)
            goto done;
        if (fwrite(raw, 1, size, out) != size) {
            fclose(out);
            goto done;
        }
        if (fclose(out) != 0)
            goto done;
    }
    if (callback != NULL && callback(context, name) != 0)
        goto done;
    result = 0;
done:
    free(compressed);
    free(raw);
    free(path);
    free(parent);
    return result;
}

int ural_archive_extract(const char *archive, const char *directory,
                         ural_file_callback on_file, void *context,
                         char *err, size_t err_size)
{
    FILE *in = NULL;
    int result = -1;
    if (archive == NULL || directory == NULL) {
        set_error(err, err_size, "archive and extraction directory are required");
        return -1;
    }
    in = fopen(archive, "rb");
    if (in == NULL || make_directories(directory) != 0) {
        set_error(err, err_size, "cannot open archive or create extraction directory");
        goto done;
    }
    for (;;) {
        uint32_t signature, crc, compressed_size, size;
        uint16_t version, flags, method, time, date, name_length, extra_length;
        char *name;
        if (read_u32(in, &signature) != 0)
            break;
        if (signature == ZIP_CENTRAL_SIGNATURE || signature == ZIP_END_SIGNATURE) {
            result = 0;
            break;
        }
        if (signature != ZIP_LOCAL_SIGNATURE ||
            read_u16(in, &version) || read_u16(in, &flags) ||
            read_u16(in, &method) || read_u16(in, &time) ||
            read_u16(in, &date) || read_u32(in, &crc) ||
            read_u32(in, &compressed_size) || read_u32(in, &size) ||
            read_u16(in, &name_length) || read_u16(in, &extra_length) ||
            name_length == 0 || name_length > 4096 || flags != 0)
            break;
        name = (char *)malloc((size_t)name_length + 1);
        if (name == NULL)
            break;
        if (fread(name, 1, name_length, in) != name_length ||
            fseek(in, extra_length, SEEK_CUR) != 0) {
            free(name);
            break;
        }
        if (memchr(name, '\0', name_length) != NULL) {
            free(name);
            break;
        }
        name[name_length] = '\0';
        if (extract_entry(in, directory, method, compressed_size, size,
                          crc, name, on_file, context) != 0) {
            free(name);
            break;
        }
        free(name);
        (void)version;
        (void)time;
        (void)date;
    }
    if (result != 0)
        set_error(err, err_size, "invalid, unsupported, or unsafe ZIP archive");
done:
    if (in != NULL)
        fclose(in);
    return result;
}
