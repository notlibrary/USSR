#if !defined(_WIN32) && !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200809L
#endif

#include "ural_archive.h"
#include "ural_db.h"
#include "ural_manifest.h"
#include "ural_url.h"

#include <curl/curl.h>
#include <ctype.h>
#include <errno.h>
#include <openssl/ssl.h>
#include <sqlite3.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#ifdef _WIN32
#include <direct.h>
#include <io.h>
#include <windows.h>
#define URAL_MKDIR(path) _mkdir(path)
#define URAL_RMDIR(path) _rmdir(path)
#define URAL_UNLINK(path) _unlink(path)
#else
#include <dirent.h>
#include <errno.h>
#include <unistd.h>
#define URAL_MKDIR(path) mkdir(path, 0700)
#define URAL_RMDIR(path) rmdir(path)
#define URAL_UNLINK(path) unlink(path)
#endif

#define URAL_MAX_MANIFEST (1024u * 1024u)
#define URAL_MAX_DOWNLOAD (512u * 1024u * 1024u)

static void set_error(char *err, size_t cap, const char *message)
{
    if (err != NULL && cap > 0)
        snprintf(err, cap, "%s", message != NULL ? message : "unknown error");
}

static char *duplicate(const char *text)
{
    size_t length = strlen(text) + 1;
    char *copy = (char *)malloc(length);
    if (copy != NULL)
        memcpy(copy, text, length);
    return copy;
}

static char *join_path(const char *a, const char *b)
{
    size_t length = strlen(a);
    size_t other = strlen(b);
    while (length > 1 && (a[length - 1] == '/' || a[length - 1] == '\\')
#ifdef _WIN32
           && !(length == 3 && a[1] == ':')
#endif
    )
        --length;
    char *path = (char *)malloc(length + other + 2);
    if (path == NULL)
        return NULL;
    memcpy(path, a, length);
#ifdef _WIN32
    if (length > 0 && a[length - 1] != '/' && a[length - 1] != '\\')
        path[length++] = '\\';
#else
    if (length > 0 && a[length - 1] != '/')
        path[length++] = '/';
#endif
    memcpy(path + length, b, other + 1);
    return path;
}

static int make_directories(const char *path)
{
    char *copy = duplicate(path), *p;
    if (copy == NULL)
        return -1;
    for (p = copy + 1; *p; ++p) {
        if (*p == '/' || *p == '\\') {
            char saved = *p;
            *p = '\0';
            if (URAL_MKDIR(copy) != 0) {
#ifdef _WIN32
                DWORD attrs = GetFileAttributesA(copy);
                if (attrs == INVALID_FILE_ATTRIBUTES ||
                    !(attrs & FILE_ATTRIBUTE_DIRECTORY)) {
                    free(copy);
                    return -1;
                }
#else
                struct stat st;
                if (stat(copy, &st) != 0 || !S_ISDIR(st.st_mode)) {
                    free(copy);
                    return -1;
                }
#endif
            }
            *p = saved;
        }
    }
    if (URAL_MKDIR(copy) != 0) {
#ifdef _WIN32
        DWORD attrs = GetFileAttributesA(copy);
        if (attrs == INVALID_FILE_ATTRIBUTES || !(attrs & FILE_ATTRIBUTE_DIRECTORY)) {
            free(copy);
            return -1;
        }
#else
        struct stat st;
        if (stat(copy, &st) != 0 || !S_ISDIR(st.st_mode)) {
            free(copy);
            return -1;
        }
#endif
    }
    free(copy);
    return 0;
}

static int remove_tree(const char *path)
{
#ifdef _WIN32
    char *pattern = join_path(path, "*");
    WIN32_FIND_DATAA data;
    HANDLE find;
    if (pattern == NULL)
        return -1;
    find = FindFirstFileA(pattern, &data);
    free(pattern);
    if (find != INVALID_HANDLE_VALUE) {
        do {
            char *child;
            int rc;
            if (!strcmp(data.cFileName, ".") || !strcmp(data.cFileName, ".."))
                continue;
            child = join_path(path, data.cFileName);
            if (child == NULL) { FindClose(find); return -1; }
            if (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                if (data.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)
                    rc = URAL_RMDIR(child);
                else
                    rc = remove_tree(child);
            }
            else
                rc = URAL_UNLINK(child);
            free(child);
            if (rc != 0) { FindClose(find); return -1; }
        } while (FindNextFileA(find, &data));
        FindClose(find);
    } else if (GetLastError() != ERROR_FILE_NOT_FOUND) {
        return -1;
    }
#else
    {
        struct stat root_stat;
        if (lstat(path, &root_stat) == 0 && S_ISLNK(root_stat.st_mode))
            return URAL_UNLINK(path);
    }
    DIR *dir = opendir(path);
    struct dirent *item;
    if (dir != NULL) {
        while ((item = readdir(dir)) != NULL) {
            char *child;
            struct stat st;
            int rc;
            if (!strcmp(item->d_name, ".") || !strcmp(item->d_name, ".."))
                continue;
            child = join_path(path, item->d_name);
            if (child == NULL) { closedir(dir); return -1; }
            if (lstat(child, &st) != 0) {
                free(child);
                closedir(dir);
                return -1;
            }
            if (S_ISDIR(st.st_mode) && !S_ISLNK(st.st_mode))
                rc = remove_tree(child);
            else
                rc = URAL_UNLINK(child);
            free(child);
            if (rc != 0) { closedir(dir); return -1; }
        }
        closedir(dir);
    } else {
        struct stat st;
        if (lstat(path, &st) == 0 && !S_ISDIR(st.st_mode))
            return URAL_UNLINK(path);
        if (errno != ENOENT)
            return -1;
    }
#endif
    return URAL_RMDIR(path) == 0 ? 0 : -1;
}

static char *default_state_dir(void)
{
    const char *home;
    char *path;
#ifdef _WIN32
    home = getenv("LOCALAPPDATA");
    if (home == NULL || *home == '\0')
        home = getenv("USERPROFILE");
    if (home == NULL || *home == '\0')
        return NULL;
    path = join_path(home, "USSR");
#else
    home = getenv("HOME");
    if (home == NULL || *home == '\0')
        return NULL;
    path = join_path(home, ".ussr");
#endif
    return path;
}

static int url_has_scheme(const char *url, const char *scheme)
{
    size_t length = strlen(scheme);
    const unsigned char *p;
    if (url == NULL || strncmp(url, scheme, length) != 0 || url[length] == '\0')
        return 0;
    p = (const unsigned char *)url;
    for (; *p; ++p) {
        if (*p <= 0x20 || *p == 0x7f)
            return 0;
    }
    return 1;
}

static int valid_base_url(const char *url, const char *scheme)
{
    size_t length = strlen(scheme);
    const char *p;
    if (!url_has_scheme(url, scheme) || url[length] == '/' ||
        strchr(url, '?') != NULL || strchr(url, '#') != NULL ||
        strchr(url + length, '@') != NULL)
        return 0;
    for (p = url + length; *p != '\0' && *p != '/'; ++p) {
        if (*p == '\\' || *p == '%')
            return 0;
    }
    return p != url + length;
}

static char *url_join(const char *base, const char *leaf)
{
    size_t a = strlen(base), b = strlen(leaf);
    int slash = a > 0 && base[a - 1] != '/';
    char *url = (char *)malloc(a + (size_t)slash + b + 1);
    if (url == NULL)
        return NULL;
    memcpy(url, base, a);
    if (slash)
        url[a++] = '/';
    memcpy(url + a, leaf, b + 1);
    return url;
}

static size_t normalized_server_length(const char *server)
{
    size_t length = strlen(server);
    while (length > 8 && server[length - 1] == '/')
        --length;
    return length;
}

typedef struct download_stream {
    FILE *file;
    size_t total;
} download_stream_t;

static size_t curl_write_file(char *buffer, size_t size, size_t count, void *context)
{
    download_stream_t *stream = (download_stream_t *)context;
    size_t bytes;
    if (size != 0 && count > SIZE_MAX / size)
        return 0;
    bytes = size * count;
    if (bytes > URAL_MAX_DOWNLOAD - stream->total)
        return 0;
    bytes = fwrite(buffer, 1, bytes, stream->file);
    stream->total += bytes;
    return bytes;
}

