#include "help.h"

#include <stdio.h>
#include <string.h>

typedef struct
{
    const char *name;
    const char *group;
    const char *usage;
    const char *text;
} help_entry_t;

static const help_entry_t g_help[] = {
    /* values and variables */
    { "set", "values", "set(x): value", "store a value in x" },
    { "print", "values", "print(_): value", "print a value and a newline" },
    { "typeof", "values", "typeof(t): value", "type name of a value" },
    { "global", "values", "global(_): name ...",
      "make names global (top level: in every function)" },
    { "eval", "values", "eval(x): \"source\"", "run USSR code from a string" },
    { "fail", "values", "fail(_): \"message\"", "stop with an error message" },

    /* arithmetic */
    { "add", "math", "add(sum): a b", "a + b" },
    { "sub", "math", "sub(diff): a b", "a - b" },
    { "mul", "math", "mul(product): a b", "a * b" },
    { "div", "math", "div(quotient): a b", "a / b, always a real" },
    { "mod", "math", "mod(remainder): a b", "a % b for integers" },
    { "sin", "math", "sin(y): x", "sine, radians" },
    { "ln", "math", "ln(y): x", "natural logarithm" },
    { "random64", "math", "random64(r):", "random 64-bit integer" },
    { "seed_random64", "math", "seed_random64(_): seed",
      "seed the random generator" },
    { "time", "math", "time(now):", "current time" },

    /* control flow */
    { "if", "control", "if(_): {condition} [ ... ]",
      "run the block when the condition holds; elif/else may follow" },
    { "elif", "control", "elif(_): {condition} [ ... ]",
      "next test of an if chain" },
    { "else", "control", "else(_): [ ... ]", "last branch of an if chain" },
    { "while", "control", "while(_): {condition} [ ... ]",
      "repeat the block while the condition holds" },
    { "do", "control", "do(_): 1 [ ... ]  loop(_): {condition}",
      "run the block, then repeat while loop's condition holds" },
    { "loop", "control", "loop(_): {condition}", "the test after a do block" },
    { "break", "control", "break(_): \"\"", "leave the innermost loop" },
    { "continue", "control", "continue(_): \"\"",
      "next iteration of the innermost loop" },
    { "return", "control", "return(_): value", "return from a function" },
    { "try", "control", "try(failed): \"function\" arg ...",
      "call a function; an error in it ends the call, not the program: "
      "failed is 1 then (the error is still reported), else 0" },

    /* strings */
    { "concat", "strings", "concat(s): a b ...", "join strings" },
    { "len", "strings", "len(n): v", "length of a string or vector" },
    { "byte", "strings", "byte(b): s i", "byte at index i" },
    { "slice", "strings", "slice(t): s i n", "n bytes of s from index i" },
    { "chr", "strings", "chr(s): byte", "one-byte string for 1..255" },

    /* structs, vectors, UNO */
    { "struct", "data", "struct(Name): \"field:type\" ...", "declare a struct" },
    { "new", "data", "new(x): \"Name\" value ...", "make a struct instance" },
    { "getf", "data", "getf(v): obj field", "read a field" },
    { "setf", "data", "setf(_): obj field value", "write a field" },
    { "vec", "data", "vec(v): \"\"", "make a vector" },
    { "push", "data", "push(_): v item", "append to a vector" },
    { "pop", "data", "pop(item): v", "remove the last item" },
    { "at", "data", "at(item): v index", "item at index" },
    { "encode", "data", "encode(s): obj", "value to UNO text" },
    { "decode", "data", "decode(obj): s", "UNO text to value" },

    /* files */
    { "readfile", "files", "readfile(v): path", "whole file as a string" },
    { "writefile", "files", "writefile(v): path data", "replace a file" },
    { "file_exists", "files", "file_exists(v): path", "true if the path exists" },
    { "open_file", "files", "open_file(h): path mode", "open a file; -1 on error" },
    { "read_file", "files", "read_file(s): h [n]", "read from a handle" },
    { "read_line", "files", "read_line(s): h", "read one line" },
    { "write_file", "files", "write_file(n): h data", "write to a handle" },
    { "close_file", "files", "close_file(ok): h", "close a handle" },
    { "list_dir", "files", "list_dir(v): path", "directory entries" },
    { "is_dir", "files", "is_dir(v): path", "1 if path is a directory" },
    { "getcwd", "files", "getcwd(d):", "current directory" },
    { "cd", "files", "cd(_): path", "change directory" },
    { "make_dir", "files", "make_dir(r): path", "create a directory" },
    { "rename_path", "files", "rename_path(r): from to", "rename or move" },
    { "copy_file", "files", "copy_file(r): from to", "copy a file" },
    { "remove_file", "files", "remove_file(r): path", "delete a file" },
    { "remove_dir", "files", "remove_dir(r): path", "delete an empty directory" },

    /* bytes, hashes and binary sockets */
    { "text_bytes", "bytes", "text_bytes(v): s", "a string's bytes as a vector" },
    { "bytes_text", "bytes", "bytes_text(s): v [from [count]]", "bytes as a string; null if one is 0" },
    { "sha256", "bytes", "sha256(v): data", "SHA-256 digest (32 bytes) of a string or byte vector" },
    { "crc32", "bytes", "crc32(n): data [from [count]]", "CRC-32 (zlib, GPT, PNG) of a string or byte vector" },
    { "md5", "bytes", "md5(v): data", "MD5 digest (16 bytes); for old protocols only" },
    { "hmac_sha256", "bytes", "hmac_sha256(v): key data", "HMAC-SHA-256 (32 bytes)" },
    { "pbkdf2_sha256", "bytes", "pbkdf2_sha256(v): password salt iterations [length]", "PBKDF2-HMAC-SHA-256 key (default 32 bytes)" },
    { "socket_send_bytes", "bytes", "socket_send_bytes(n): socket v", "send a byte vector; bytes sent or -1" },
    { "socket_recv_bytes", "bytes", "socket_recv_bytes(v): socket max", "receive up to max bytes; empty once closed, null on error" },
    { "tls_send_bytes", "bytes", "tls_send_bytes(n): socket v", "tls_send for a byte vector" },
    { "bread", "bytes", "bread(v): device lba [count [sector]]", "read sectors of a disk or image; null on error" },
    { "bwrite", "bytes", "bwrite(n): device lba bytes [sector]", "write whole sectors; bytes written or -1" },
    { "bsize", "bytes", "bsize(n): device", "size of a disk or image in bytes; -1 if unknown" },
    { "bdevices", "bytes", "bdevices(v):", "storage devices: [path bytes kind bus model sector] each" },
    { "tls_recv_bytes", "bytes", "tls_recv_bytes(v): socket max", "tls_recv as a byte vector; null: wait_readable" },

    /* UDP datagrams */
    { "socket_udp", "network", "socket_udp(h): [port [ip]]",
      "a UDP socket bound to port (0: any) with broadcast on; -1 if the "
      "port is taken" },
    { "socket_sendto", "network", "socket_sendto(n): socket host port data",
      "send one datagram (a string or byte vector) to host:port, "
      "255.255.255.255 broadcasts; bytes sent or -1" },
    { "socket_recvfrom", "network", "socket_recvfrom(v): socket [ms]",
      "one datagram as [text ip port], waiting up to ms; null if none "
      "came, -1 on error" },
    { "terminal_key_ready", "network", "terminal_key_ready(v): [ms]",
      "1 when terminal_getch has a key (or the end of input) within ms, "
      "else 0: poll the keyboard between datagrams" },

    /* environment and shell */
    { "getenv", "shell", "getenv(v): name", "environment variable, or null" },
    { "setenv", "shell", "setenv(r): name value", "set an environment variable" },
    { "unsetenv", "shell", "unsetenv(r): name", "remove an environment variable" },
    { "launch", "shell", "launch(ok): target [params [hidden]]",
      "start a program, or open a file, folder or URL with its system "
      "program, without waiting; 0 started, 1 failed. hidden: no console "
      "window (Windows)" },
    { "localtime", "shell", "localtime(v): [seconds]",
      "[year month day hour minute second weekday yearday] in local time" },
    { "alias", "shell", "alias(_): \"ll\" \"ls\" \"-l\"",
      "ll(_): x then runs ls(_): \"-l\" x; \"\" removes it, one argument "
      "gives its words, none lists them. Expanded in lines typed at "
      "the REPL only, never in scripts; put them in kiev.su" },
    { "keybind", "shell", "keybind(_): \"Alt+F1\" \"help(_):\"",
      "bind a REPL key to a line; \"\" unbinds; one argument reads a "
      "binding, none lists them. Keys: Ctrl/Alt(Mod)/Shift + a..z, 0..9, "
      "F1..F12" },
    { "keybind_match", "shell", "keybind_match(line): bytes",
      "the line bound to the key that sends these bytes, or null "
      "(for REPLs written in USSR)" },
    { "prompt", "shell", "prompt(answer): \"question\" ...",
      "ask an AI model; with prompt(_) the answer is printed. "
      "Configure with USSR_AI_* variables: help(_): \"ai\"" },
    { "help", "shell", "help(_): [\"name\"]", "this help" },

    /* processes */
    { "async", "processes", "async(p): \"name\" arg ...",
      "start a definition as a process" },
    { "await", "processes", "await(r): pid", "wait for a process's result" },
    { "sleep", "processes", "sleep(_): milliseconds", "pause this process" },
    { "process", "processes", "process(_): name", "start a definition as a process" },
    { "load", "processes", "load(p): \"script.su\"", "run a script as a process" },
    { NULL, NULL, NULL, NULL }
};

