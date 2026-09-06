%code requires
{
#include <stddef.h>
#include "ussr.h"
}

%{
#include <stdio.h>
#include <stdlib.h>

#include "ussr.h"

int yylex(void);
void yyerror(const char *message);

extern int yylineno;
extern FILE *yyin;
%}

%union
{
    char *string;
    long integer;
    double real;
    int boolean;

    ussr_value_t value;

    struct
    {
        ussr_value_t *values;
        size_t count;
    } parameters;
}

%token <string> IDENTIFIER
%token <string> STRING
%token <integer> INTEGER
%token <real> REAL
%token <boolean> BOOLEAN

%token NULL_VALUE
%token LBRACKET
%token RBRACKET
%token COLON
%token LPAREN
%token RPAREN
%token NEWLINE
%token COMMENT

%type <value> parameter
%type <parameters> parameter_list

%start program

%%

program
    : command_list
    ;

command_list
    : LBRACKET command_lines RBRACKET
    ;

command_lines
    : %empty
    | command_lines command_line
    ;

command_line
    : NEWLINE
    | COMMENT NEWLINE
    | command NEWLINE
    ;

command
    : IDENTIFIER
      LPAREN IDENTIFIER RPAREN
      COLON parameter_list
      {
          if (ussr_execute_command(
                  $1,
                  $3,
                  $6.values,
                  $6.count) != 0)
          {
              free($1);
              free($3);
              free($6.values);
              YYERROR;
          }

          free($1);
          free($3);
          free($6.values);
      }
    ;

parameter_list
    : parameter
      {
          $$.values = malloc(sizeof(ussr_value_t));

          if ($$.values == NULL)
              YYABORT;

          $$.values[0] = $1;
          $$.count = 1;
      }
    | parameter_list parameter
      {
          ussr_value_t *new_values;

          new_values = realloc(
              $1.values,
              ($1.count + 1) * sizeof(ussr_value_t)
          );

          if (new_values == NULL)
          {
              size_t i;

              for (i = 0; i < $1.count; ++i)
                  ussr_value_free(&$1.values[i]);

              free($1.values);

              YYABORT;
          }

          new_values[$1.count] = $2;

          $$.values = new_values;
          $$.count = $1.count + 1;
      }
    ;

parameter
    : STRING
      {
          $$.type = USSR_STRING;
          $$.data.string = $1;
      }
    | INTEGER
      {
          $$.type = USSR_INTEGER;
          $$.data.integer = $1;
      }
    | REAL
      {
          $$.type = USSR_REAL;
          $$.data.real = $1;
      }
    | BOOLEAN
      {
          $$.type = USSR_BOOLEAN;
          $$.data.boolean = $1;
      }
    | NULL_VALUE
      {
          $$.type = USSR_NULL;
      }
    | IDENTIFIER
      {
          const ussr_value_t *value;

          value = ussr_get_variable($1);

          if (value == NULL)
          {
              fprintf(
                  stderr,
                  "USSR: undefined variable '%s'\n",
                  $1
              );

              free($1);
              YYERROR;
          }

          $$ = ussr_value_copy(value);

          free($1);
      }
    ;

%%

void
yyerror(const char *message)
{
    fprintf(
        stderr,
        "USSR: syntax error at line %d: %s\n",
        yylineno,
        message
    );
}