static int curl_download(const char *url, const char *path, char *err, size_t cap)
{
    CURL *curl = curl_easy_init();
    FILE *file;
    download_stream_t stream;
    CURLcode rc;
    long response_code = 0;
    long ssl_verify_result = 0;
    const char *ca_bundle = getenv("CURL_CA_BUNDLE");
    if (ca_bundle == NULL || ca_bundle[0] == '\0')
        ca_bundle = getenv("SSL_CERT_FILE");
    if (curl == NULL) {
        set_error(err, cap, "cannot initialize HTTPS transport");
        return -1;
    }
    file = fopen(path, "wb");
    if (file == NULL) {
        curl_easy_cleanup(curl);
        set_error(err, cap, "cannot create download file");
        return -1;
    }
    stream.file = file;
    stream.total = 0;
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_write_file);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &stream);
    curl_easy_setopt(curl, CURLOPT_PROTOCOLS, CURLPROTO_HTTPS);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 0L);
    curl_easy_setopt(curl, CURLOPT_MAXFILESIZE_LARGE, (curl_off_t)URAL_MAX_DOWNLOAD);
    curl_easy_setopt(curl, CURLOPT_FAILONERROR, 1L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);
    if (ca_bundle != NULL && ca_bundle[0] != '\0') {
        curl_easy_setopt(curl, CURLOPT_CAINFO, ca_bundle);
    }
#if defined(_WIN32) && defined(CURLSSLOPT_NATIVE_CA)
    else {
        curl_easy_setopt(curl, CURLOPT_SSL_OPTIONS, (long)CURLSSLOPT_NATIVE_CA);
    }
#endif
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 30L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 300L);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
    rc = curl_easy_perform(curl);
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response_code);
    curl_easy_getinfo(curl, CURLINFO_SSL_VERIFYRESULT, &ssl_verify_result);
    if (fclose(file) != 0 && rc == CURLE_OK)
        rc = CURLE_WRITE_ERROR;
    if (rc != CURLE_OK) {
        if (err != NULL && cap > 0) {
            if (rc == CURLE_PEER_FAILED_VERIFICATION)
                snprintf(err, cap,
                         "HTTPS certificate verification failed (TLS verify result %ld)",
                         ssl_verify_result);
            else if (response_code != 0)
                snprintf(err, cap, "HTTPS request failed with HTTP %ld: %s",
                         response_code, curl_easy_strerror(rc));
            else
                snprintf(err, cap, "HTTPS request failed: %s", curl_easy_strerror(rc));
        }
        URAL_UNLINK(path);
        curl_easy_cleanup(curl);
        return -1;
    }
    curl_easy_cleanup(curl);
    return 0;
}

static int read_text_file(const char *path, char **text, size_t limit)
{
    FILE *file = fopen(path, "rb");
    long length;
    char *buffer;
    if (file == NULL)
        return -1;
    if (fseek(file, 0, SEEK_END) != 0 || (length = ftell(file)) < 0 ||
        (unsigned long)length > limit || fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        return -1;
    }
    buffer = (char *)malloc((size_t)length + 1);
    if (buffer == NULL || fread(buffer, 1, (size_t)length, file) != (size_t)length) {
        free(buffer);
        fclose(file);
        return -1;
    }
    fclose(file);
    if (memchr(buffer, '\0', (size_t)length) != NULL) {
        free(buffer);
        return -1;
    }
    buffer[length] = '\0';
    *text = buffer;
    return 0;
}

static int db_exec(sqlite3 *db, const char *sql, char *err, size_t cap)
{
    char *message = NULL;
    if (sqlite3_exec(db, sql, NULL, NULL, &message) != SQLITE_OK) {
        set_error(err, cap, message != NULL ? message : sqlite3_errmsg(db));
        sqlite3_free(message);
        return -1;
    }
    return 0;
}

static int db_add_index(ural_db_t *db, const char *server, const char *name,
                        const char *version, const char *metadata_url,
                        const char *archive_url, const char *metadata_hash,
                        const char *archive_hash, const char *public_url,
                        char *err, size_t cap)
{
    sqlite3_stmt *stmt = NULL;
    const char *sql =
        "INSERT INTO repository_index(server_url,package_name,version,metadata_url,"
        "archive_url,archive_hash,metadata_hash) VALUES(?1,?2,?3,?4,?5,?6,?7) "
        "ON CONFLICT(server_url,package_name,version) DO UPDATE SET "
        "metadata_url=excluded.metadata_url,archive_url=excluded.archive_url,"
        "archive_hash=excluded.archive_hash,metadata_hash=excluded.metadata_hash";
    int rc = sqlite3_prepare_v2(db->handle, sql, -1, &stmt, NULL);
    if (rc == SQLITE_OK) rc = sqlite3_bind_text(stmt, 1, server, -1, SQLITE_TRANSIENT);
    if (rc == SQLITE_OK) rc = sqlite3_bind_text(stmt, 2, name, -1, SQLITE_TRANSIENT);
    if (rc == SQLITE_OK) rc = sqlite3_bind_text(stmt, 3, version, -1, SQLITE_TRANSIENT);
    if (rc == SQLITE_OK) rc = sqlite3_bind_text(stmt, 4, metadata_url, -1, SQLITE_TRANSIENT);
    if (rc == SQLITE_OK) rc = sqlite3_bind_text(stmt, 5, archive_url, -1, SQLITE_TRANSIENT);
    if (rc == SQLITE_OK) rc = sqlite3_bind_text(stmt, 6, archive_hash, -1, SQLITE_TRANSIENT);
    if (rc == SQLITE_OK) rc = sqlite3_bind_text(stmt, 7, metadata_hash, -1, SQLITE_TRANSIENT);
    if (rc == SQLITE_OK) rc = sqlite3_step(stmt);
    if (rc != SQLITE_DONE) {
        set_error(err, cap, sqlite3_errmsg(db->handle));
        sqlite3_finalize(stmt);
        return -1;
    }
    sqlite3_finalize(stmt);
    sql = "INSERT INTO repository_config(server_url,public_url) VALUES(?1,?2) "
          "ON CONFLICT(server_url) DO UPDATE SET public_url=excluded.public_url";
    rc = sqlite3_prepare_v2(db->handle, sql, -1, &stmt, NULL);
    if (rc == SQLITE_OK) rc = sqlite3_bind_text(stmt, 1, server, -1, SQLITE_TRANSIENT);
    if (rc == SQLITE_OK) rc = sqlite3_bind_text(stmt, 2, public_url, -1, SQLITE_TRANSIENT);
    if (rc == SQLITE_OK) rc = sqlite3_step(stmt);
    if (rc != SQLITE_DONE) {
        set_error(err, cap, sqlite3_errmsg(db->handle));
        sqlite3_finalize(stmt);
        return -1;
    }
    sqlite3_finalize(stmt);
    return 0;
}

static int load_manifest_from_dir(const char *directory, char **text,
                                  ural_manifest_t *manifest, char *err, size_t cap)
{
    char *path = join_path(directory, "box.uno");
    int result;
    if (path == NULL)
        return -1;
    if (read_text_file(path, text, URAL_MAX_MANIFEST) != 0) {
        set_error(err, cap, "cannot read box.uno");
        free(path);
        return -1;
    }
    free(path);
    result = ural_manifest_parse(*text, manifest, err, cap);
    if (result != 0) {
        free(*text);
        *text = NULL;
    }
    return result;
}