static const char *const g_groups[] = {
    "values", "math", "control", "strings", "data",
    "files", "bytes", "network", "shell", "processes", NULL
};

/* text word-wrapped to 78 columns, every line indented by two */
static void help_wrap(const char *text)
{
    int column = 0;

    while (*text != '\0')
    {
        size_t word = strcspn(text, " ");

        if (column > 2 && column + 1 + (int)word > 78)
        {
            printf("\n");
            column = 0;
        }
        column += printf(column == 0 ? "  %.*s" : " %.*s", (int)word, text);
        text += word;
        while (*text == ' ')
            ++text;
    }
    printf("\n");
}

static void help_overview(void)
{
    size_t g;

    printf("USSR: every line is a command\n"
           "    name(result): argument argument ...\n"
           "  _ discards the result; {a + b} computes; [ ... ] is a block;\n"
           "  any other name runs a program: ls(_): \"-l\"\n\n");
    for (g = 0; g_groups[g] != NULL; ++g)
    {
        size_t i;
        int column = printf("  %-10s", g_groups[g]);

        for (i = 0; g_help[i].name != NULL; ++i)
        {
            size_t width;

            if (strcmp(g_help[i].group, g_groups[g]) != 0)
                continue;
            width = strlen(g_help[i].name) + 1;
            if (column + (int)width > 78)
            {
                printf("\n%12s", "");
                column = 12;
            }
            column += printf(" %s", g_help[i].name);
        }
        printf("\n");
    }
    printf("\nhelp(_): \"name\" for one command; topics: \"syntax\", "
           "\"keys\", \"ai\"\n"
           "The manual: docs/USSR_USER_MANUAL.md\n");
}

