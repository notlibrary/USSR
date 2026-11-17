/* ural_cmds.inc - Ural refine pass 1 commands. Included by ural.c (it
 * uses ural.c's static helpers: join_path, read_text_file, remove_tree,
 * make_directories, installed_version, file_is_regular).
 *
 * Without the database: new, check, version, hash, compare, satisfies,
 * completion. With it: run, verify, doctor, info, files, tree, why, backups,
 * clean, list --json. */

#include "ural_more.h"

#ifdef _WIN32
#include <process.h>
#else
#include <sys/types.h>
#include <sys/wait.h>
#endif

/* ------------------------------------------------------------ helpers */

static int write_text_file(const char *path, const char *text)
{
    FILE *f = fopen(path, "wb");
    size_t n = strlen(text);
    int ok;
    if (f == NULL) return -1;
    ok = fwrite(text, 1, n, f) == n;
    if (fclose(f) != 0) ok = 0;
    return ok ? 0 : -1;
}

static int path_exists(const char *path)
{
    struct stat st;
    return stat(path, &st) == 0;
}

static long long file_size_of(const char *path)
{
    struct stat st;
    if (stat(path, &st) != 0) return -1;
    return (long long)st.st_size;
}

static void json_string(FILE *out, const char *s)
{
    fputc('"', out);
    for (; s && *s; ++s) {
        unsigned char c = (unsigned char)*s;
        if (c == '"' || c == '\\') { fputc('\\', out); fputc(c, out); }
        else if (c == '\n') fputs("\\n", out);
        else if (c < 32) fprintf(out, "\\u%04x", c);
        else fputc(c, out);
    }
    fputc('"', out);
}

typedef int (*walk_fn)(void *ctx, const char *relative, int is_dir);