static int pack_one(const char *directory, const char *repo_root,
                    const char *public_url, ural_db_t *db,
                    char *err, size_t cap)
{
    char *manifest_text = NULL, *archive_path = NULL;
    char *metadata_url = NULL, *archive_url = NULL, *sidecar = NULL;
    char *versioned_sidecar = NULL;
    ural_manifest_t manifest;
    char archive_name[512], sidecar_name[512], versioned_sidecar_name[512];
    char archive_hash[17], metadata_hash[17];
    FILE *metadata_file = NULL;
    int result = -1;
    if (load_manifest_from_dir(directory, &manifest_text, &manifest, err, cap) != 0)
        return -1;
    if (snprintf(archive_name, sizeof archive_name, "%s-%s.ural",
                 manifest.name, manifest.version) >= (int)sizeof archive_name ||
        snprintf(sidecar_name, sizeof sidecar_name, "%s-box.uno",
                 manifest.name) >= (int)sizeof sidecar_name ||
        snprintf(versioned_sidecar_name, sizeof versioned_sidecar_name,
                 "%s-%s-box.uno", manifest.name, manifest.version) >=
                 (int)sizeof versioned_sidecar_name) {
        set_error(err, cap, "package name or version is too long");
        goto done;
    }
    archive_path = join_path(repo_root, archive_name);
    sidecar = join_path(repo_root, sidecar_name);
    versioned_sidecar = join_path(repo_root, versioned_sidecar_name);
    metadata_url = url_join(public_url, versioned_sidecar_name);
    archive_url = url_join(public_url, archive_name);
    if (!archive_path || !sidecar || !versioned_sidecar || !metadata_url || !archive_url) {
        set_error(err, cap, "out of memory while preparing package");
        goto done;
    }
    if (ural_archive_pack(directory, archive_path, err, cap) != 0 ||
        ural_hash_file(archive_path, archive_hash) != 0) {
        if (err[0] == '\0') set_error(err, cap, "cannot hash package archive");
        goto done;
    }
    metadata_file = fopen(versioned_sidecar, "wb");
    if (metadata_file == NULL ||
        fwrite(manifest_text, 1, strlen(manifest_text), metadata_file) != strlen(manifest_text)) {
        set_error(err, cap, "cannot write package metadata sidecar");
        goto done;
    }
    if (fclose(metadata_file) != 0) {
        metadata_file = NULL;
        set_error(err, cap, "cannot finish package metadata sidecar");
        goto done;
    }
    metadata_file = NULL;
    metadata_file = fopen(sidecar, "wb");
    if (metadata_file == NULL ||
        fwrite(manifest_text, 1, strlen(manifest_text), metadata_file) != strlen(manifest_text)) {
        set_error(err, cap, "cannot write package metadata alias");
        goto done;
    }
    if (fclose(metadata_file) != 0) {
        metadata_file = NULL;
        set_error(err, cap, "cannot finish package metadata alias");
        goto done;
    }
    metadata_file = NULL;
    if (ural_hash_file(versioned_sidecar, metadata_hash) != 0 ||
        db_add_index(db, public_url, manifest.name, manifest.version,
                     metadata_url, archive_url, metadata_hash, archive_hash,
                     public_url, err, cap) != 0)
        goto done;
    printf("packed %s %s\n", manifest.name, manifest.version);
    result = 0;
done:
    if (metadata_file != NULL)
        fclose(metadata_file);
    free(manifest_text);
    free(archive_path);
    free(metadata_url);
    free(archive_url);
    free(sidecar);
    free(versioned_sidecar);
    ural_manifest_free(&manifest);
    return result;
}

static int scan_packages(const char *directory, const char *root,
                         const char *public_url, ural_db_t *db,
                         char *err, size_t cap)
{
    char *manifest_path = join_path(directory, "box.uno");
    int found;
    if (manifest_path == NULL)
        return -1;
#ifdef _WIN32
    {
        DWORD attributes = GetFileAttributesA(manifest_path);
        found = attributes != INVALID_FILE_ATTRIBUTES &&
                !(attributes & FILE_ATTRIBUTE_DIRECTORY);
    }
#else
    {
        struct stat st;
        found = stat(manifest_path, &st) == 0 && !S_ISDIR(st.st_mode);
    }
#endif
    free(manifest_path);
    if (found)
        return pack_one(directory, root, public_url, db, err, cap);
#ifdef _WIN32
    {
        char *pattern = join_path(directory, "*");
        WIN32_FIND_DATAA data;
        HANDLE find;
        int result = 0;
        if (pattern == NULL) return -1;
        find = FindFirstFileA(pattern, &data);
        free(pattern);
        if (find == INVALID_HANDLE_VALUE)
            return GetLastError() == ERROR_FILE_NOT_FOUND ? 0 : -1;
        do {
            char *child;
            if (!(data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ||
                (data.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) ||
                !strcmp(data.cFileName, ".") || !strcmp(data.cFileName, ".."))
                continue;
            child = join_path(directory, data.cFileName);
            if (child == NULL) { result = -1; break; }
            result = scan_packages(child, root, public_url, db, err, cap);
            free(child);
            if (result != 0) break;
        } while (FindNextFileA(find, &data));
        FindClose(find);
        return result;
    }
#else
    {
        DIR *dir = opendir(directory);
        struct dirent *item;
        int result = 0;
        if (dir == NULL) return -1;
        while ((item = readdir(dir)) != NULL) {
            char *child;
            struct stat child_stat;
            if (!strcmp(item->d_name, ".") || !strcmp(item->d_name, ".."))
                continue;
            child = join_path(directory, item->d_name);
            if (child == NULL) { result = -1; break; }
            if (lstat(child, &child_stat) == 0 && S_ISDIR(child_stat.st_mode) &&
                !S_ISLNK(child_stat.st_mode))
                result = scan_packages(child, root, public_url, db, err, cap);
            free(child);
            if (result != 0) break;
        }
        closedir(dir);
        return result;
    }
#endif
}

static int command_pack(const char *root, const char *public_url)
{
    ural_db_t db = {0};
    char *dbpath = join_path(root, "ussr.boxes");
    char err[512] = {0};
    int result;
    if (!valid_base_url(public_url, "https://")) {
        fprintf(stderr, "ural: pack public URL must be an HTTPS base URL without credentials, query, or fragment\n");
        free(dbpath);
        return 1;
    }
    if (dbpath == NULL || ural_db_open(&db, dbpath, err, sizeof err) != 0) {
        fprintf(stderr, "ural: %s\n", dbpath == NULL ? "out of memory" : err);
        free(dbpath);
        return 1;
    }
    if (db_exec(db.handle, "BEGIN IMMEDIATE", err, sizeof err) != 0) {
        fprintf(stderr, "ural: %s\n", err);
        ural_db_close(&db);
        free(dbpath);
        return 1;
    }
    result = scan_packages(root, root, public_url, &db, err, sizeof err);
    if (result != 0) {
        db_exec(db.handle, "ROLLBACK", NULL, 0);
        fprintf(stderr, "ural: pack failed: %s\n", err[0] ? err : "cannot scan package directory");
    } else if (db_exec(db.handle, "COMMIT", err, sizeof err) != 0) {
        db_exec(db.handle, "ROLLBACK", NULL, 0);
        fprintf(stderr, "ural: failed to commit repository index: %s\n", err);
        result = -1;
    }
    ural_db_close(&db);
    free(dbpath);
    return result == 0 ? 0 : 1;
}

static int ftp_upload_file(CURL *curl, const char *path, const char *base,
                           const char *login, const char *password,
                           const char *name, char *err, size_t cap)
{
    char *escaped = NULL, *url = NULL;
    FILE *file = NULL;
    struct stat st;
    CURLcode rc;
    int result = -1;
    escaped = curl_easy_escape(curl, name, 0);
    if (escaped == NULL)
        goto done;
    url = ural_ftp_url_join(base, escaped);
    if (url == NULL)
        goto done;
    file = fopen(path, "rb");
    if (file == NULL) {
        set_error(err, cap, "cannot open file for FTP upload");
        goto done;
    }
    if (stat(path, &st) != 0 || st.st_size < 0) {
        set_error(err, cap, "cannot determine upload file size");
        goto done;
    }
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_USERNAME, login);
    curl_easy_setopt(curl, CURLOPT_PASSWORD, password);
    curl_easy_setopt(curl, CURLOPT_UPLOAD, 1L);
    curl_easy_setopt(curl, CURLOPT_READFUNCTION, NULL);
    curl_easy_setopt(curl, CURLOPT_READDATA, file);
    curl_easy_setopt(curl, CURLOPT_INFILESIZE_LARGE, (curl_off_t)st.st_size);
    curl_easy_setopt(curl, CURLOPT_PROTOCOLS, CURLPROTO_FTP);
    curl_easy_setopt(curl, CURLOPT_FTP_CREATE_MISSING_DIRS, CURLFTP_CREATE_DIR_RETRY);
    curl_easy_setopt(curl, CURLOPT_FAILONERROR, 1L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 30L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 300L);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
    rc = curl_easy_perform(curl);
    if (rc != CURLE_OK) {
        snprintf(err, cap, "FTP upload failed: %s", curl_easy_strerror(rc));
        goto done;
    }
    printf("uploaded %s\n", name);
    result = 0;
done:
    if (file != NULL)
        fclose(file);
    if (escaped != NULL)
        curl_free(escaped);
    free(url);
    curl_easy_setopt(curl, CURLOPT_UPLOAD, 0L);
    curl_easy_setopt(curl, CURLOPT_USERNAME, NULL);
    curl_easy_setopt(curl, CURLOPT_PASSWORD, NULL);
    return result;
}

