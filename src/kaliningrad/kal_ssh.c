/* Moscow's native SSH backend. The protocol and cryptography live in libssh2. */
#include <arpa/inet.h>
#include <cpuid.h>
#include <errno.h>
#include <libssh2.h>
#include <openssl/crypto.h>
#include <openssl/rand.h>
#include <poll.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>

#define MOSCOW_CONSOLE_RAW 0x4b00

static bool seed_random(void)
{
    unsigned int eax, ebx, ecx, edx;
    unsigned long seed[8];
    if (!__get_cpuid(1, &eax, &ebx, &ecx, &edx) || !(ecx & bit_RDRND))
        return false;
    for (size_t i = 0; i < sizeof(seed) / sizeof(seed[0]); ++i) {
        unsigned char ready = 0;
        for (int attempt = 0; attempt < 10 && !ready; ++attempt)
            __asm__ volatile("rdrand %0; setc %1" : "=r"(seed[i]), "=qm"(ready));
        if (!ready) {
            OPENSSL_cleanse(seed, sizeof(seed));
            return false;
        }
    }
    RAND_seed(seed, sizeof(seed));
    OPENSSL_cleanse(seed, sizeof(seed));
    return RAND_status() == 1;
}

static int hex_digit(char c)
{
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    return -1;
}

static bool parse_fingerprint(const char *text, unsigned char digest[32])
{
    if (strlen(text) != 64)
        return false;
    for (size_t i = 0; i < 32; ++i) {
        int high = hex_digit(text[i * 2]);
        int low = hex_digit(text[i * 2 + 1]);
        if (high < 0 || low < 0)
            return false;
        digest[i] = (unsigned char)(16 * high + low);
    }
    return true;
}

static int write_all(int fd, const char *data, size_t size)
{
    while (size) {
        ssize_t n = write(fd, data, size);
        if (n <= 0)
            return -1;
        data += n;
        size -= (size_t)n;
    }
    return 0;
}

static int read_password(char *password, size_t capacity)
{
    size_t length = 0;
    size_t excess = 0;
    fputs("Password: ", stderr);
    fflush(stderr);
    for (;;) {
        char ch;
        if (read(STDIN_FILENO, &ch, 1) != 1 || ch == 3 || ch == 4)
            return -1;
        if (ch == '\r' || ch == '\n')
            break;
        if (ch == '\b' || ch == 127) {
            if (excess)
                --excess;
            else if (length)
                --length;
        } else if ((unsigned char)ch >= 32) {
            if (length == capacity)
                ++excess;
            else
                password[length++] = ch;
        }
    }
    fputc('\n', stderr);
    return excess ? -1 : (int)length;
}

static int interactive(LIBSSH2_SESSION *session, LIBSSH2_CHANNEL *channel, int fd)
{
    char output[4096];
    char input[1024];
    size_t offset = 0, length = 0;
    libssh2_session_set_blocking(session, 0);
    while (!libssh2_channel_eof(channel)) {
        bool progress = false;
        for (int stream = 0; stream <= 1; ++stream) {
            ssize_t n = libssh2_channel_read_ex(channel, stream, output, sizeof(output));
            if (n > 0) {
                if (write_all(stream ? STDERR_FILENO : STDOUT_FILENO, output, (size_t)n))
                    return 1;
                progress = true;
            } else if (n < 0 && n != LIBSSH2_ERROR_EAGAIN) {
                return 1;
            }
        }
        if (offset < length) {
            ssize_t n = libssh2_channel_write(channel, input + offset, length - offset);
            if (n > 0) {
                offset += (size_t)n;
                progress = true;
            } else if (n < 0 && n != LIBSSH2_ERROR_EAGAIN) {
                return 1;
            }
        }
        int directions = libssh2_session_block_directions(session);
        struct pollfd events[] = {
            {fd, POLLIN | ((directions & LIBSSH2_SESSION_BLOCK_OUTBOUND) ? POLLOUT : 0), 0},
            {STDIN_FILENO, offset == length ? POLLIN : 0, 0},
        };
        if (poll(events, 2, progress ? 0 : 50) < 0)
            return 1;
        if (events[1].revents & POLLIN) {
            ssize_t n = read(STDIN_FILENO, input, sizeof(input));
            if (n < 0 && errno == EAGAIN)
                continue;
            if (n <= 0 || memchr(input, 29, (size_t)n))
                return 0;
            offset = 0;
            length = (size_t)n;
        }
    }
    return libssh2_channel_get_exit_status(channel);
}

int main(int argc, char **argv)
{
    unsigned char expected[32];
    struct sockaddr_in addr = {.sin_family = AF_INET};
    char *end = NULL;
    long port = argc == 5 ? strtol(argv[3], &end, 10) : 0;
    if (argc != 5 || port < 1 || port > 65535 || *end || !argv[2][0] ||
        inet_pton(AF_INET, argv[1], &addr.sin_addr) != 1 ||
        !parse_fingerprint(argv[4], expected)) {
        fputs("Usage: kal-ssh IPV4 USER PORT SHA256_HEX\n", stderr);
        return 2;
    }
    addr.sin_port = htons((unsigned short)port);
    if (!seed_random()) {
        fputs("kal: Moscow SSH requires a working CPU RDRAND source\n", stderr);
        return 1;
    }
    if (libssh2_init(0)) {
        fputs("kal: could not initialize libssh2\n", stderr);
        return 1;
    }
    int result = 1;
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    LIBSSH2_SESSION *session = NULL;
    LIBSSH2_CHANNEL *channel = NULL;
    bool raw = false;
    char password[256] = {0};
    if (fd < 0 || connect(fd, (struct sockaddr *)&addr, sizeof(addr))) {
        perror("kal: connect");
        goto done;
    }
    session = libssh2_session_init();
    if (!session)
        goto done;
    libssh2_session_set_timeout(session, 15000);
    if (libssh2_session_handshake(session, fd)) {
        fputs("kal: SSH handshake failed\n", stderr);
        goto done;
    }
    const char *actual = libssh2_hostkey_hash(session, LIBSSH2_HOSTKEY_HASH_SHA256);
    if (!actual || CRYPTO_memcmp(expected, actual, sizeof(expected))) {
        fputs("kal: SSH host key fingerprint does not match\n", stderr);
        goto done;
    }
    if (ioctl(STDIN_FILENO, MOSCOW_CONSOLE_RAW, 1)) {
        fputs("kal: could not enable unechoed terminal input\n", stderr);
        goto done;
    }
    raw = true;
    int length = read_password(password, sizeof(password));
    if (length < 0 || libssh2_userauth_password_ex(session, argv[2],
            (unsigned int)strlen(argv[2]), password, (unsigned int)length, NULL)) {
        fputs("kal: password authentication failed\n", stderr);
        goto done;
    }
    OPENSSL_cleanse(password, sizeof(password));
    channel = libssh2_channel_open_session(session);
    if (!channel || libssh2_channel_request_pty(channel, "xterm") ||
        libssh2_channel_shell(channel)) {
        fputs("kal: could not start a remote terminal\n", stderr);
        goto done;
    }
    fputs("kal: press Ctrl-] to disconnect\n", stderr);
    result = interactive(session, channel, fd);
done:
    OPENSSL_cleanse(password, sizeof(password));
    if (raw)
        ioctl(STDIN_FILENO, MOSCOW_CONSOLE_RAW, 0);
    if (session)
        libssh2_session_set_blocking(session, 0);
    if (channel)
        libssh2_channel_free(channel);
    if (fd >= 0)
        close(fd);
    if (session)
        libssh2_session_free(session);
    libssh2_exit();
    return result;
}
