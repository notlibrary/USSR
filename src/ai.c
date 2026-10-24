/* POSIX access() under -std=c11. */
#if !defined(_WIN32) && !defined(_DEFAULT_SOURCE)
#define _DEFAULT_SOURCE
#endif

#include "ai.h"

#include "ussr_oop_builtins.h" /* ussr_env_get */

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef __wasi__
#include "process.h"
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <unistd.h>
#endif
#endif

#define AI_DEFAULT_ANTHROPIC_URL   "https://api.anthropic.com/v1/messages"
#define AI_DEFAULT_ANTHROPIC_MODEL "claude-sonnet-5-5"
#define AI_DEFAULT_OPENAI_URL      "https://api.openai.com/v1/chat/completions"

/* ------------------------------------------------------------- */
/* growable text buffer                                             */
/* ------------------------------------------------------------- */

typedef struct
{
    char *data;
    size_t length;
    size_t capacity;
    int failed;
} ai_buffer_t;

static void ai_add(ai_buffer_t *b, const char *text, size_t length)
{
    if (b->failed)
        return;
    if (b->length + length + 1 > b->capacity)
    {
        size_t capacity = b->capacity ? b->capacity : 256;
        char *grown;

        while (b->length + length + 1 > capacity)
            capacity *= 2;
        grown = realloc(b->data, capacity);
        if (grown == NULL)
        {
            b->failed = 1;
            return;
        }
        b->data = grown;
        b->capacity = capacity;
    }
    memcpy(b->data + b->length, text, length);
    b->length += length;
    b->data[b->length] = '\0';
}

static void ai_adds(ai_buffer_t *b, const char *text)
{
    ai_add(b, text, strlen(text));
}

/* text as a JSON string literal, quotes included */
static void ai_add_json_string(ai_buffer_t *b, const char *text)
{
    const unsigned char *p;

    ai_adds(b, "\"");
    for (p = (const unsigned char *)text; *p != '\0'; ++p)
    {
        char escaped[8];

        if (*p == '"' || *p == '\\')
        {
            escaped[0] = '\\';
            escaped[1] = (char)*p;
            ai_add(b, escaped, 2);
        }
        else if (*p == '\n')
            ai_adds(b, "\\n");
        else if (*p == '\r')
            ai_adds(b, "\\r");
        else if (*p == '\t')
            ai_adds(b, "\\t");
        else if (*p < 0x20)
        {
            snprintf(escaped, sizeof(escaped), "\\u%04x", *p);
            ai_adds(b, escaped);
        }
        else
            ai_add(b, (const char *)p, 1);
    }
    ai_adds(b, "\"");
}

/* text as a curl config quoted value: only \ and " need escaping */
static void ai_add_config_string(ai_buffer_t *b, const char *text)
{
    const char *p;

    ai_adds(b, "\"");
    for (p = text; *p != '\0'; ++p)
    {
        if (*p == '"' || *p == '\\')
            ai_add(b, "\\", 1);
        ai_add(b, p, 1);
    }
    ai_adds(b, "\"\n");
}

/* ------------------------------------------------------------- */
/* just enough JSON reading for one response                        */
/* ------------------------------------------------------------- */

static const char *json_ws(const char *p)
{
    while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')
        ++p;
    return p;
}

static const char *json_skip(const char *p);

static const char *json_skip_string(const char *p)
{
    if (*p != '"')
        return NULL;
    for (++p; *p != '\0'; ++p)
    {
        if (*p == '\\')
        {
            if (p[1] == '\0')
                return NULL;
            ++p;
        }
        else if (*p == '"')
            return p + 1;
    }
    return NULL;
}