static int upload_if_package_file(CURL *curl, const char *root, const char *name,
                                  const char *base, const char *login,
                                  const char *password, char *err, size_t cap)
{
    char *path;
    size_t n = strlen(name);
    int is_upload = (n >= 5 && strcmp(name + n - 5, ".ural") == 0) ||
                    (n >= 8 && strcmp(name + n - 8, "-box.uno") == 0) ||
                    strcmp(name, "ussr.boxes") == 0;
    int result;
    if (!is_upload)
        return 0;
    path = join_path(root, name);
    if (path == NULL)
        return -1;
    result = ftp_upload_file(curl, path, base, login, password, name, err, cap);
    free(path);
    return result;
}

static int command_upload(const char *root, const char *url,
                          const char *login, const char *password)
{
    CURL *curl;
    char err[512] = {0};
    int result = 0;
    if (!valid_base_url(url, "ftp://")) {
        fprintf(stderr, "ural: upload destination must be an FTP base URL without embedded credentials, query, or fragment\n");
        return 1;
    }
    if (curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK) {
        fprintf(stderr, "ural: cannot initialize cURL\n");
        return 1;
    }
    fprintf(stderr, "ural: WARNING: FTP sends credentials and package files without encryption.\n");
    curl = curl_easy_init();
    if (curl == NULL) {
        fprintf(stderr, "ural: cannot initialize FTP transport\n");
        curl_global_cleanup();
        return 1;
    }
#ifdef _WIN32
    {
        char *pattern = join_path(root, "*");
        WIN32_FIND_DATAA data;
        HANDLE find;
        if (pattern == NULL) { curl_easy_cleanup(curl); return 1; }
        find = FindFirstFileA(pattern, &data);
        free(pattern);
        if (find == INVALID_HANDLE_VALUE) {
            fprintf(stderr, "ural: cannot scan upload directory\n");
            curl_easy_cleanup(curl);
            curl_global_cleanup();
            return 1;
        }
        do {
            if (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
                continue;
            result = upload_if_package_file(curl, root, data.cFileName,
                                            url, login, password, err, sizeof err);
            if (result != 0) break;
        } while (FindNextFileA(find, &data));
        FindClose(find);
    }
#else
    {
        DIR *dir = opendir(root);
        struct dirent *item;
        if (dir == NULL) {
            fprintf(stderr, "ural: cannot scan upload directory\n");
            curl_easy_cleanup(curl);
            curl_global_cleanup();
            return 1;
        }
        while ((item = readdir(dir)) != NULL) {
            struct stat st;
            char *path;
            if (!strcmp(item->d_name, ".") || !strcmp(item->d_name, ".."))
                continue;
            path = join_path(root, item->d_name);
            if (path == NULL) { result = -1; break; }
            if (lstat(path, &st) == 0 && S_ISREG(st.st_mode))
                result = upload_if_package_file(curl, root, item->d_name,
                                                url, login, password, err, sizeof err);
            free(path);
            if (result != 0) break;
        }
        closedir(dir);
    }
#endif
    if (result != 0)
        fprintf(stderr, "ural: %s\n", err[0] ? err : "upload failed");
    curl_easy_cleanup(curl);
    curl_global_cleanup();
    return result == 0 ? 0 : 1;
}

typedef struct remote_package {
    char *version;
    char *server;
    char *metadata_url;
    char *archive_url;
    char *metadata_hash;
    char *archive_hash;
} remote_package_t;

typedef struct installed_file {
    char *path;
    char hash[17];
} installed_file_t;

typedef struct installed_files {
    char *root;
    installed_file_t *items;
    size_t count;
    size_t capacity;
} installed_files_t;

static unsigned long process_id(void)
{
#ifdef _WIN32
    return (unsigned long)GetCurrentProcessId();
#else
    return (unsigned long)getpid();
#endif
}

static void remote_package_free(remote_package_t *package)
{
    free(package->version);
    free(package->server);
    free(package->metadata_url);
    free(package->archive_url);
    free(package->metadata_hash);
    free(package->archive_hash);
    memset(package, 0, sizeof(*package));
}

static int url_belongs_to_server(const char *url, const char *server)
{
    size_t length = normalized_server_length(server);
    return url_has_scheme(url, "https://") &&
           strncmp(url, server, length) == 0 &&
           (url[length] == '\0' || url[length] == '/');
}

static int version_compare(const char *left, const char *right)
{
    const unsigned char *a = (const unsigned char *)left;
    const unsigned char *b = (const unsigned char *)right;
    while (*a != '\0' && *b != '\0') {
        if (isdigit(*a) && isdigit(*b)) {
            const unsigned char *a_start, *b_start;
            size_t a_len, b_len;
            while (*a == '0') ++a;
            while (*b == '0') ++b;
            a_start = a;
            b_start = b;
            while (isdigit(*a)) ++a;
            while (isdigit(*b)) ++b;
            a_len = (size_t)(a - a_start);
            b_len = (size_t)(b - b_start);
            if (a_len != b_len) return a_len < b_len ? -1 : 1;
            if (a_len != 0) {
                int cmp = memcmp(a_start, b_start, a_len);
                if (cmp != 0) return cmp < 0 ? -1 : 1;
            }
            continue;
        }
        if (*a != *b)
            return *a < *b ? -1 : 1;
        ++a;
        ++b;
    }
    if (*a == '\0' && *b == '\0') return 0;
    return *a == '\0' ? 1 : -1;
}

static int copy_column(sqlite3_stmt *stmt, int column, char **out)
{
    const unsigned char *value = sqlite3_column_text(stmt, column);
    if (value == NULL)
        return -1;
    *out = duplicate((const char *)value);
    return *out == NULL ? -1 : 0;
}

static int read_remote_package(sqlite3 *index, const char *server,
                               const char *name, remote_package_t *out)
{
    sqlite3_stmt *stmt = NULL;
    int rc;
    const char *sql =
        "SELECT version,metadata_url,archive_url,metadata_hash,archive_hash "
        "FROM repository_index WHERE package_name=?1";
    rc = sqlite3_prepare_v2(index, sql, -1, &stmt, NULL);
    if (rc == SQLITE_OK) rc = sqlite3_bind_text(stmt, 1, name, -1, SQLITE_TRANSIENT);
    if (rc != SQLITE_OK) {
        sqlite3_finalize(stmt);
        return -1;
    }
    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
        remote_package_t candidate = {0};
        if (copy_column(stmt, 0, &candidate.version) != 0 ||
            copy_column(stmt, 1, &candidate.metadata_url) != 0 ||
            copy_column(stmt, 2, &candidate.archive_url) != 0 ||
            copy_column(stmt, 3, &candidate.metadata_hash) != 0 ||
            copy_column(stmt, 4, &candidate.archive_hash) != 0) {
            remote_package_free(&candidate);
            continue;
        }
        candidate.server = duplicate(server);
        if (candidate.server == NULL ||
            !ural_manifest_validate_component(candidate.version) ||
            !url_belongs_to_server(candidate.metadata_url, server) ||
            !url_belongs_to_server(candidate.archive_url, server) ||
            strlen(candidate.archive_hash) != 16) {
            remote_package_free(&candidate);
            continue;
        }
        if (out->version == NULL || version_compare(candidate.version, out->version) > 0) {
            remote_package_free(out);
            *out = candidate;
        } else {
            remote_package_free(&candidate);
        }
    }
    sqlite3_finalize(stmt);
    return rc == SQLITE_DONE ? 0 : -1;
}

