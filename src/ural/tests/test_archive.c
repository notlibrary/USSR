#if !defined(_WIN32) && !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200809L
#endif

#include "../native/ural_archive.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#ifdef _WIN32
#include <direct.h>
#include <process.h>
#define mkdir_one(path) _mkdir(path)
#define rmdir_one(path) _rmdir(path)
#define process_id() ((unsigned long)_getpid())
#else
#include <unistd.h>
#define mkdir_one(path) mkdir(path, 0700)
#define rmdir_one(path) rmdir(path)
#define process_id() ((unsigned long)getpid())
#endif

static int files_seen;

static int count_file(void *context, const char *path)
{
    (void)context;
    if (!ural_archive_path_is_safe(path))
        return -1;
    ++files_seen;
    return 0;
}

static int write_text(const char *path, const char *text)
{
    FILE *file = fopen(path, "wb");
    size_t length = strlen(text);
    int ok, closed;
    if (file == NULL)
        return -1;
    ok = fwrite(text, 1, length, file) == length;
    closed = fclose(file) == 0;
    return ok && closed ? 0 : -1;
}

static int write_u16(FILE *file, unsigned value)
{
    unsigned char bytes[2] = {(unsigned char)value, (unsigned char)(value >> 8)};
    return fwrite(bytes, 1, sizeof bytes, file) == sizeof bytes ? 0 : -1;
}

static int write_u32(FILE *file, unsigned value)
{
    unsigned char bytes[4] = {(unsigned char)value, (unsigned char)(value >> 8),
                              (unsigned char)(value >> 16), (unsigned char)(value >> 24)};
    return fwrite(bytes, 1, sizeof bytes, file) == sizeof bytes ? 0 : -1;
}

static int contents_equal(const char *path, const char *expected)
{
    char buffer[64];
    FILE *file = fopen(path, "rb");
    size_t length = strlen(expected);
    int ok;
    if (file == NULL)
        return 0;
    ok = fread(buffer, 1, sizeof buffer, file) == length &&
         memcmp(buffer, expected, length) == 0;
    fclose(file);
    return ok;
}

int main(void)
{
    char root[96], source[128], nested[160], archive[128], bad_archive[128];
    char output[128], file[160], empty_file[160], escape_path[128];
    char hash[17], error[160] = {0};
    FILE *bad;
    int rc;
    snprintf(root, sizeof root, "ural-archive-test-%lu", process_id());
    snprintf(source, sizeof source, "%s/source", root);
    snprintf(nested, sizeof nested, "%s/source/nested", root);
    snprintf(archive, sizeof archive, "%s/test.ural", root);
    snprintf(bad_archive, sizeof bad_archive, "%s/bad.ural", root);
    snprintf(output, sizeof output, "%s/output", root);
    snprintf(file, sizeof file, "%s/source/nested/data.txt", root);
    snprintf(empty_file, sizeof empty_file, "%s/source/empty", root);
    if (mkdir_one(root) != 0 || mkdir_one(source) != 0 || mkdir_one(nested) != 0 ||
        write_text(file, "archive round-trip") != 0 ||
        write_text(empty_file, "") != 0) {
        fputs("could not create archive test fixture\n", stderr);
        return 1;
    }
    if (ural_hash_file(file, hash) != 0 ||
        ural_archive_pack(source, archive, error, sizeof error) != 0 ||
        ural_archive_extract(archive, output, count_file, NULL, error, sizeof error) != 0) {
        fprintf(stderr, "archive operation failed: %s\n", error);
        return 1;
    }
    snprintf(file, sizeof file, "%s/output/nested/data.txt", root);
    if (strcmp(hash, "a7abc6d72ad154db") != 0 ||
        !contents_equal(file, "archive round-trip") || files_seen != 2) {
        fputs("archive contents or FNV-1a hash failed\n", stderr);
        return 1;
    }
    if (ural_archive_path_is_safe("../escape") ||
        ural_archive_path_is_safe("/absolute") ||
        ural_archive_path_is_safe("C:/drive") ||
        ural_archive_path_is_safe("a/../escape") ||
        !ural_archive_path_is_safe("nested/data.txt")) {
        fputs("archive path validation failed\n", stderr);
        return 1;
    }
    bad = fopen(bad_archive, "wb");
    if (bad == NULL ||
        write_u32(bad, 0x04034b50u) != 0 || write_u16(bad, 20) != 0 ||
        write_u16(bad, 0) != 0 || write_u16(bad, 0) != 0 ||
        write_u16(bad, 0) != 0 || write_u16(bad, 0) != 0 ||
        write_u32(bad, 0) != 0 || write_u32(bad, 0) != 0 ||
        write_u32(bad, 0) != 0 || write_u16(bad, 9) != 0 ||
        write_u16(bad, 0) != 0 || fwrite("../escape", 1, 9, bad) != 9 ||
        write_u32(bad, 0x02014b50u) != 0 || fclose(bad) != 0) {
        fputs("could not create unsafe archive fixture\n", stderr);
        return 1;
    }
    if (ural_archive_extract(bad_archive, output, NULL, NULL, error, sizeof error) == 0) {
        fputs("archive extractor accepted a traversal entry\n", stderr);
        return 1;
    }
    snprintf(escape_path, sizeof escape_path, "%s/escape", root);
    bad = fopen(escape_path, "rb");
    if (bad != NULL) {
        fclose(bad);
        fputs("unsafe archive wrote outside extraction root\n", stderr);
        return 1;
    }
    remove(file);
    remove(bad_archive);
    remove(empty_file);
    snprintf(file, sizeof file, "%s/source/nested/data.txt", root);
    remove(file);
    snprintf(file, sizeof file, "%s/output/empty", root); remove(file);
    snprintf(file, sizeof file, "%s/output/nested/data.txt", root); remove(file);
    snprintf(file, sizeof file, "%s/output/nested", root); rmdir_one(file);
    snprintf(file, sizeof file, "%s/output", root); rmdir_one(file);
    snprintf(file, sizeof file, "%s/source/nested", root); rmdir_one(file);
    snprintf(file, sizeof file, "%s/source", root); rmdir_one(file);
    remove(archive);
    rc = rmdir_one(root);
    if (rc != 0) {
        fputs("could not clean archive test fixture\n", stderr);
        return 1;
    }
    puts("ural_archive tests: OK");
    return 0;
}