static void help_keys(void)
{
    printf("REPL keys\n"
           "  Tab           complete commands, variables and paths\n"
           "  Up / Down     history, kept in ~/.ussr_history\n"
           "  Ctrl-D        exit on an empty line (Ctrl-Z on Windows)\n"
#ifndef _WIN32
           "  Ctrl-R        search history\n"
           "  Ctrl-A / E    start / end of line\n"
           "  Ctrl-K / U    cut to end / start of line\n"
           "  Ctrl-W        cut the word before the cursor\n"
#endif
           "\nYour own keys run a line when pressed:\n"
           "  keybind(_): \"Alt+F1\" \"help(_):\"\n"
           "  keybind(_): \"Ctrl+G\" \"ls(_):\"\n"
           "  keybind(_): \"Alt+F1\" \"\"        remove it\n"
           "  keybind(_):                     list them\n"
           "Mod means Alt: terminals never see the Super/Windows key.\n"
           "Put bindings in kiev.su to have them in every session.\n");
}

static void help_ai(void)
{
    printf("prompt(answer): \"To be or not to be?\"\n"
           "  asks a chat model over HTTPS (ryazan TLS).\n\n"
           "  USSR_AI_PROVIDER    anthropic (default) or openai\n"
           "                      (openai also means any OpenAI-compatible\n"
           "                      server: Ollama, LM Studio, OpenRouter ...)\n"
           "  USSR_AI_URL         endpoint; default is the provider's API\n"
           "  USSR_AI_MODEL       model; anthropic default claude-sonnet-5-5\n"
           "  USSR_AI_KEY         API key (or ANTHROPIC_API_KEY / OPENAI_API_KEY)\n"
           "  USSR_AI_SYSTEM      system prompt\n"
           "  USSR_AI_MAX_TOKENS  answer length limit\n"
           "  USSR_AI_TIMEOUT     seconds, default 120\n"
           "  USSR_AI_CURL        use this curl program instead of ryazan\n\n"
           "Local Ollama example:\n"
           "  setenv(_): \"USSR_AI_PROVIDER\" \"openai\"\n"
           "  setenv(_): \"USSR_AI_URL\" "
           "\"http://localhost:11434/v1/chat/completions\"\n"
           "  setenv(_): \"USSR_AI_MODEL\" \"llama3.2\"\n");
}

