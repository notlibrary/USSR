%{
#include "parser.tab.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

extern int fileno(FILE *);
extern int ussr_lexer_reported;

/* yylineno already counts the token's own newlines here: report the line
 * the token starts on. */
#define YY_USER_ACTION                                  \
    {                                                   \
        int ussr_i;                                     \
        yylloc.first_line = yylineno;                   \
        for (ussr_i = 0; ussr_i < (int)yyleng; ++ussr_i) \
            if (yytext[ussr_i] == '\n')                 \
                --yylloc.first_line;                    \
        yylloc.last_line = yylineno;                    \
    }

static char *ussr_strdup(const char *text);
static char *unquote_string(const char *text);
%}

%option noyywrap
%option nodefault
%option yylineno
%option nointeractive
%option noinput
%option nounput

DIGIT       [0-9]
LETTER      [a-zA-Z_]
IDENT       {LETTER}({LETTER}|{DIGIT})*
INTEGER     {DIGIT}+
REAL        {DIGIT}+"."{DIGIT}+
STRING      \"([^\"\\\n]|\\.)*\"
UNO         '([^'\\\n]|\\.)*'

%%

[ \t\r]+                    ;
\#[^\r\n]*                  ;
\n                          { return NEWLINE; }

"true"                      { yylval.boolean = 1; return BOOLEAN; }
"false"                     { yylval.boolean = 0; return BOOLEAN; }
"null"                      { return NULL_VALUE; }

">="                        { return GREATER_EQUAL; }
"<="                        { return LESS_EQUAL; }
"!="                        { return NOT_EQUAL; }
"="                         { return EQUAL; }
">"                         { return GREATER; }
"<"                         { return LESS; }

"&&"                        { return LOGICAL_AND; }
"||"                        { return LOGICAL_OR; }
"<<"                        { return SHIFT_LEFT; }
">>"                        { return SHIFT_RIGHT; }
"^"                         { return BITWISE_XOR; }
"&"                         { return BITWISE_AND; }
"|"                         { return BITWISE_OR; }

"+"                         { return PLUS; }
"-"                         { return MINUS; }
"*"                         { return MULTIPLY; }
"/"                         { return DIVIDE; }
"%"                         { return MODULO; }
"?"                         { return QUESTION; }
"!"                         { return EXCLAMATION; }

"["                         { return LBRACKET; }
"@"                         { return AT; }
"]"                         { return RBRACKET; }
"{"                         { return LBRACE; }
"}"                         { return RBRACE; }
"("                         { return LPAREN; }
")"                         { return RPAREN; }
":"                         { return COLON; }

{REAL}                      {
    yylval.real = strtod(yytext, NULL);
    return REAL;
}

{INTEGER}                   {
    errno = 0;
    yylval.integer = strtoll(yytext, NULL, 10);

    if (errno == ERANGE)
    {
        const char *file;
        int line;

        ussr_parse_locate(yylineno, &file, &line);
        fprintf(
            stderr,
            "%s:%d: integer %s is outside -9223372036854775808..9223372036854775807 (write it as a real, e.g. %s.0)\n",
            file,
            line,
            yytext,
            yytext
        );
        ussr_lexer_reported = 1;
        return LEX_ERROR;
    }

    return INTEGER;
}

{STRING}                    {
    yylval.string = unquote_string(yytext);

    if (yylval.string == NULL)
        return 0;

    return STRING;
}

{UNO}                       {
    size_t length = yyleng;
    char *value = malloc(length - 1);

    if (value == NULL)
        return 0;

    memcpy(value, yytext + 1, length - 2);
    value[length - 2] = '\0';
    yylval.string = value;
    return UNO_LITERAL;
}

[A-Za-z0-9_./\\:-]+\( {
    size_t length = yyleng - 1;

    yylval.string = ussr_strdup(yytext);

    if (yylval.string == NULL)
        return 0;

    yylval.string[length] = '\0';

    yyless((int)length);

    return COMMAND_HEAD;
}

{IDENT}                     {
    yylval.string = ussr_strdup(yytext);

    if (yylval.string == NULL)
        return 0;

    return IDENTIFIER;
}

.                           {
    const char *file;
    int line;

    ussr_parse_locate(yylineno, &file, &line);
    fprintf(
        stderr,
        "%s:%d: unexpected character '%s'\n",
        file,
        line,
        yytext
    );

    ussr_lexer_reported = 1;
    return LEX_ERROR;
}

%%

static char *
ussr_strdup(const char *text)
{
    size_t length;
    char *copy;

    if (text == NULL)
        return NULL;

    length = strlen(text) + 1;
    copy = malloc(length);

    if (copy == NULL)
        return NULL;

    memcpy(copy, text, length);
    return copy;
}

static char *
unquote_string(const char *text)
{
    size_t length;
    size_t i;
    size_t output_index;
    char *output;

    if (text == NULL)
        return NULL;

    length = strlen(text);

    if (length < 2)
        return NULL;

    output = malloc(length);
    if (output == NULL)
        return NULL;

    output_index = 0;

    for (i = 1; i < length - 1; ++i)
    {
        if (text[i] == '\\' && i + 1 < length - 1)
        {
            ++i;

            switch (text[i])
            {
                case 'n': output[output_index++] = '\n'; break;
                case 't': output[output_index++] = '\t'; break;
                case 'r': output[output_index++] = '\r'; break;
                case '\\': output[output_index++] = '\\'; break;
                case '"': output[output_index++] = '"'; break;
                default: output[output_index++] = text[i]; break;
            }
        }
        else
        {
            output[output_index++] = text[i];
        }
    }

    output[output_index] = '\0';
    return output;
}