static int find_remote_package(sqlite3 *local, const char *state,
                               const char *name, remote_package_t *out,
                               char *err, size_t cap)
{
    sqlite3_stmt *servers = NULL;
    unsigned int index_number = 0;
    int rc, found = 0, indexes_read = 0, servers_seen = 0;
    if (sqlite3_prepare_v2(local,
            "SELECT url FROM trusted_servers WHERE enabled=1 ORDER BY url",
            -1, &servers, NULL) != SQLITE_OK) {
        set_error(err, cap, sqlite3_errmsg(local));
        return -1;
    }
    while ((rc = sqlite3_step(servers)) == SQLITE_ROW) {
        const unsigned char *server_text = sqlite3_column_text(servers, 0);
        char index_name[80];
        char *index_path, *index_url;
        sqlite3 *remote = NULL;
        char server_err[512] = {0};
        ++servers_seen;
        if (server_text == NULL)
            continue;
        snprintf(index_name, sizeof index_name, ".ural-index-%lu-%u.sqlite",
                 process_id(), index_number++);
        index_path = join_path(state, index_name);
        index_url = url_join((const char *)server_text, "ussr.boxes");
        if (index_path == NULL || index_url == NULL) {
            free(index_path);
            free(index_url);
            set_error(err, cap, "out of memory");
            sqlite3_finalize(servers);
            return -1;
        }
        if (curl_download(index_url, index_path, server_err, sizeof server_err) == 0) {
            int open_rc = sqlite3_open_v2(index_path, &remote, SQLITE_OPEN_READONLY, NULL);
            if (open_rc == SQLITE_OK) {
                int read_rc = read_remote_package(remote, (const char *)server_text,
                                                  name, out);
                if (read_rc == 0) {
                    ++indexes_read;
                    if (out->version != NULL)
                        found = 1;
                } else {
                    snprintf(server_err, sizeof server_err,
                             "repository index has an invalid schema: %s",
                             sqlite3_errmsg(remote));
                }
            } else {
                snprintf(server_err, sizeof server_err,
                         "repository index is not a readable SQLite database: %s",
                         remote != NULL ? sqlite3_errmsg(remote) : "cannot open file");
            }
        }
        if (server_err[0] != '\0')
            snprintf(err, cap, "cannot read repository index %s: %.400s",
                     index_url, server_err);
        if (remote != NULL)
            sqlite3_close(remote);
        URAL_UNLINK(index_path);
        free(index_path);
        free(index_url);
        if (found)
            break;
    }
    if (rc != SQLITE_DONE && rc != SQLITE_ROW && !found) {
        set_error(err, cap, sqlite3_errmsg(local));
        sqlite3_finalize(servers);
        return -1;
    }
    sqlite3_finalize(servers);
    if (!found) {
        if (indexes_read > 0)
            snprintf(err, cap, "package '%s' was not found on any trusted repository", name);
        else if (servers_seen == 0)
            set_error(err, cap, "no enabled trusted repositories are configured");
        else if (err != NULL && cap > 0 && err[0] == '\0')
            set_error(err, cap, "could not download a trusted repository index");
        return -1;
    }
    return 0;
}

static char *executable_path(const char *argv0)
{
    (void)argv0;
#ifdef _WIN32
    DWORD capacity = MAX_PATH;
    char *path = NULL;
    for (;;) {
        DWORD length;
        char *grown = (char *)realloc(path, (size_t)capacity + 1);
        if (grown == NULL) {
            free(path);
            return NULL;
        }
        path = grown;
        length = GetModuleFileNameA(NULL, path, capacity);
        if (length == 0) {
            free(path);
            return NULL;
        }
        if (length < capacity) {
            path[length] = '\0';
            return path;
        }
        if (capacity >= 32768) {
            free(path);
            return NULL;
        }
        capacity *= 2;
    }
#elif defined(__linux__)
    size_t capacity = 512;
    char *path = NULL;
    for (;;) {
        ssize_t length;
        char *grown = (char *)realloc(path, capacity);
        if (grown == NULL) {
            free(path);
            return NULL;
        }
        path = grown;
        length = readlink("/proc/self/exe", path, capacity - 1);
        if (length < 0) {
            free(path);
            return NULL;
        }
        if ((size_t)length < capacity - 1) {
            path[length] = '\0';
            return path;
        }
        if (capacity >= 32768) {
            free(path);
            return NULL;
        }
        capacity *= 2;
    }
#else
    if (argv0 != NULL && (strchr(argv0, '/') != NULL || strchr(argv0, '\\') != NULL))
        return duplicate(argv0);
    return NULL;
#endif
}

static int seed_user_database(const char *db_path, const char *state,
                              const char *argv0, char *err, size_t cap)
{
    char *program = NULL, *directory = NULL, *seed = NULL, *temporary = NULL;
    const char *separator;
    struct stat st;
    FILE *input = NULL, *output = NULL;
    unsigned char buffer[32768];
    size_t count;
    int result = 0;

    if (stat(db_path, &st) == 0)
        return 0;
    if (errno != ENOENT) {
        set_error(err, cap, "cannot inspect per-user database path");
        return -1;
    }
    program = executable_path(argv0);
    if (program == NULL)
        return 0;
    separator = strrchr(program, '/');
#ifdef _WIN32
    {
        const char *backslash = strrchr(program, '\\');
        if (backslash != NULL && (separator == NULL || backslash > separator))
            separator = backslash;
    }
#endif
    if (separator == NULL)
        goto done;
    directory = duplicate(program);
    if (directory == NULL) {
        set_error(err, cap, "out of memory locating bundled database");
        result = -1;
        goto done;
    }
    {
        size_t directory_length = (size_t)(separator - program);
#ifdef _WIN32
        if (directory_length == 2 && program[1] == ':')
            ++directory_length;
#endif
        if (directory_length == 0)
            directory_length = 1;
        directory[directory_length] = '\0';
    }
    seed = join_path(directory, "ussr.boxes");
    if (seed == NULL) {
        set_error(err, cap, "out of memory locating bundled database");
        result = -1;
        goto done;
    }
    input = fopen(seed, "rb");
    if (input == NULL) {
        if (errno != ENOENT) {
            set_error(err, cap, "cannot open bundled database template");
            result = -1;
        }
        goto done;
    }
    {
        char temporary_name[96];
        snprintf(temporary_name, sizeof temporary_name, ".ussr.boxes.seed-%lu",
                 process_id());
        temporary = join_path(state, temporary_name);
    }
    if (temporary == NULL || (output = fopen(temporary, "wb")) == NULL) {
        set_error(err, cap, "cannot create per-user database from bundled template");
        result = -1;
        goto done;
    }
    while ((count = fread(buffer, 1, sizeof buffer, input)) != 0) {
        if (fwrite(buffer, 1, count, output) != count) {
            set_error(err, cap, "cannot copy bundled database template");
            result = -1;
            goto done;
        }
    }
    {
        int read_failed = ferror(input);
        int close_failed = fclose(output) != 0;
        output = NULL;
        if (read_failed || close_failed) {
            set_error(err, cap, "cannot finish copying bundled database template");
            result = -1;
            goto done;
        }
    }
#ifdef _WIN32
    {
        if (!MoveFileA(temporary, db_path)) {
            DWORD move_error = GetLastError();
            if (move_error != ERROR_ALREADY_EXISTS && move_error != ERROR_FILE_EXISTS) {
                set_error(err, cap, "cannot install bundled database template");
                result = -1;
            }
        }
    }
#else
    if (link(temporary, db_path) != 0 && errno != EEXIST) {
        set_error(err, cap, "cannot install bundled database template");
        result = -1;
    }
#endif
done:
    if (input != NULL)
        fclose(input);
    if (output != NULL)
        fclose(output);
    if (temporary != NULL)
        URAL_UNLINK(temporary);
    free(program);
    free(directory);
    free(seed);
    free(temporary);
    return result;
}

static int installed_version(sqlite3 *db, const char *name, char **version)
{
    sqlite3_stmt *stmt = NULL;
    int rc = sqlite3_prepare_v2(db,
        "SELECT version FROM packages WHERE name=?1", -1, &stmt, NULL);
    if (rc == SQLITE_OK) rc = sqlite3_bind_text(stmt, 1, name, -1, SQLITE_TRANSIENT);
    if (rc == SQLITE_OK) rc = sqlite3_step(stmt);
    if (rc == SQLITE_ROW) {
        rc = version != NULL ? copy_column(stmt, 0, version) : 0;
        sqlite3_finalize(stmt);
        return rc == 0 ? 1 : -1;
    }
    sqlite3_finalize(stmt);
    return rc == SQLITE_DONE ? 0 : -1;
}

static int installed_file_callback(void *context, const char *relative)
{
    installed_files_t *files = (installed_files_t *)context;
    installed_file_t *grown;
    char *path;
    size_t capacity;
    if (!ural_archive_path_is_safe(relative))
        return -1;
    if (files->count == files->capacity) {
        capacity = files->capacity ? files->capacity * 2 : 32;
        grown = (installed_file_t *)realloc(files->items, capacity * sizeof(*grown));
        if (grown == NULL)
            return -1;
        files->items = grown;
        files->capacity = capacity;
    }
    path = join_path(files->root, relative);
    if (path == NULL || ural_hash_file(path, files->items[files->count].hash) != 0) {
        free(path);
        return -1;
    }
    files->items[files->count].path = duplicate(relative);
    free(path);
    if (files->items[files->count].path == NULL)
        return -1;
    ++files->count;
    return 0;
}

