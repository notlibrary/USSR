%{
#include "parser.tab.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern int fileno(FILE *);

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

%%

[ \t\r]+                    ;

\#[^\r\n]*                  { return COMMENT; }
\n                          { return NEWLINE; }

"if"                        { return IF; }
"else"                      { return ELSE; }

"true"                      { yylval.boolean = 1; return BOOLEAN; }
"false"                     { yylval.boolean = 0; return BOOLEAN; }
"null"                      { return NULL_VALUE; }

">="                        { return GREATER_EQUAL; }
"<="                        { return LESS_EQUAL; }
"=="                        { return EQUAL; }
"!="                        { return NOT_EQUAL; }
">"                         { return GREATER; }
"<"                         { return LESS; }

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
                case 'n':
                    output[output_index++] = '\n';
                    break;

                case 't':
                    output[output_index++] = '\t';
                    break;

                case 'r':
                    output[output_index++] = '\r';
                    break;

                case '\\':
                    output[output_index++] = '\\';
                    break;

                case '"':
                    output[output_index++] = '"';
                    break;

                default:
                    output[output_index++] = text[i];
                    break;
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