/* Skip one value; returns the position after it, or NULL. */
static const char *json_skip(const char *p)
{
    p = json_ws(p);
    if (*p == '"')
        return json_skip_string(p);
    if (*p == '{' || *p == '[')
    {
        char close = *p == '{' ? '}' : ']';

        p = json_ws(p + 1);
        if (*p == close)
            return p + 1;
        for (;;)
        {
            if (close == '}')
            {
                p = json_skip_string(json_ws(p));
                if (p == NULL)
                    return NULL;
                p = json_ws(p);
                if (*p != ':')
                    return NULL;
                ++p;
            }
            p = json_skip(p);
            if (p == NULL)
                return NULL;
            p = json_ws(p);
            if (*p == ',')
            {
                ++p;
                continue;
            }
            if (*p == close)
                return p + 1;
            return NULL;
        }
    }
    if (*p == '-' || isalnum((unsigned char)*p))
    {
        while (*p == '-' || *p == '+' || *p == '.' ||
               isalnum((unsigned char)*p))
            ++p;
        return p;
    }
    return NULL;
}

/* Decode the JSON string at p into a malloc'd UTF-8 string. */
static char *json_string(const char *p)
{
    ai_buffer_t out = { NULL, 0, 0, 0 };

    p = json_ws(p);
    if (*p != '"')
        return NULL;
    ai_add(&out, "", 0);
    for (++p; *p != '"'; ++p)
    {
        char c = *p;

        if (c == '\0')
        {
            free(out.data);
            return NULL;
        }
        if (c != '\\')
        {
            ai_add(&out, p, 1);
            continue;
        }
        c = *++p;
        if (c == 'u')
        {
            unsigned long code = 0;
            char utf8[4];
            int i;
            int n;

            for (i = 1; i <= 4; ++i)
            {
                if (!isxdigit((unsigned char)p[i]))
                {
                    free(out.data);
                    return NULL;
                }
                code = code * 16 + (unsigned long)(isdigit((unsigned char)p[i])
                    ? p[i] - '0' : tolower((unsigned char)p[i]) - 'a' + 10);
            }
            p += 4;
            /* surrogate pair */
            if (code >= 0xD800 && code <= 0xDBFF &&
                p[1] == '\\' && p[2] == 'u')
            {
                char hex[5];
                unsigned long low;

                strncpy(hex, p + 3, 4); /* stops at a truncated end */
                hex[4] = '\0';
                low = strtoul(hex, NULL, 16);
                if (strspn(hex, "0123456789abcdefABCDEF") == 4 &&
                    low >= 0xDC00 && low <= 0xDFFF)
                {
                    code = 0x10000 + ((code - 0xD800) << 10) + (low - 0xDC00);
                    p += 6;
                }
            }
            if (code < 0x80)
            {
                utf8[0] = (char)code;
                n = 1;
            }
            else if (code < 0x800)
            {
                utf8[0] = (char)(0xC0 | (code >> 6));
                utf8[1] = (char)(0x80 | (code & 0x3F));
                n = 2;
            }
            else if (code < 0x10000)
            {
                utf8[0] = (char)(0xE0 | (code >> 12));
                utf8[1] = (char)(0x80 | ((code >> 6) & 0x3F));
                utf8[2] = (char)(0x80 | (code & 0x3F));
                n = 3;
            }
            else
            {
                utf8[0] = (char)(0xF0 | (code >> 18));
                utf8[1] = (char)(0x80 | ((code >> 12) & 0x3F));
                utf8[2] = (char)(0x80 | ((code >> 6) & 0x3F));
                utf8[3] = (char)(0x80 | (code & 0x3F));
                n = 4;
            }
            ai_add(&out, utf8, (size_t)n);
            continue;
        }
        switch (c)
        {
        case 'n': ai_add(&out, "\n", 1); break;
        case 't': ai_add(&out, "\t", 1); break;
        case 'r': ai_add(&out, "\r", 1); break;
        case 'b': ai_add(&out, "\b", 1); break;
        case 'f': ai_add(&out, "\f", 1); break;
        case '\0':
            free(out.data);
            return NULL;
        default:  ai_add(&out, &c, 1); break; /* \" \\ \/ */
        }
    }
    if (out.failed)
    {
        free(out.data);
        return NULL;
    }
    return out.data;
}

