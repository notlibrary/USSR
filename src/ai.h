#ifndef USSR_AI_H
#define USSR_AI_H

#include <stddef.h>

/*
 * prompt(answer): "To be or not to be?"
 *
 * Sends one question to a chat model and returns its text answer.
 * http:// and https:// endpoints are reached through ryazan, ussr's TLS
 * library (src/ryazan.h: Schannel on Windows, OpenSSL elsewhere), which
 * checks the server's certificate. Other URLs, builds without TLS, and
 * any run with USSR_AI_CURL set go through the curl program instead; the
 * request, API key included, then reaches curl on its standard input as
 * a curl config, never on a command line.
 *
 * Configuration, all from environment variables (setenv works too):
 *   USSR_AI_PROVIDER    anthropic (default) or openai; openai also
 *                       covers OpenAI-compatible servers (Ollama,
 *                       LM Studio, OpenRouter, vLLM, llama.cpp, ...)
 *   USSR_AI_URL         endpoint URL; defaults to the provider's own
 *   USSR_AI_MODEL       model name; anthropic defaults to
 *                       claude-sonnet-5-5, openai has no default
 *   USSR_AI_KEY         API key; falls back to ANTHROPIC_API_KEY or
 *                       OPENAI_API_KEY; may be unset for local servers
 *   USSR_AI_SYSTEM      optional system prompt
 *   USSR_AI_MAX_TOKENS  answer length limit (anthropic default 1024)
 *   USSR_AI_TIMEOUT     seconds to wait, default 120
 *   USSR_AI_CURL        use the curl program at this path instead of
 *                       ryazan
 */

/* Returns the answer as a malloc'd string, or NULL with a message in
 * error. */
char *ussr_ai_prompt(const char *question, char *error, size_t error_size);

#endif /* USSR_AI_H */
