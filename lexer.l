%{
#include "parser.tab.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern int fileno(FILE *);

static char *ussr_strdup(const char *text);
static char *unquote_string(const char *text);

static char *ussr_strdup(const char *text)
{
    size_t length;
    char *result;

    if (text == NULL)
        return NULL;

    length = strlen(text);

    result = malloc(length + 1);

    if (result == NULL)
        return NULL;

    memcpy(result, text, length + 1);

    return result;
}

static char *unquote_string(const char *text)
{
    size_t length;
    size_t i;
    size_t j;
    char *result;

    if (text == NULL)
        return NULL;

    length = strlen(text);

    if (length < 2 ||
        text[0] != '"' ||
        text[length - 1] != '"')
        return NULL;

    result = malloc(length);

    if (result == NULL)
        return NULL;

    j = 0;

    for (i = 1; i < length - 1; ++i)
    {
        if (text[i] == '\\' && i + 1 < length - 1)
        {
            ++i;

            switch (text[i])
            {
                case 'n':
                    result[j++] = '\n';
                    break;

                case 'r':
                    result[j++] = '\r';
                    break;

                case 't':
                    result[j++] = '\t';
                    break;

                case '\\':
                    result[j++] = '\\';
                    break;

                case '"':
                    result[j++] = '"';
                    break;

                default:
                    result[j++] = text[i];
                    break;
            }
        }
        else
        {
            result[j++] = text[i];
        }
    }

    result[j] = '\0';

    return result;
}
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

%%

[ \t\r]+                    ;
\#[^\r\n]*                  ;
\n                          { return NEWLINE; }

"true"                      { yylval.boolean = 1; return BOOLEAN; }
"false"                     { yylval.boolean = 0; return BOOLEAN; }
"null"                      { return NULL_VALUE; }


">="                        { return GREATER_EQUAL; }
"<="                        { return LESS_EQUAL; }
"=="                        { return EQUAL; }
"!="                        { return NOT_EQUAL; }
">"                         { return GREATER; }
"<"                         { return LESS; }

"+"                         { return PLUS; }
"-"                         { return MINUS; }
"*"                         { return MULTIPLY; }
"/"                         { return DIVIDE; }
"%"                         { return MODULO; }


"?"                         { return QUESTION; }
"!"                         { return EXCLAMATION; }


"["                         { return LBRACKET; }
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
    yylval.integer = strtol(yytext, NULL, 10);
    return INTEGER;
}

{STRING}                    {
    yylval.string = unquote_string(yytext);

    if (yylval.string == NULL)
        return 0;

    return STRING;
}

{IDENT}                     {
    yylval.string = ussr_strdup(yytext);

    if (yylval.string == NULL)
        return 0;

    return IDENTIFIER;
}

.                           {
    fprintf(
        stderr,
        "USSR: unexpected character '%s' at line %d\n",
        yytext,
        yylineno
    );

    return 0;
}

%%