static int file_is_regular(const char *path)
{
#ifdef _WIN32
    DWORD attr = GetFileAttributesA(path);
    return attr != INVALID_FILE_ATTRIBUTES && !(attr & FILE_ATTRIBUTE_DIRECTORY) &&
           !(attr & FILE_ATTRIBUTE_REPARSE_POINT);
#else
    struct stat st;
    return lstat(path, &st) == 0 && S_ISREG(st.st_mode);
#endif
}

static int remove_recorded_package(sqlite3 *db, const char *packages_dir,
                                  const char *name, char *err, size_t cap)
{
    sqlite3_stmt *stmt = NULL;
    char *package_dir = join_path(packages_dir, name);
    int rc, result = -1;
    if (package_dir == NULL)
        return -1;
    if (sqlite3_prepare_v2(db,
            "SELECT relative_path FROM package_files WHERE package_name=?1",
            -1, &stmt, NULL) != SQLITE_OK ||
        sqlite3_bind_text(stmt, 1, name, -1, SQLITE_TRANSIENT) != SQLITE_OK) {
        set_error(err, cap, sqlite3_errmsg(db));
        goto done;
    }
    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
        const unsigned char *relative = sqlite3_column_text(stmt, 0);
        char *path;
        if (relative == NULL || !ural_archive_path_is_safe((const char *)relative)) {
            set_error(err, cap, "installed file record contains an unsafe path");
            goto done;
        }
        path = join_path(package_dir, (const char *)relative);
        if (path == NULL) {
            set_error(err, cap, "out of memory");
            goto done;
        }
        if (file_is_regular(path) && URAL_UNLINK(path) != 0) {
            free(path);
            set_error(err, cap, "cannot remove an installed package file");
            goto done;
        }
        free(path);
    }
    sqlite3_finalize(stmt);
    stmt = NULL;
    if (rc != SQLITE_DONE) {
        set_error(err, cap, sqlite3_errmsg(db));
        goto done;
    }
    if (db_exec(db, "BEGIN IMMEDIATE", err, cap) != 0)
        goto done;
    if (sqlite3_prepare_v2(db, "DELETE FROM packages WHERE name=?1",
                           -1, &stmt, NULL) != SQLITE_OK ||
        sqlite3_bind_text(stmt, 1, name, -1, SQLITE_TRANSIENT) != SQLITE_OK ||
        sqlite3_step(stmt) != SQLITE_DONE) {
        set_error(err, cap, sqlite3_errmsg(db));
        sqlite3_finalize(stmt);
        stmt = NULL;
        db_exec(db, "ROLLBACK", NULL, 0);
        goto done;
    }
    sqlite3_finalize(stmt);
    stmt = NULL;
    if (db_exec(db, "COMMIT", err, cap) != 0) {
        db_exec(db, "ROLLBACK", NULL, 0);
        goto done;
    }
    URAL_RMDIR(package_dir);
    printf("uninstalled %s\n", name);
    result = 0;
done:
    sqlite3_finalize(stmt);
    free(package_dir);
    return result;
}

typedef struct install_context {
    const char *state;
    const char *packages_dir;
    sqlite3 *db;
    char *stack[32];
    size_t depth;
    char *err;
    size_t err_size;
} install_context_t;