/* Value of key in the object at p, or NULL. */
static const char *json_get(const char *p, const char *key)
{
    size_t key_length = strlen(key);

    if (p == NULL)
        return NULL;
    p = json_ws(p);
    if (*p != '{')
        return NULL;
    p = json_ws(p + 1);
    while (*p == '"')
    {
        const char *name = p + 1;
        const char *after = json_skip_string(p);

        if (after == NULL)
            return NULL;
        p = json_ws(after);
        if (*p != ':')
            return NULL;
        p = json_ws(p + 1);
        if ((size_t)(after - 1 - name) == key_length &&
            memcmp(name, key, key_length) == 0)
            return p;
        p = json_skip(p);
        if (p == NULL)
            return NULL;
        p = json_ws(p);
        if (*p != ',')
            return NULL;
        p = json_ws(p + 1);
    }
    return NULL;
}

/* Element index of the array at p, or NULL. */
static const char *json_at(const char *p, size_t index)
{
    if (p == NULL)
        return NULL;
    p = json_ws(p);
    if (*p != '[')
        return NULL;
    p = json_ws(p + 1);
    if (*p == ']')
        return NULL;
    while (index-- > 0)
    {
        p = json_skip(p);
        if (p == NULL)
            return NULL;
        p = json_ws(p);
        if (*p != ',')
            return NULL;
        p = json_ws(p + 1);
    }
    return p;
}

static char *ai_parse_answer(const char *json, int anthropic,
                             char *error, size_t error_size)
{
    const char *message = json_get(json_get(json, "error"), "message");

    if (message != NULL)
    {
        char *text = json_string(message);
        snprintf(error, error_size, "AI provider error: %s",
                 text != NULL ? text : "(unreadable)");
        free(text);
        return NULL;
    }

    if (anthropic)
    {
        /* content: [{"type":"text","text":"..."}, ...], text blocks joined */
        const char *content = json_get(json, "content");
        ai_buffer_t answer = { NULL, 0, 0, 0 };
        const char *block;
        size_t i;

        for (i = 0; (block = json_at(content, i)) != NULL; ++i)
        {
            const char *text = json_get(block, "text");
            char *decoded;

            if (text == NULL)
                continue;
            decoded = json_string(text);
            if (decoded != NULL)
                ai_adds(&answer, decoded);
            free(decoded);
        }
        if (answer.data != NULL && !answer.failed)
            return answer.data;
        free(answer.data);
    }
    else
    {
        /* choices[0].message.content */
        const char *content = json_get(json_get(
            json_at(json_get(json, "choices"), 0), "message"), "content");
        char *text = content != NULL ? json_string(content) : NULL;

        if (text != NULL)
            return text;
    }

    snprintf(error, error_size,
             "AI response has no answer in it: %.200s", json);
    return NULL;
}

/* ------------------------------------------------------------- */
/* request                                                          */
/* ------------------------------------------------------------- */

#ifndef __wasi__

/* Full path of the curl program, malloc'd, or NULL. */
static char *ai_find_curl(void)
{
    char *configured = ussr_env_get("USSR_AI_CURL");

    if (configured != NULL && configured[0] != '\0')
        return configured;
    free(configured);
#ifdef _WIN32
    {
        char path[MAX_PATH];
        DWORD found = SearchPathA(NULL, "curl.exe", NULL, MAX_PATH, path, NULL);

        if (found > 0 && found < MAX_PATH)
        {
            char *copy = malloc(strlen(path) + 1);
            if (copy != NULL)
                strcpy(copy, path);
            return copy;
        }
    }
#else
    {
        char *search = ussr_env_get("PATH");
        char *dir;
        char *rest;

        if (search == NULL)
            return NULL;
        for (dir = search; dir != NULL; dir = rest)
        {
            char *candidate;

            rest = strchr(dir, ':');
            if (rest != NULL)
                *rest++ = '\0';
            candidate = malloc(strlen(dir) + sizeof("/curl") + 1);
            if (candidate == NULL)
                break;
            sprintf(candidate, "%s/curl", dir[0] != '\0' ? dir : ".");
            if (access(candidate, X_OK) == 0)
            {
                free(search);
                return candidate;
            }
            free(candidate);
        }
        free(search);
    }
#endif
    return NULL;
}