static void help_syntax(void)
{
    printf("name(result): arg arg ...   one command per line\n"
           "  \"text\"  42  -1.5  true  null    values\n"
           "  x                              a variable\n"
           "  {x * 2 > y && z != 0}          an expression\n"
           "  [ ... ]                        a block of commands\n"
           "  # comment\n"
           "  f(r): a b [ mul(r): a b ]      defines function f\n"
           "  name?  / name!                 read / write the shared hash\n"
           "  $include \"file.su\"  $define  preprocessor\n");
}

int ussr_help_print(const char *topic)
{
    size_t i;
    size_t length;
    int shown = 0;

    if (topic == NULL || topic[0] == '\0')
    {
        help_overview();
        return 0;
    }
    if (strcmp(topic, "keys") == 0)
    {
        help_keys();
        return 0;
    }
    if (strcmp(topic, "ai") == 0)
    {
        help_ai();
        return 0;
    }
    if (strcmp(topic, "syntax") == 0)
    {
        help_syntax();
        return 0;
    }

    for (i = 0; g_help[i].name != NULL; ++i)
    {
        if (strcmp(g_help[i].name, topic) == 0)
        {
            printf("%s\n", g_help[i].usage);
            help_wrap(g_help[i].text);
            if (strcmp(topic, "keybind") == 0)
                printf("  more: help(_): \"keys\"\n");
            return 0;
        }
    }

    length = strlen(topic);
    for (i = 0; g_help[i].name != NULL; ++i)
    {
        if (strncmp(g_help[i].name, topic, length) == 0)
        {
            printf("%s%s", shown ? " " : "did you mean: ", g_help[i].name);
            shown = 1;
        }
    }
    if (shown)
        printf("\n");
    else
        printf("no help for '%s'; it may be a program: try %s(_): \"--help\"\n",
               topic, topic);
    return -1;
}