/* every file and directory under root, relative paths with '/' */
static int walk_tree(const char *root, const char *relative, walk_fn fn, void *ctx)
{
    char *dir = relative[0] ? join_path(root, relative) : duplicate(root);
    int rc = 0;
    if (dir == NULL) return -1;
#ifdef _WIN32
    {
        char *pattern = join_path(dir, "*");
        WIN32_FIND_DATAA data;
        HANDLE find;
        free(dir);
        if (pattern == NULL) return -1;
        find = FindFirstFileA(pattern, &data);
        free(pattern);
        if (find == INVALID_HANDLE_VALUE) return 0;
        do {
            char child[1024];
            int is_dir;
            if (!strcmp(data.cFileName, ".") || !strcmp(data.cFileName, "..")) continue;
            if (snprintf(child, sizeof child, "%s%s%s", relative, relative[0] ? "/" : "",
                         data.cFileName) >= (int)sizeof child) { rc = -1; break; }
            is_dir = (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
            rc = fn(ctx, child, is_dir);
            if (rc == 0 && is_dir) rc = walk_tree(root, child, fn, ctx);
        } while (rc == 0 && FindNextFileA(find, &data));
        FindClose(find);
    }
#else
    {
        DIR *d = opendir(dir);
        struct dirent *item;
        if (d == NULL) { free(dir); return 0; }
        while (rc == 0 && (item = readdir(d)) != NULL) {
            char child[1024];
            char *full;
            struct stat st;
            int is_dir;
            if (!strcmp(item->d_name, ".") || !strcmp(item->d_name, "..")) continue;
            if (snprintf(child, sizeof child, "%s%s%s", relative, relative[0] ? "/" : "",
                         item->d_name) >= (int)sizeof child) { rc = -1; break; }
            full = join_path(dir, item->d_name);
            is_dir = full != NULL && lstat(full, &st) == 0 && S_ISDIR(st.st_mode);
            free(full);
            rc = fn(ctx, child, is_dir);
            if (rc == 0 && is_dir) rc = walk_tree(root, child, fn, ctx);
        }
        closedir(d);
        free(dir);
    }
#endif
    return rc;
}

static int load_box(const char *dir, char **text, ural_manifest_t *m, char *err, size_t cap)
{
    char *path = join_path(dir, "box.uno");
    int rc;
    memset(m, 0, sizeof *m);
    *text = NULL;
    if (path == NULL) return -1;
    rc = read_text_file(path, text, URAL_MAX_MANIFEST);
    free(path);
    if (rc != 0) {
        set_error(err, cap, "cannot read box.uno");
        return -1;
    }
    if (ural_manifest_parse(*text, m, err, cap) != 0) {
        free(*text);
        *text = NULL;
        return -1;
    }
    return 0;
}

/* ------------------------------------------------------------ new */

static int command_new(const char *name, const char *dir_arg)
{
    char dir[1024], path[1200];
    struct { const char *file, *text; } files[6];
    char box[1024], readme[512], changelog[256], api[512], hello[256], plan[512];
    size_t i;
    if (!ural_manifest_validate_component(name)) {
        fprintf(stderr, "ural: '%s' is not a package name (a-z, 0-9, '-', '_', '.')\n", name);
        return 2;
    }
    snprintf(dir, sizeof dir, "%s", dir_arg ? dir_arg : name);
    if (path_exists(dir)) {
        snprintf(path, sizeof path, "%s/box.uno", dir);
        if (path_exists(path)) {
            fprintf(stderr, "ural: %s already holds a box.uno\n", dir);
            return 1;
        }
    }
    snprintf(path, sizeof path, "%s/docs", dir);
    if (make_directories(path) != 0) { fprintf(stderr, "ural: cannot create %s\n", path); return 1; }
    snprintf(path, sizeof path, "%s/examples", dir);
    if (make_directories(path) != 0) { fprintf(stderr, "ural: cannot create %s\n", path); return 1; }
    snprintf(path, sizeof path, "%s/tests", dir);
    if (make_directories(path) != 0) { fprintf(stderr, "ural: cannot create %s\n", path); return 1; }
    snprintf(box, sizeof box,
             "'Package{name=`%s`,version=`0.1.0`,description=`%s: a USSR package`,"
             "entry=`%s.su`,dependencies=[`library`],files=[`%s.su`,`README.md`,"
             "`CHANGELOG.md`,`docs/API.md`,`examples/hello.su`,`tests/TESTPLAN.md`],"
             "license=`UNSPECIFIED`,minimum_ussr=`0.1.0`,source=``}'\n",
             name, name, name, name);
    snprintf(readme, sizeof readme, "# %s\n\nA USSR package.\n\n```bash\nussr %s.su\n```\n", name, name);
    snprintf(changelog, sizeof changelog, "# Changelog\n\n## 0.1.0\n- First version.\n");
    snprintf(api, sizeof api, "# %s API\n\n`$include \"%s.su\"` gives `%s_main(code): args`.\n", name, name, name);
    snprintf(hello, sizeof hello, "$include \"../%s.su\"\n\nprint(_): \"hello from %s\"\n", name, name);
    snprintf(plan, sizeof plan, "# %s test plan\n\n- `ussr %s.su` runs and exits 0.\n", name, name);
    files[0].file = "box.uno"; files[0].text = box;
    files[1].file = "README.md"; files[1].text = readme;
    files[2].file = "CHANGELOG.md"; files[2].text = changelog;
    files[3].file = "docs/API.md"; files[3].text = api;
    files[4].file = "examples/hello.su"; files[4].text = hello;
    files[5].file = "tests/TESTPLAN.md"; files[5].text = plan;
    for (i = 0; i < 6; ++i) {
        snprintf(path, sizeof path, "%s/%s", dir, files[i].file);
        if (write_text_file(path, files[i].text) != 0) {
            fprintf(stderr, "ural: cannot write %s\n", path);
            return 1;
        }
    }
    {
        char entry[1200], main_su[512];
        snprintf(entry, sizeof entry, "%s/%s.su", dir, name);
        snprintf(main_su, sizeof main_su,
                 "# %s\n$once\n\n%s_main(r): args [\n    print(_): \"%s\"\n    set(r): 0\n]\n\n"
                 "$ifdef __MAIN__\ninit(ret): arg_count arg_values [\n    vec(args): \"\"\n"
                 "    %s_main(ret): args\n]\n$endif\n", name, name, name, name);
        if (write_text_file(entry, main_su) != 0) {
            fprintf(stderr, "ural: cannot write %s\n", entry);
            return 1;
        }
    }
    printf("created package %s in %s (7 files)\n", name, dir);
    return 0;
}

/* ------------------------------------------------------------ check */

typedef struct {
    char **listed;
    size_t listed_count;
    int strays;
    int quiet;
} stray_ctx_t;

static int stray_cb(void *context, const char *relative, int is_dir)
{
    stray_ctx_t *ctx = (stray_ctx_t *)context;
    size_t i, n = strlen(relative);
    if (is_dir) return 0;
    if (!strcmp(relative, "box.uno") || !strcmp(relative, "box.uno.example") ||
        !strcmp(relative, "REFINE.md"))
        return 0;
    if (n > 5 && !strcmp(relative + n - 5, ".ural")) return 0;
    for (i = 0; i < ctx->listed_count; ++i) {
        size_t ll = strlen(ctx->listed[i]);
        if (!strcmp(ctx->listed[i], relative)) return 0;
        /* a listed directory covers what is in it */
        if (ll < n && !strncmp(ctx->listed[i], relative, ll) && relative[ll] == '/') return 0;
    }
    if (ctx->strays < 10 && !ctx->quiet)
        printf("note: %s is not in files\n", relative);
    ++ctx->strays;
    return 0;
}

static int command_check(const char *dir)
{
    char err[256] = {0};
    char *text = NULL;
    ural_manifest_t m;
    char **files = NULL;
    size_t count = 0, i;
    int problems = 0, warnings = 0, entry_listed = 0;
    if (load_box(dir, &text, &m, err, sizeof err) != 0) {
        fprintf(stderr, "ural: %s: %s\n", dir, err[0] ? err : "bad box.uno");
        return 1;
    }
    if (!ural_version_valid(m.version)) {
        printf("problem: version '%s' is not N.N.N[-suffix]\n", m.version);
        ++problems;
    }
    if (m.license == NULL || !ural_spdx_known(m.license)) {
        printf("problem: license '%s' is not an SPDX id this ural knows\n",
               m.license ? m.license : "");
        ++problems;
    } else if (!strcmp(m.license, "UNSPECIFIED")) {
        printf("warning: license is UNSPECIFIED\n");
        ++warnings;
    }
    if (m.minimum_ussr != NULL && m.minimum_ussr[0] && !ural_version_valid(m.minimum_ussr)) {
        printf("problem: minimum_ussr '%s' is not a version\n", m.minimum_ussr);
        ++problems;
    }
    for (i = 0; i < m.dependency_count; ++i) {
        char name[128];
        const char *c;
        if (ural_dependency_split(m.dependencies[i], name, sizeof name, &c) != 0 ||
            !ural_constraint_valid(c)) {
            printf("problem: dependency '%s' is not NAME[CONSTRAINT]\n", m.dependencies[i]);
            ++problems;
        } else if (!strcmp(name, m.name)) {
            printf("problem: the package depends on itself\n");
            ++problems;
        }
    }
    if (ural_manifest_files(text, &files, &count) != 0) {
        printf("problem: the files list does not read\n");
        ++problems;
    }
    for (i = 0; i < count; ++i) {
        char *p = join_path(dir, files[i]);
        if (!ural_archive_path_is_safe(files[i])) {
            printf("problem: %s points outside the package\n", files[i]);
            ++problems;
        } else if (p == NULL || !path_exists(p)) {
            printf("problem: %s is listed but missing\n", files[i]);
            ++problems;
        }
        if (m.entry && !strcmp(files[i], m.entry)) entry_listed = 1;
        free(p);
    }
    if (count > 0 && m.entry && m.entry[0] && !entry_listed) {
        printf("problem: the entry %s is not in files\n", m.entry);
        ++problems;
    }
    if (count > 0) {
        stray_ctx_t ctx;
        ctx.listed = files;
        ctx.listed_count = count;
        ctx.strays = 0;
        ctx.quiet = 0;
        walk_tree(dir, "", stray_cb, &ctx);
        if (ctx.strays > 10) printf("note: and %d more files not in files\n", ctx.strays - 10);
        if (ctx.strays > 0) ++warnings;
    } else {
        printf("warning: files is empty: pack takes the whole directory\n");
        ++warnings;
    }
    printf("%s %s: %d problem(s), %d warning(s)\n", m.name, m.version, problems, warnings);
    ural_free_list(files, count);
    ural_manifest_free(&m);
    free(text);
    return problems ? 1 : 0;
}

/* ------------------------------------------------------------ version */

static int command_version_bump(const char *part, const char *dir)
{
    char err[256] = {0}, bumped[64];
    char *text = NULL, *path = NULL, *updated = NULL;
    ural_manifest_t m;
    int rc = 1;
    if (load_box(dir, &text, &m, err, sizeof err) != 0) {
        fprintf(stderr, "ural: %s: %s\n", dir, err[0] ? err : "bad box.uno");
        return 1;
    }
    if (ural_version_bump(m.version, part, bumped, sizeof bumped) != 0) {
        fprintf(stderr, "ural: cannot bump '%s' (%s): use patch, minor or major\n", m.version, part);
        goto done;
    }
    updated = ural_manifest_with_version(text, bumped);
    path = join_path(dir, "box.uno");
    if (updated == NULL || path == NULL || write_text_file(path, updated) != 0) {
        fprintf(stderr, "ural: cannot write box.uno\n");
        goto done;
    }
    free(path);
    /* a new CHANGELOG heading, under the title */
    path = join_path(dir, "CHANGELOG.md");
    if (path != NULL) {
        char *log = NULL;
        if (read_text_file(path, &log, URAL_MAX_MANIFEST) == 0) {
            char heading[96];
            const char *after = strstr(log, "\n## ");
            size_t at = after ? (size_t)(after - log) + 1 : strlen(log);
            char *out;
            snprintf(heading, sizeof heading, "## %s\n- \n\n", bumped);
            out = (char *)malloc(strlen(log) + strlen(heading) + 2);
            if (out != NULL) {
                memcpy(out, log, at);
                out[at] = '\0';
                if (after == NULL && at > 0 && log[at - 1] != '\n') strcat(out, "\n");
                strcat(out, heading);
                strcat(out, log + at);
                write_text_file(path, out);
                free(out);
            }
            free(log);
        }
    }
    printf("%s %s -> %s\n", m.name, m.version, bumped);
    rc = 0;
done:
    free(updated);
    free(path);
    ural_manifest_free(&m);
    free(text);
    return rc;
}

/* ------------------------------------------------------------ small ones */

static int command_hash(const char *path)
{
    char fnv[17], sha[65];
    if (ural_hash_file(path, fnv) != 0 || ural_sha256_file(path, sha) != 0) {
        fprintf(stderr, "ural: cannot read %s\n", path);
        return 1;
    }
    printf("fnv1a64 %s\nsha256  %s\n", fnv, sha);
    return 0;
}

static void completion(const char *shell)
{
    static const char *words =
        "init db-path install uninstall update list search server pack upload "
        "new check version hash compare satisfies completion verify doctor info "
        "files tree why backups clean run";
    if (shell != NULL && !strcmp(shell, "powershell")) {
        printf("Register-ArgumentCompleter -Native -CommandName ural -ScriptBlock {\n"
               "    param($word, $ast, $cursor)\n"
               "    '%s'.Split(' ') | Where-Object { $_ -like \"$word*\" } |\n"
               "        ForEach-Object { [System.Management.Automation.CompletionResult]::new($_) }\n"
               "}\n", words);
    } else {
        printf("_ural() {\n"
               "    local cur=${COMP_WORDS[COMP_CWORD]}\n"
               "    if [ \"$COMP_CWORD\" -eq 1 ]; then\n"
               "        COMPREPLY=($(compgen -W \"%s\" -- \"$cur\"))\n"
               "    else\n"
               "        COMPREPLY=($(compgen -W \"$(ural list 2>/dev/null | cut -f1)\" -- \"$cur\"))\n"
               "    fi\n"
               "}\n"
               "complete -F _ural ural\n", words);
    }
}

/* commands without the database: 1 when handled (*result set) */
static int ural_more_nodb(int argc, char **argv, int *result)
{
    if (argc < 2) return 0;
    if (!strcmp(argv[1], "new") && (argc == 3 || argc == 4)) {
        *result = command_new(argv[2], argc == 4 ? argv[3] : NULL);
        return 1;
    }
    if (!strcmp(argv[1], "check") && argc <= 3) {
        *result = command_check(argc == 3 ? argv[2] : ".");
        return 1;
    }
    if (!strcmp(argv[1], "version") && (argc == 3 || argc == 4)) {
        *result = command_version_bump(argv[2], argc == 4 ? argv[3] : ".");
        return 1;
    }
    if (!strcmp(argv[1], "hash") && argc == 3) {
        *result = command_hash(argv[2]);
        return 1;
    }
    if (!strcmp(argv[1], "compare") && argc == 4) {
        int c = ural_version_compare(argv[2], argv[3]);
        printf("%s %s %s\n", argv[2], c < 0 ? "<" : c > 0 ? ">" : "=", argv[3]);
        *result = 0;
        return 1;
    }
    if (!strcmp(argv[1], "satisfies") && argc == 4) {
        if (!ural_constraint_valid(argv[3])) {
            fprintf(stderr, "ural: bad constraint '%s'\n", argv[3]);
            *result = 2;
            return 1;
        }
        *result = ural_constraint_satisfied(argv[2], argv[3]) ? 0 : 1;
        printf("%s %s %s\n", argv[2], *result == 0 ? "satisfies" : "does not satisfy", argv[3]);
        return 1;
    }
    if (!strcmp(argv[1], "completion") && argc <= 3) {
        completion(argc == 3 ? argv[2] : "bash");
        *result = 0;
        return 1;
    }
    return 0;
}

/* ------------------------------------------------------------ installed */

static char *package_path(sqlite3 *db, const char *name, char **version)
{
    sqlite3_stmt *st = NULL;
    char *path = NULL;
    if (version) *version = NULL;
    if (sqlite3_prepare_v2(db, "SELECT install_path, version FROM packages WHERE name=?",
                           -1, &st, NULL) != SQLITE_OK)
        return NULL;
    sqlite3_bind_text(st, 1, name, -1, SQLITE_TRANSIENT);
    if (sqlite3_step(st) == SQLITE_ROW) {
        path = duplicate((const char *)sqlite3_column_text(st, 0));
        if (version) *version = duplicate((const char *)sqlite3_column_text(st, 1));
    }
    sqlite3_finalize(st);
    return path;
}

/* checks one package's files: problems found; prints them */
static int verify_package(sqlite3 *db, const char *name, int quiet, int *files_out)
{
    sqlite3_stmt *st = NULL;
    char *root = package_path(db, name, NULL);
    int problems = 0, files = 0;
    if (root == NULL) {
        printf("%s: not installed\n", name);
        return 1;
    }
    if (sqlite3_prepare_v2(db, "SELECT relative_path, file_hash FROM package_files "
                           "WHERE package_name=? ORDER BY relative_path", -1, &st, NULL) != SQLITE_OK) {
        free(root);
        return 1;
    }
    sqlite3_bind_text(st, 1, name, -1, SQLITE_TRANSIENT);
    while (sqlite3_step(st) == SQLITE_ROW) {
        const char *rel = (const char *)sqlite3_column_text(st, 0);
        const char *want = (const char *)sqlite3_column_text(st, 1);
        char *p = join_path(root, rel);
        char got[17];
        ++files;
        if (p == NULL || !file_is_regular(p)) {
            printf("%s: %s is missing\n", name, rel);
            ++problems;
        } else if (want && want[0] && (ural_hash_file(p, got) != 0 || strcmp(got, want) != 0)) {
            printf("%s: %s was changed\n", name, rel);
            ++problems;
        }
        free(p);
    }
    sqlite3_finalize(st);
    if (!quiet && problems == 0) printf("%s: %d files as installed\n", name, files);
    if (files_out) *files_out = files;
    free(root);
    return problems;
}

static int names_callback(void *context, int columns, char **values, char **names)
{
    (void)columns;
    (void)names;
    {
        char ***list = (char ***)((void **)context)[0];
        size_t *count = (size_t *)((void **)context)[1];
        char **grown = (char **)realloc(*list, (*count + 1) * sizeof(*grown));
        if (grown == NULL) return 1;
        *list = grown;
        grown[*count] = duplicate(values[0] ? values[0] : "");
        ++*count;
    }
    return 0;
}

static int installed_names(sqlite3 *db, char ***list, size_t *count)
{
    void *ctx[2];
    *list = NULL;
    *count = 0;
    ctx[0] = list;
    ctx[1] = count;
    return sqlite3_exec(db, "SELECT name FROM packages ORDER BY name", names_callback, ctx, NULL)
           == SQLITE_OK ? 0 : -1;
}

/* the installed package's manifest, or -1 */
static int installed_manifest(sqlite3 *db, const char *name, ural_manifest_t *m, char **text)
{
    char err[256];
    char *root = package_path(db, name, NULL);
    int rc;
    if (root == NULL) { memset(m, 0, sizeof *m); *text = NULL; return -1; }
    rc = load_box(root, text, m, err, sizeof err);
    free(root);
    return rc;
}

static int command_verify(sqlite3 *db, int argc, char **argv)
{
    int problems = 0, i;
    if (argc > 2) {
        for (i = 2; i < argc; ++i) problems += verify_package(db, argv[i], 0, NULL);
    } else {
        char **names;
        size_t n, k;
        if (installed_names(db, &names, &n) != 0) return 1;
        for (k = 0; k < n; ++k) problems += verify_package(db, names[k], 0, NULL);
        if (n == 0) puts("no packages installed");
        ural_free_list(names, n);
    }
    return problems ? 1 : 0;
}

static int command_doctor(sqlite3 *db, const char *state)
{
    sqlite3_stmt *st = NULL;
    char **names;
    size_t n, k, d;
    int problems = 0;
    if (sqlite3_prepare_v2(db, "PRAGMA integrity_check", -1, &st, NULL) == SQLITE_OK) {
        if (sqlite3_step(st) == SQLITE_ROW) {
            const char *v = (const char *)sqlite3_column_text(st, 0);
            if (v == NULL || strcmp(v, "ok") != 0) {
                printf("problem: the database: %s\n", v ? v : "unreadable");
                ++problems;
            } else {
                puts("database: ok");
            }
        }
        sqlite3_finalize(st);
    }
    if (installed_names(db, &names, &n) != 0) return 1;
    for (k = 0; k < n; ++k) {
        ural_manifest_t m;
        char *text = NULL;
        int files = 0;
        problems += verify_package(db, names[k], 1, &files);
        if (installed_manifest(db, names[k], &m, &text) == 0) {
            for (d = 0; d < m.dependency_count; ++d) {
                char dep[128], *have = NULL;
                const char *c;
                if (ural_dependency_split(m.dependencies[d], dep, sizeof dep, &c) != 0) continue;
                if (installed_version(db, dep, &have) <= 0) {
                    printf("problem: %s needs %s, which is not installed\n", names[k], dep);
                    ++problems;
                } else if (c[0] && !ural_constraint_satisfied(have, c)) {
                    printf("problem: %s needs %s%s, %s is installed\n", names[k], dep, c, have);
                    ++problems;
                }
                free(have);
            }
            ural_manifest_free(&m);
            free(text);
        }
    }
    printf("packages: %u checked\n", (unsigned)n);
    ural_free_list(names, n);
    {
        char *backups = join_path(state, "backups");
        int count = 0;
#ifdef _WIN32
        char *pattern = backups ? join_path(backups, "*") : NULL;
        WIN32_FIND_DATAA data;
        HANDLE find = pattern ? FindFirstFileA(pattern, &data) : INVALID_HANDLE_VALUE;
        if (find != INVALID_HANDLE_VALUE) {
            do if (strcmp(data.cFileName, ".") && strcmp(data.cFileName, "..")) ++count;
            while (FindNextFileA(find, &data));
            FindClose(find);
        }
        free(pattern);
#else
        DIR *dir = backups ? opendir(backups) : NULL;
        struct dirent *item;
        if (dir != NULL) {
            while ((item = readdir(dir)) != NULL)
                if (strcmp(item->d_name, ".") && strcmp(item->d_name, "..")) ++count;
            closedir(dir);
        }
#endif
        if (count > 0) printf("note: %d backup(s) from updates (ural clean removes them)\n", count);
        free(backups);
    }
    printf("%d problem(s)\n", problems);
    return problems ? 1 : 0;
}

static int command_info(sqlite3 *db, const char *name, int json)
{
    sqlite3_stmt *st = NULL;
    int files = 0;
    long long bytes = 0;
    char *root, *version = NULL;
    ural_manifest_t m;
    char *text = NULL;
    int have_manifest;
    root = package_path(db, name, &version);
    if (root == NULL) {
        fprintf(stderr, "ural: %s is not installed\n", name);
        return 1;
    }
    if (sqlite3_prepare_v2(db, "SELECT relative_path FROM package_files WHERE package_name=?",
                           -1, &st, NULL) == SQLITE_OK) {
        sqlite3_bind_text(st, 1, name, -1, SQLITE_TRANSIENT);
        while (sqlite3_step(st) == SQLITE_ROW) {
            char *p = join_path(root, (const char *)sqlite3_column_text(st, 0));
            long long s = p ? file_size_of(p) : -1;
            if (s > 0) bytes += s;
            ++files;
            free(p);
        }
        sqlite3_finalize(st);
    }
    have_manifest = installed_manifest(db, name, &m, &text) == 0;
    if (json) {
        size_t d;
        printf("{\"name\": ");
        json_string(stdout, name);
        printf(", \"version\": ");
        json_string(stdout, version);
        printf(", \"path\": ");
        json_string(stdout, root);
        printf(", \"files\": %d, \"bytes\": %lld", files, bytes);
        if (have_manifest) {
            printf(", \"description\": ");
            json_string(stdout, m.description);
            printf(", \"license\": ");
            json_string(stdout, m.license);
            printf(", \"dependencies\": [");
            for (d = 0; d < m.dependency_count; ++d) {
                if (d) printf(", ");
                json_string(stdout, m.dependencies[d]);
            }
            printf("]");
        }
        printf("}\n");
    } else {
        size_t d;
        printf("%s %s\n", name, version ? version : "");
        if (have_manifest) {
            printf("  %s\n", m.description ? m.description : "");
            printf("  license: %s\n", m.license ? m.license : "");
            printf("  entry: %s\n", m.entry ? m.entry : "");
            printf("  depends on:");
            for (d = 0; d < m.dependency_count; ++d) printf(" %s", m.dependencies[d]);
            printf("%s\n", m.dependency_count ? "" : " nothing");
        }
        printf("  %d files, %lld bytes, in %s\n", files, bytes, root);
        {
            char *readme = join_path(root, "README.md");
            char *rt = NULL;
            if (readme && read_text_file(readme, &rt, URAL_MAX_MANIFEST) == 0) {
                /* the first paragraph after the title */
                const char *p = strstr(rt, "\n\n");
                if (p) {
                    const char *e = strstr(p + 2, "\n\n");
                    size_t len = e ? (size_t)(e - p - 2) : strlen(p + 2);
                    if (len > 400) len = 400;
                    printf("\n%.*s\n", (int)len, p + 2);
                }
                free(rt);
            }
            free(readme);
        }
    }
    if (have_manifest) {
        ural_manifest_free(&m);
        free(text);
    }
    free(root);
    free(version);
    return 0;
}

static int command_files(sqlite3 *db, const char *name)
{
    sqlite3_stmt *st = NULL;
    int n = 0;
    if (sqlite3_prepare_v2(db, "SELECT relative_path FROM package_files WHERE package_name=? "
                           "ORDER BY relative_path", -1, &st, NULL) != SQLITE_OK)
        return 1;
    sqlite3_bind_text(st, 1, name, -1, SQLITE_TRANSIENT);
    while (sqlite3_step(st) == SQLITE_ROW) {
        puts((const char *)sqlite3_column_text(st, 0));
        ++n;
    }
    sqlite3_finalize(st);
    if (n == 0) {
        fprintf(stderr, "ural: %s has no recorded files (is it installed?)\n", name);
        return 1;
    }
    return 0;
}

static void tree_print(sqlite3 *db, const char *name, int depth, char seen[][128], int *nseen)
{
    ural_manifest_t m;
    char *text = NULL, *version = NULL;
    size_t d;
    int i;
    char *root = package_path(db, name, &version);
    printf("%*s%s %s\n", depth * 2, "", name, root ? (version ? version : "") : "(not installed)");
    free(root);
    free(version);
    for (i = 0; i < *nseen; ++i)
        if (!strcmp(seen[i], name)) {
            if (depth > 0) return;
        }
    if (*nseen < 64) snprintf(seen[(*nseen)++], 128, "%s", name);
    if (depth > 16 || installed_manifest(db, name, &m, &text) != 0) return;
    for (d = 0; d < m.dependency_count; ++d) {
        char dep[128];
        const char *c;
        if (ural_dependency_split(m.dependencies[d], dep, sizeof dep, &c) == 0)
            tree_print(db, dep, depth + 1, seen, nseen);
    }
    ural_manifest_free(&m);
    free(text);
}

static int command_why(sqlite3 *db, const char *target)
{
    char **names;
    size_t n, k, d;
    int found = 0;
    if (installed_names(db, &names, &n) != 0) return 1;
    for (k = 0; k < n; ++k) {
        ural_manifest_t m;
        char *text = NULL;
        if (installed_manifest(db, names[k], &m, &text) != 0) continue;
        for (d = 0; d < m.dependency_count; ++d) {
            char dep[128];
            const char *c;
            if (ural_dependency_split(m.dependencies[d], dep, sizeof dep, &c) == 0 &&
                !strcmp(dep, target)) {
                printf("%s needs %s%s\n", names[k], target, c);
                found = 1;
            }
        }
        ural_manifest_free(&m);
        free(text);
    }
    if (!found) printf("nothing installed needs %s\n", target);
    ural_free_list(names, n);
    return 0;
}

typedef struct { long long bytes; int files; } size_ctx_t;

static int size_cb(void *context, const char *relative, int is_dir)
{
    (void)context;
    (void)relative;
    (void)is_dir;
    return 0;
}

static int command_backups(const char *state, int remove)
{
    char *backups = join_path(state, "backups");
    int count = 0, rc = 0;
    if (backups == NULL) return 1;
#ifdef _WIN32
    {
        char *pattern = join_path(backups, "*");
        WIN32_FIND_DATAA data;
        HANDLE find = pattern ? FindFirstFileA(pattern, &data) : INVALID_HANDLE_VALUE;
        free(pattern);
        if (find != INVALID_HANDLE_VALUE) {
            do {
                char *p;
                if (!strcmp(data.cFileName, ".") || !strcmp(data.cFileName, "..")) continue;
                p = join_path(backups, data.cFileName);
                printf("%s %s\n", remove ? "removed" : "backup", data.cFileName);
                if (remove && p && remove_tree(p) != 0) rc = 1;
                free(p);
                ++count;
            } while (FindNextFileA(find, &data));
            FindClose(find);
        }
    }
#else
    {
        DIR *dir = opendir(backups);
        struct dirent *item;
        if (dir != NULL) {
            while ((item = readdir(dir)) != NULL) {
                char *p;
                if (!strcmp(item->d_name, ".") || !strcmp(item->d_name, "..")) continue;
                p = join_path(backups, item->d_name);
                printf("%s %s\n", remove ? "removed" : "backup", item->d_name);
                if (remove && p && remove_tree(p) != 0) rc = 1;
                free(p);
                ++count;
            }
            closedir(dir);
        }
    }
#endif
    (void)size_cb;
    if (count == 0) puts("no backups");
    free(backups);
    return rc;
}

static int command_list_json(sqlite3 *db)
{
    sqlite3_stmt *st = NULL;
    int first = 1;
    if (sqlite3_prepare_v2(db, "SELECT name, version, description, install_path FROM packages "
                           "ORDER BY name", -1, &st, NULL) != SQLITE_OK)
        return 1;
    printf("[");
    while (sqlite3_step(st) == SQLITE_ROW) {
        if (!first) printf(", ");
        first = 0;
        printf("{\"name\": ");
        json_string(stdout, (const char *)sqlite3_column_text(st, 0));
        printf(", \"version\": ");
        json_string(stdout, (const char *)sqlite3_column_text(st, 1));
        printf(", \"description\": ");
        json_string(stdout, (const char *)sqlite3_column_text(st, 2));
        printf(", \"path\": ");
        json_string(stdout, (const char *)sqlite3_column_text(st, 3));
        printf("}");
    }
    printf("]\n");
    sqlite3_finalize(st);
    return 0;
}

/* ural run PKG [ARGS]: ussr runs the package's entry */
static int command_run(sqlite3 *db, int argc, char **argv)
{
    ural_manifest_t m;
    char *text = NULL, *root, *entry;
    char **args;
    int i, rc;
    if (installed_manifest(db, argv[2], &m, &text) != 0) {
        fprintf(stderr, "ural: %s is not installed\n", argv[2]);
        return 1;
    }
    if (m.entry == NULL || m.entry[0] == '\0') {
        fprintf(stderr, "ural: %s has no entry to run\n", argv[2]);
        ural_manifest_free(&m);
        free(text);
        return 1;
    }
    root = package_path(db, argv[2], NULL);
    entry = root ? join_path(root, m.entry) : NULL;
    ural_manifest_free(&m);
    free(text);
    free(root);
    args = (char **)calloc((size_t)argc + 1, sizeof(*args));
    if (entry == NULL || args == NULL) { free(entry); free(args); return 1; }
    args[0] = "ussr";
    args[1] = entry;
    for (i = 3; i < argc; ++i) args[i - 1] = argv[i];
    args[argc - 1] = NULL;
    fflush(stdout);
#ifdef _WIN32
    {
        /* _spawnvp joins the words with spaces: quote each one */
        char **quoted = (char **)calloc((size_t)argc + 1, sizeof(*quoted));
        int k;
        rc = -1;
        if (quoted != NULL) {
            for (k = 0; args[k] != NULL; ++k) {
                size_t n = strlen(args[k]);
                quoted[k] = (char *)malloc(n + 3);
                if (quoted[k] == NULL) break;
                snprintf(quoted[k], n + 3, "\"%s\"", args[k]);
            }
            if (args[k] == NULL) rc = (int)_spawnvp(_P_WAIT, "ussr", (const char *const *)quoted);
            for (k = 0; quoted[k] != NULL; ++k) free(quoted[k]);
            free(quoted);
        }
    }
#else
    {
        pid_t pid = fork();
        int status = 0;
        if (pid == 0) {
            execvp("ussr", args);
            _exit(127);
        }
        rc = -1;
        if (pid > 0 && waitpid(pid, &status, 0) == pid && WIFEXITED(status))
            rc = WEXITSTATUS(status);
    }
#endif
    if (rc < 0) fprintf(stderr, "ural: cannot start ussr (is it on PATH?)\n");
    free(entry);
    free(args);
    return rc < 0 ? 1 : rc;
}

/* commands with the database: 1 when handled */
static int ural_more_db(int argc, char **argv, const char *state, sqlite3 *db, int *result)
{
    int json = argc >= 3 && !strcmp(argv[argc - 1], "--json");
    int n = json ? argc - 1 : argc;
    if (n < 2) return 0;
    if (!strcmp(argv[1], "run") && argc >= 3) { *result = command_run(db, argc, argv); return 1; }
    if (!strcmp(argv[1], "verify")) { *result = command_verify(db, n, argv); return 1; }
    if (!strcmp(argv[1], "doctor") && n == 2) { *result = command_doctor(db, state); return 1; }
    if (!strcmp(argv[1], "info") && n == 3) { *result = command_info(db, argv[2], json); return 1; }
    if (!strcmp(argv[1], "files") && n == 3) { *result = command_files(db, argv[2]); return 1; }
    if (!strcmp(argv[1], "tree") && n == 3) {
        char seen[64][128];
        int nseen = 0;
        tree_print(db, argv[2], 0, seen, &nseen);
        *result = 0;
        return 1;
    }
    if (!strcmp(argv[1], "why") && n == 3) { *result = command_why(db, argv[2]); return 1; }
    if (!strcmp(argv[1], "backups") && n == 2) { *result = command_backups(state, 0); return 1; }
    if (!strcmp(argv[1], "clean") && n == 2) { *result = command_backups(state, 1); return 1; }
    if (!strcmp(argv[1], "list") && json && n == 2) { *result = command_list_json(db); return 1; }
    return 0;
}