/* An environment variable, or fallback when it is unset or empty;
 * malloc'd either way (NULL when both are missing). */
static char *ai_setting(const char *name, const char *fallback)
{
    char *value = ussr_env_get(name);

    if (value != NULL && value[0] != '\0')
        return value;
    free(value);
    if (fallback == NULL)
        return NULL;
    value = malloc(strlen(fallback) + 1);
    if (value != NULL)
        strcpy(value, fallback);
    return value;
}

static int ai_positive(const char *text, long *out)
{
    char *end;
    long value;

    if (text == NULL)
        return 0;
    value = strtol(text, &end, 10);
    if (end == text || *end != '\0' || value <= 0)
        return 0;
    *out = value;
    return 1;
}

char *ussr_ai_prompt(const char *question, char *error, size_t error_size)
{
    char *provider = ai_setting("USSR_AI_PROVIDER", "anthropic");
    char *url = NULL;
    char *model = NULL;
    char *key = NULL;
    char *system = NULL;
    char *max_tokens_text = NULL;
    char *timeout_text = NULL;
    char *curl = NULL;
    char *output = NULL;
    char *answer = NULL;
    ai_buffer_t body = { NULL, 0, 0, 0 };
    ai_buffer_t config = { NULL, 0, 0, 0 };
    long max_tokens = 1024;
    long timeout = 120;
    int anthropic;
    int exit_code = 0;
    char number[64];

    if (provider == NULL)
    {
        snprintf(error, error_size, "out of memory");
        return NULL;
    }
    for (anthropic = 0; provider[anthropic] != '\0'; ++anthropic)
        provider[anthropic] = (char)tolower((unsigned char)provider[anthropic]);
    if (strcmp(provider, "anthropic") == 0 || strcmp(provider, "claude") == 0)
        anthropic = 1;
    else if (strcmp(provider, "openai") == 0)
        anthropic = 0;
    else
    {
        snprintf(error, error_size,
                 "USSR_AI_PROVIDER must be anthropic or openai, not '%s'",
                 provider);
        free(provider);
        return NULL;
    }
    free(provider);

    url = ai_setting("USSR_AI_URL", anthropic ? AI_DEFAULT_ANTHROPIC_URL
                                              : AI_DEFAULT_OPENAI_URL);
    model = ai_setting("USSR_AI_MODEL",
                       anthropic ? AI_DEFAULT_ANTHROPIC_MODEL : NULL);
    key = ai_setting("USSR_AI_KEY", NULL);
    if (key == NULL)
        key = ai_setting(anthropic ? "ANTHROPIC_API_KEY" : "OPENAI_API_KEY",
                         NULL);
    system = ai_setting("USSR_AI_SYSTEM", NULL);
    max_tokens_text = ai_setting("USSR_AI_MAX_TOKENS", NULL);
    timeout_text = ai_setting("USSR_AI_TIMEOUT", NULL);

    if (url == NULL)
    {
        snprintf(error, error_size, "out of memory");
        goto done;
    }
    if (model == NULL)
    {
        snprintf(error, error_size,
                 "set USSR_AI_MODEL to the model to ask");
        goto done;
    }
    if (key == NULL && (strcmp(url, AI_DEFAULT_ANTHROPIC_URL) == 0 ||
                        strcmp(url, AI_DEFAULT_OPENAI_URL) == 0))
    {
        snprintf(error, error_size, "set USSR_AI_KEY to your %s API key",
                 anthropic ? "Anthropic" : "OpenAI");
        goto done;
    }
    if (max_tokens_text != NULL && !ai_positive(max_tokens_text, &max_tokens))
    {
        snprintf(error, error_size,
                 "USSR_AI_MAX_TOKENS must be a positive number");
        goto done;
    }
    if (timeout_text != NULL && !ai_positive(timeout_text, &timeout))
    {
        snprintf(error, error_size,
                 "USSR_AI_TIMEOUT must be a positive number of seconds");
        goto done;
    }

    curl = ai_find_curl();
    if (curl == NULL)
    {
        snprintf(error, error_size,
                 "the curl program is needed; put it on PATH "
                 "or set USSR_AI_CURL to its path");
        goto done;
    }

    /* request body */
    ai_adds(&body, "{\"model\":");
    ai_add_json_string(&body, model);
    if (anthropic || max_tokens_text != NULL)
    {
        snprintf(number, sizeof(number), ",\"max_tokens\":%ld", max_tokens);
        ai_adds(&body, number);
    }
    if (anthropic && system != NULL)
    {
        ai_adds(&body, ",\"system\":");
        ai_add_json_string(&body, system);
    }
    ai_adds(&body, ",\"messages\":[");
    if (!anthropic && system != NULL)
    {
        ai_adds(&body, "{\"role\":\"system\",\"content\":");
        ai_add_json_string(&body, system);
        ai_adds(&body, "},");
    }
    ai_adds(&body, "{\"role\":\"user\",\"content\":");
    ai_add_json_string(&body, question);
    ai_adds(&body, "}]}");

    /* curl config, read from stdin so the key stays off the command line */
    ai_adds(&config, "silent\nshow-error\n");
    snprintf(number, sizeof(number), "max-time = %ld\n", timeout);
    ai_adds(&config, number);
    ai_adds(&config, "url = ");
    ai_add_config_string(&config, url);
    ai_adds(&config, "header = \"content-type: application/json\"\n");
    if (anthropic)
        ai_adds(&config, "header = \"anthropic-version: 2023-06-01\"\n");
    if (key != NULL)
    {
        ai_buffer_t header = { NULL, 0, 0, 0 };

        ai_adds(&header, anthropic ? "x-api-key: " : "Authorization: Bearer ");
        ai_adds(&header, key);
        ai_adds(&config, "header = ");
        if (header.data != NULL)
            ai_add_config_string(&config, header.data);
        free(header.data);
    }
    ai_adds(&config, "data-binary = ");
    if (body.data != NULL)
        ai_add_config_string(&config, body.data);
    ai_adds(&config, "write-out = \"\\n%{http_code}\"\n");

    if (body.failed || config.failed || config.data == NULL)
    {
        snprintf(error, error_size, "out of memory");
        goto done;
    }

    {
        /* -q: ignore ~/.curlrc; -K -: the config above on stdin */
        char *argv[] = { curl, "-q", "-K", "-", NULL };
        ussr_process_t process;

        process.program = curl;
        process.argv = argv;
        process.stdin_data = config.data;
        process.stdout_data = &output;
        process.capture_stdout = 1;
        if (ussr_process_run(&process, &exit_code) != 0 || output == NULL)
        {
            snprintf(error, error_size, "could not run %s", curl);
            goto done;
        }
    }

    {
        /* the last line is the HTTP status from write-out */
        char *status_line = strrchr(output, '\n');
        long status = 0;

        if (status_line != NULL)
        {
            *status_line++ = '\0';
            status = strtol(status_line, NULL, 10);
        }
        if (exit_code != 0 && output[0] == '\0')
        {
            snprintf(error, error_size,
                     "curl failed (exit code %d) reaching %s", exit_code, url);
            goto done;
        }
        answer = ai_parse_answer(output, anthropic, error, error_size);
        if (answer == NULL && status >= 400 &&
            strncmp(error, "AI provider error", 17) != 0)
            snprintf(error, error_size, "HTTP %ld from %s: %.200s",
                     status, url, output);
    }

done:
    /* the config holds the API key: wipe it before freeing */
    if (config.data != NULL)
        memset(config.data, 0, config.capacity);
    free(config.data);
    free(body.data);
    free(output);
    free(curl);
    free(timeout_text);
    free(max_tokens_text);
    free(system);
    free(key);
    free(model);
    free(url);
    return answer;
}

#else /* __wasi__ */

char *ussr_ai_prompt(const char *question, char *error, size_t error_size)
{
    (void)question;
    (void)ai_parse_answer;
    snprintf(error, error_size, "prompt is not available in the WASM build");
    return NULL;
}

#endif