static int install_package(install_context_t *ctx, const char *name, int force)
{
    remote_package_t remote = {0};
    ural_manifest_t manifest;
    char *manifest_path = NULL, *archive_path = NULL, *stage = NULL;
    char *dest = NULL, *backup = NULL, *metadata = NULL;
    char *installed = NULL;
    char manifest_hash[17], archive_hash[17], dest_name[256], stage_name[256];
    installed_files_t files = {0};
    int exists, result = -1, moved_old = 0, installed_new = 0;
    int stage_created = 0;
    size_t i;
    sqlite3_stmt *stmt = NULL;
    for (i = 0; i < ctx->depth; ++i) {
        if (strcmp(ctx->stack[i], name) == 0) {
            snprintf(ctx->err, ctx->err_size, "dependency cycle detected at '%s'", name);
            return -1;
        }
    }
    if (ctx->depth >= sizeof(ctx->stack) / sizeof(ctx->stack[0])) {
        set_error(ctx->err, ctx->err_size, "dependency depth exceeds 32 packages");
        return -1;
    }
    ctx->stack[ctx->depth] = duplicate(name);
    if (ctx->stack[ctx->depth] == NULL) {
        set_error(ctx->err, ctx->err_size, "out of memory");
        return -1;
    }
    ++ctx->depth;
    exists = installed_version(ctx->db, name, &installed);
    if (exists < 0) {
        set_error(ctx->err, ctx->err_size, "cannot read installed package database");
        goto done;
    }
    if (exists && !force) {
        printf("%s %s is already installed\n", name, installed);
        result = 0;
        goto done;
    }
    if (find_remote_package(ctx->db, ctx->state, name, &remote,
                            ctx->err, ctx->err_size) != 0)
        goto done;
    if (exists && version_compare(remote.version, installed) < 0) {
        snprintf(ctx->err, ctx->err_size,
                 "refusing to downgrade %s from %s to %s",
                 name, installed, remote.version);
        goto done;
    }
    if (exists && version_compare(remote.version, installed) == 0) {
        printf("%s %s is already current\n", name, installed);
        result = 0;
        goto done;
    }
    if (snprintf(dest_name, sizeof dest_name, "%s", name) >= (int)sizeof dest_name ||
        snprintf(stage_name, sizeof stage_name, ".ural-stage-%s-%lu-%lu",
                 name, process_id(), (unsigned long)ctx->depth) >= (int)sizeof stage_name) {
        set_error(ctx->err, ctx->err_size, "package name is too long");
        goto done;
    }
    manifest_path = join_path(ctx->state, stage_name);
    if (manifest_path != NULL) {
        size_t n = strlen(manifest_path) + 10;
        char *tmp = (char *)realloc(manifest_path, n);
        if (tmp != NULL) {
            manifest_path = tmp;
            strcat(manifest_path, ".box.uno");
        }
    }
    archive_path = join_path(ctx->state, stage_name);
    if (archive_path != NULL) {
        size_t n = strlen(archive_path) + 7;
        char *tmp = (char *)realloc(archive_path, n);
        if (tmp != NULL) {
            archive_path = tmp;
            strcat(archive_path, ".ural");
        }
    }
    stage = join_path(ctx->state, stage_name);
    dest = join_path(ctx->packages_dir, dest_name);
    if (manifest_path == NULL || archive_path == NULL || stage == NULL || dest == NULL) {
        set_error(ctx->err, ctx->err_size, "out of memory");
        goto done;
    }
    if (!url_belongs_to_server(remote.metadata_url, remote.server) ||
        !url_belongs_to_server(remote.archive_url, remote.server) ||
        curl_download(remote.metadata_url, manifest_path, ctx->err, ctx->err_size) != 0 ||
        curl_download(remote.archive_url, archive_path, ctx->err, ctx->err_size) != 0)
        goto done;
    if (ural_hash_file(manifest_path, manifest_hash) != 0 ||
        (remote.metadata_hash[0] != '\0' &&
         strcmp(manifest_hash, remote.metadata_hash) != 0)) {
        set_error(ctx->err, ctx->err_size, "package metadata hash mismatch");
        goto done;
    }
    if (ural_hash_file(archive_path, archive_hash) != 0 ||
        strcmp(archive_hash, remote.archive_hash) != 0) {
        set_error(ctx->err, ctx->err_size, "package archive hash mismatch");
        goto done;
    }
    if (read_text_file(manifest_path, &metadata, URAL_MAX_MANIFEST) != 0 ||
        ural_manifest_parse(metadata, &manifest, ctx->err, ctx->err_size) != 0)
        goto done;
    if (strcmp(manifest.name, name) != 0 || strcmp(manifest.version, remote.version) != 0 ||
        (manifest.entry != NULL && manifest.entry[0] != '\0' &&
         !ural_archive_path_is_safe(manifest.entry))) {
        set_error(ctx->err, ctx->err_size, "package manifest identity or entry path is invalid");
        ural_manifest_free(&manifest);
        goto done;
    }
    for (i = 0; i < manifest.dependency_count; ++i) {
        if (install_package(ctx, manifest.dependencies[i], 0) != 0) {
            ural_manifest_free(&manifest);
            goto done;
        }
    }
    if (URAL_MKDIR(stage) != 0) {
        set_error(ctx->err, ctx->err_size, "cannot create staging directory");
        ural_manifest_free(&manifest);
        goto done;
    }
    stage_created = 1;
    files.root = stage;
    if (ural_archive_extract(archive_path, stage, installed_file_callback,
                             &files, ctx->err, ctx->err_size) != 0) {
        ural_manifest_free(&manifest);
        goto done;
    }
    if (manifest.entry != NULL && manifest.entry[0] != '\0') {
        char *entry = join_path(stage, manifest.entry);
        int valid = entry != NULL && file_is_regular(entry);
        free(entry);
        if (!valid) {
            set_error(ctx->err, ctx->err_size, "package entry does not exist in archive");
            ural_manifest_free(&manifest);
            goto done;
        }
    }
    {
        char *inside_manifest = join_path(stage, "box.uno");
        char inside_hash[17];
        int matches = inside_manifest != NULL &&
                      ural_hash_file(inside_manifest, inside_hash) == 0 &&
                      strcmp(inside_hash, manifest_hash) == 0;
        free(inside_manifest);
        if (!matches) {
            set_error(ctx->err, ctx->err_size, "archive box.uno does not match repository metadata");
            ural_manifest_free(&manifest);
            goto done;
        }
    }
    if (exists) {
        char backup_name[320];
        char *backups = join_path(ctx->state, "backups");
        if (backups == NULL || make_directories(backups) != 0) {
            free(backups);
            set_error(ctx->err, ctx->err_size, "cannot create rollback directory");
            ural_manifest_free(&manifest);
            goto done;
        }
        if (snprintf(backup_name, sizeof backup_name, "%s-previous-%lu",
                     name, process_id()) >= (int)sizeof backup_name) {
            free(backups);
            set_error(ctx->err, ctx->err_size, "package name is too long for rollback path");
            ural_manifest_free(&manifest);
            goto done;
        }
        backup = join_path(backups, backup_name);
        free(backups);
        if (backup == NULL || rename(dest, backup) != 0) {
            set_error(ctx->err, ctx->err_size, "cannot preserve previous package files");
            ural_manifest_free(&manifest);
            goto done;
        }
        moved_old = 1;
    } else {
        struct stat st;
        if (stat(dest, &st) == 0) {
            set_error(ctx->err, ctx->err_size,
                      "refusing to overwrite an unregistered package directory");
            ural_manifest_free(&manifest);
            goto done;
        }
    }
    if (rename(stage, dest) != 0) {
        set_error(ctx->err, ctx->err_size, "cannot move package into install directory");
        if (moved_old) rename(backup, dest);
        ural_manifest_free(&manifest);
        goto done;
    }
    installed_new = 1;
    if (db_exec(ctx->db, "BEGIN IMMEDIATE", ctx->err, ctx->err_size) != 0) {
        ural_manifest_free(&manifest);
        goto done;
    }
    if (sqlite3_prepare_v2(ctx->db,
        "INSERT INTO packages(name,version,description,source_url,install_path,manifest_hash) "
        "VALUES(?1,?2,?3,?4,?5,?6) ON CONFLICT(name) DO UPDATE SET "
        "version=excluded.version,description=excluded.description,source_url=excluded.source_url,"
        "install_path=excluded.install_path,manifest_hash=excluded.manifest_hash,"
        "updated_at=strftime('%s','now')", -1, &stmt, NULL) != SQLITE_OK ||
        sqlite3_bind_text(stmt, 1, name, -1, SQLITE_TRANSIENT) != SQLITE_OK ||
        sqlite3_bind_text(stmt, 2, manifest.version, -1, SQLITE_TRANSIENT) != SQLITE_OK ||
        sqlite3_bind_text(stmt, 3, manifest.description ? manifest.description : "",
                          -1, SQLITE_TRANSIENT) != SQLITE_OK ||
        sqlite3_bind_text(stmt, 4, remote.server, -1, SQLITE_TRANSIENT) != SQLITE_OK ||
        sqlite3_bind_text(stmt, 5, dest, -1, SQLITE_TRANSIENT) != SQLITE_OK ||
        sqlite3_bind_text(stmt, 6, remote.metadata_hash, -1, SQLITE_TRANSIENT) != SQLITE_OK ||
        sqlite3_step(stmt) != SQLITE_DONE) {
        set_error(ctx->err, ctx->err_size, sqlite3_errmsg(ctx->db));
        sqlite3_finalize(stmt);
        stmt = NULL;
        db_exec(ctx->db, "ROLLBACK", NULL, 0);
        ural_manifest_free(&manifest);
        goto done;
    }
    sqlite3_finalize(stmt);
    stmt = NULL;
    if (sqlite3_prepare_v2(ctx->db,
        "DELETE FROM package_files WHERE package_name=?1", -1, &stmt, NULL) != SQLITE_OK ||
        sqlite3_bind_text(stmt, 1, name, -1, SQLITE_TRANSIENT) != SQLITE_OK ||
        sqlite3_step(stmt) != SQLITE_DONE) {
        set_error(ctx->err, ctx->err_size, sqlite3_errmsg(ctx->db));
        sqlite3_finalize(stmt);
        stmt = NULL;
        db_exec(ctx->db, "ROLLBACK", NULL, 0);
        ural_manifest_free(&manifest);
        goto done;
    }
    sqlite3_finalize(stmt);
    stmt = NULL;
    for (i = 0; i < files.count; ++i) {
        if (sqlite3_prepare_v2(ctx->db,
            "INSERT INTO package_files(package_name,relative_path,file_hash) VALUES(?1,?2,?3)",
            -1, &stmt, NULL) != SQLITE_OK ||
            sqlite3_bind_text(stmt, 1, name, -1, SQLITE_TRANSIENT) != SQLITE_OK ||
            sqlite3_bind_text(stmt, 2, files.items[i].path, -1, SQLITE_TRANSIENT) != SQLITE_OK ||
            sqlite3_bind_text(stmt, 3, files.items[i].hash, -1, SQLITE_TRANSIENT) != SQLITE_OK ||
            sqlite3_step(stmt) != SQLITE_DONE) {
            set_error(ctx->err, ctx->err_size, sqlite3_errmsg(ctx->db));
            sqlite3_finalize(stmt);
            stmt = NULL;
            db_exec(ctx->db, "ROLLBACK", NULL, 0);
            ural_manifest_free(&manifest);
            goto done;
        }
        sqlite3_finalize(stmt);
        stmt = NULL;
    }
    if (db_exec(ctx->db, "COMMIT", ctx->err, ctx->err_size) != 0) {
        db_exec(ctx->db, "ROLLBACK", NULL, 0);
        ural_manifest_free(&manifest);
        goto done;
    }
    ural_manifest_free(&manifest);
    printf("installed %s %s\n", name, remote.version);
    result = 0;
done:
    sqlite3_finalize(stmt);
    if (result != 0 && installed_new)
        remove_tree(dest);
    if (moved_old && result != 0 && backup != NULL)
        rename(backup, dest);
    if (manifest_path != NULL) URAL_UNLINK(manifest_path);
    if (archive_path != NULL) URAL_UNLINK(archive_path);
    if (stage != NULL && stage_created)
        remove_tree(stage);
    for (i = 0; i < files.count; ++i)
        free(files.items[i].path);
    free(files.items);
    free(manifest_path);
    free(archive_path);
    free(stage);
    free(dest);
    free(backup);
    free(metadata);
    free(installed);
    remote_package_free(&remote);
    free(ctx->stack[--ctx->depth]);
    ctx->stack[ctx->depth] = NULL;
    return result;
}

static int command_install(sqlite3 *db, const char *state, const char *operation,
                           int argc, char **argv)
{
    char *packages_dir = join_path(state, "packages");
    install_context_t ctx;
    int i, result = 0, force = strcmp(operation, "update") == 0;
    size_t package_count = 0;
    if (packages_dir == NULL || make_directories(packages_dir) != 0) {
        free(packages_dir);
        fprintf(stderr, "ural: cannot create per-user package directory\n");
        return 1;
    }
    memset(&ctx, 0, sizeof ctx);
    ctx.state = state;
    ctx.packages_dir = packages_dir;
    ctx.db = db;
    ctx.err = (char *)calloc(1, 512);
    ctx.err_size = 512;
    if (ctx.err == NULL) {
        free(packages_dir);
        return 1;
    }
    for (i = 0; i < argc; ++i) {
        char *copy = duplicate(argv[i]);
        char *token;
        if (copy == NULL) { result = 1; break; }
        for (token = strtok(copy, " \t\r\n"); token != NULL;
             token = strtok(NULL, " \t\r\n")) {
            ++package_count;
            if (!ural_manifest_validate_component(token)) {
                fprintf(stderr, "ural: invalid package name '%s'\n", token);
                result = 1;
                break;
            }
            if (!strcmp(operation, "uninstall")) {
                int installed = installed_version(db, token, NULL);
                if (installed <= 0 ||
                    remove_recorded_package(db, packages_dir, token,
                                            ctx.err, ctx.err_size) != 0) {
                    if (installed == 0)
                        snprintf(ctx.err, ctx.err_size, "package '%s' is not installed", token);
                    result = 1;
                    break;
                }
            } else if (install_package(&ctx, token, force) != 0) {
                result = 1;
                break;
            }
        }
        free(copy);
        if (result != 0) break;
    }
    if (result == 0 && package_count == 0) {
        set_error(ctx.err, ctx.err_size, "at least one package name is required");
        result = 1;
    }
    if (result != 0)
        fprintf(stderr, "ural: %s\n", ctx.err[0] ? ctx.err : "operation failed");
    free(ctx.err);
    free(packages_dir);
    return result;
}

