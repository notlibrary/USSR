/*
 * moscow_native.c - a Linux program for Moscow's system calls, built static
 * PIE against musl (src/moscow/build.sh puts it in /bin/moscow_native), for
 * qemu_test.py: moscow_native [what]. Without what it goes through memory,
 * time, files and directories and ends with status 11; with "read" it also
 * reads the console, "busy" never ends, "bad" writes the kernel's memory and
 * "grow" wants more and more memory.
 */
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

int main(int argc, char **argv)
{
    const char *what = argc > 1 ? argv[1] : "";
    char line[128], cwd[64];
    struct timespec ts;
    struct stat st;
    struct dirent *e;
    DIR *dir;
    FILE *f;
    char *p;
    void *m;
    double d = 2.5;
    int i, found = 0;

    printf("native: %d args, argv0 %s, HOME=%s\n", argc, argv[0], getenv("HOME"));
    p = malloc(100000);
    memset(p, 7, 100000);
    printf("native: malloc %d\n", p[99999]);
    free(p);
    m = mmap(0, 1 << 22, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    ((char *)m)[(1 << 22) - 1] = 9;
    printf("native: mmap %d\n", ((char *)m)[(1 << 22) - 1]);
    munmap(m, 1 << 22);
    for (i = 0; i < 3; i++)
        d *= 2;
    printf("native: real %g\n", d);
    clock_gettime(CLOCK_REALTIME, &ts);
    printf("native: clock %s\n", ts.tv_sec > 1000000000 ? "ok" : "bad");

    mkdir("/home", 0755);
    f = fopen("/home/native.txt", "w");
    if (!f) {
        perror("fopen");
        return 1;
    }
    fprintf(f, "written by a Linux program\n");
    fclose(f);
    f = fopen("/home/native.txt", "r");
    fgets(line, sizeof line, f);
    fclose(f);
    printf("native: read back: %s", line);
    stat("/home/native.txt", &st);
    printf("native: stat size %ld, dir %d\n", (long)st.st_size, S_ISDIR(st.st_mode));
    dir = opendir("/etc");
    while ((e = readdir(dir)))
        if (!strcmp(e->d_name, "motd"))
            found = 1;
    closedir(dir);
    printf("native: /etc has motd: %d\n", found);
    errno = 0;
    open("/no/such", O_RDONLY);
    printf("native: open of nothing: errno %d\n", errno);
    mkdir("/home/nd", 0755);
    chdir("/home/nd");
    printf("native: cwd %s\n", getcwd(cwd, sizeof cwd));
    chdir("/");
    rmdir("/home/nd");
    fflush(stdout);

    if (!strcmp(what, "read")) {
        p = fgets(line, sizeof line, stdin);
        printf("native: stdin: %s", p ? line : "(the end)\n");
    }
    if (!strcmp(what, "busy"))
        for (;;)
            ;
    if (!strcmp(what, "bad"))
        *(volatile int *)0x1000 = 1;
    if (!strcmp(what, "grow"))
        for (i = 0; i < 16384; i++)
            memset(malloc(4096), 1, 4096);
    return 11;
}