static int command_search(sqlite3 *local, const char *state, const char *query)
{
    sqlite3_stmt *servers = NULL;
    unsigned int index_number = 0;
    int rc, result = 0, printed = 0, indexes_read = 0;
    if (sqlite3_prepare_v2(local,
        "SELECT url FROM trusted_servers WHERE enabled=1 ORDER BY url",
        -1, &servers, NULL) != SQLITE_OK) {
        fprintf(stderr, "ural: %s\n", sqlite3_errmsg(local));
        return 1;
    }
    while ((rc = sqlite3_step(servers)) == SQLITE_ROW) {
        const unsigned char *server = sqlite3_column_text(servers, 0);
        char filename[96];
        char *path, *url;
        sqlite3 *remote = NULL;
        sqlite3_stmt *packages = NULL;
        if (server == NULL)
            continue;
        snprintf(filename, sizeof filename, ".ural-search-%lu-%u.sqlite",
                 process_id(), index_number++);
        path = join_path(state, filename);
        url = url_join((const char *)server, "ussr.boxes");
        if (path == NULL || url == NULL ||
            curl_download(url, path, NULL, 0) != 0 ||
            sqlite3_open_v2(path, &remote, SQLITE_OPEN_READONLY, NULL) != SQLITE_OK) {
            if (path != NULL)
                URAL_UNLINK(path);
            free(path);
            free(url);
            if (remote != NULL) sqlite3_close(remote);
            continue;
        }
        ++indexes_read;
        if (sqlite3_prepare_v2(remote,
            "SELECT DISTINCT package_name,version FROM repository_index "
            "WHERE package_name LIKE ?1 ORDER BY package_name,version",
            -1, &packages, NULL) == SQLITE_OK) {
            size_t n = strlen(query) + 3;
            char *pattern = (char *)malloc(n);
            if (pattern != NULL) {
                snprintf(pattern, n, "%%%s%%", query);
                if (sqlite3_bind_text(packages, 1, pattern, -1, SQLITE_TRANSIENT) == SQLITE_OK) {
                    while (sqlite3_step(packages) == SQLITE_ROW) {
                        printf("%s\t%s\t%s\n",
                               sqlite3_column_text(packages, 0),
                               sqlite3_column_text(packages, 1), server);
                        printed = 1;
                    }
                }
                free(pattern);
            }
        }
        sqlite3_finalize(packages);
        sqlite3_close(remote);
        URAL_UNLINK(path);
        free(path);
        free(url);
    }
    if (rc != SQLITE_DONE)
        result = 1;
    sqlite3_finalize(servers);
    if (indexes_read == 0) {
        fputs("ural: could not download an enabled trusted repository index\n", stderr);
        return 1;
    }
    if (!printed && result == 0)
        puts("no matching packages");
    return result;
}

static int list_callback(void *context, int columns, const char *const *names,
                         const char *const *values)
{
    int i;
    FILE *out = (FILE *)context;
    for (i = 0; i < columns; ++i)
        fprintf(out, "%s%s=%s", i ? "\t" : "", names[i], values[i]);
    fputc('\n', out);
    return 0;
}

static void usage(FILE *out)
{
    fputs("Usage:\n"
          "  ural init\n"
          "  ural db-path\n"
          "  ural install PACKAGE...\n"
          "  ural uninstall PACKAGE...\n"
          "  ural update PACKAGE...\n"
          "  ural list\n"
          "  ural search QUERY\n"
          "  ural server add|remove URL\n"
          "  ural server list\n"
          "  ural pack TARGET-DIR PUBLIC-HTTPS-URL\n"
          "  ural upload TARGET-DIR FTP-URL LOGIN PASSWORD\n", out);
}

int main(int argc, char **argv)
{
    char *state = default_state_dir();
    char *db_path = NULL;
    char err[512] = {0};
    ural_db_t db = {0};
    int result = 1;
    if (argc == 2 && (!strcmp(argv[1], "--help") || !strcmp(argv[1], "-h"))) {
        usage(stdout);
        free(state);
        return 0;
    }
    if (argc == 2 && !strcmp(argv[1], "--version")) {
        puts("ural 0.1.0");
        free(state);
        return 0;
    }
    if (argc >= 2 && !strcmp(argv[1], "pack") && argc == 4) {
        if (make_directories(argv[2]) != 0) {
            fprintf(stderr, "ural: cannot access package repository directory\n");
            free(state);
            return 1;
        }
        result = command_pack(argv[2], argv[3]);
        free(state);
        return result;
    }
    if (argc >= 2 && !strcmp(argv[1], "upload") && argc == 6) {
        result = command_upload(argv[2], argv[3], argv[4], argv[5]);
        free(state);
        return result;
    }
    if (state == NULL) {
        fprintf(stderr, "ural: HOME/USERPROFILE is not set\n");
        return 1;
    }
    if (make_directories(state) != 0) {
        fprintf(stderr, "ural: cannot create per-user state directory\n");
        free(state);
        return 1;
    }
    db_path = join_path(state, "ussr.boxes");
    if (db_path == NULL) {
        fprintf(stderr, "ural: out of memory\n");
        free(state);
        return 1;
    }
    if (seed_user_database(db_path, state, argv[0], err, sizeof err) != 0) {
        fprintf(stderr, "ural: %s\n", err);
        free(db_path);
        free(state);
        return 1;
    }
    if (ural_db_open(&db, db_path, err, sizeof err) != 0) {
        fprintf(stderr, "ural: %s\n", err);
        free(state);
        free(db_path);
        return 1;
    }
    if (argc == 2 && !strcmp(argv[1], "db-path")) {
        puts(db_path);
        ural_db_close(&db);
        free(db_path);
        free(state);
        return 0;
    }
    if (OPENSSL_init_ssl(0, NULL) != 1 ||
        curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK) {
        fprintf(stderr, "ural: cannot initialize OpenSSL/cURL\n");
        ural_db_close(&db);
        free(db_path);
        free(state);
        return 1;
    }
    if (argc == 2 && !strcmp(argv[1], "init")) {
        result = 0;
    } else if (argc == 2 && !strcmp(argv[1], "list")) {
        result = ural_db_list_packages(&db, list_callback, stdout, err, sizeof err) == 0 ? 0 : 1;
    } else if (argc == 3 && !strcmp(argv[1], "server") && !strcmp(argv[2], "list")) {
        result = ural_db_list_trusted_servers(&db, list_callback, stdout, err, sizeof err) == 0 ? 0 : 1;
    } else if (argc == 4 && !strcmp(argv[1], "server") && !strcmp(argv[2], "add")) {
        result = ural_db_add_trusted_server(&db, argv[3], err, sizeof err) == 0 ? 0 : 1;
        if (result == 0)
            printf("trusted server saved in %s\n", db_path);
    } else if (argc == 4 && !strcmp(argv[1], "server") && !strcmp(argv[2], "remove")) {
        result = ural_db_remove_trusted_server(&db, argv[3], err, sizeof err) == 0 ? 0 : 1;
    } else if (argc >= 3 && (!strcmp(argv[1], "install") ||
               !strcmp(argv[1], "uninstall") || !strcmp(argv[1], "update"))) {
        result = command_install(db.handle, state, argv[1], argc - 2, argv + 2);
    } else if (argc == 3 && !strcmp(argv[1], "search")) {
        result = command_search(db.handle, state, argv[2]);
    } else {
        usage(stderr);
        result = 2;
    }
    if (result != 0 && err[0] != '\0')
        fprintf(stderr, "ural: %s\n", err);
    curl_global_cleanup();
    ural_db_close(&db);
    free(db_path);
    free(state);
    return result;
}
