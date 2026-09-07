%code requires
{
#include <stddef.h>
#include "ussr.h"

struct ussr_command_list_t;

typedef enum
{
    USSR_EXPR_VALUE,
    USSR_EXPR_COMPARE
} ussr_expression_type_t;

typedef enum
{
    USSR_CMP_GT,
    USSR_CMP_LT,
    USSR_CMP_GE,
    USSR_CMP_LE,
    USSR_CMP_EQ,
    USSR_CMP_NE
} ussr_comparison_t;

typedef struct ussr_expression_t
{
    ussr_expression_type_t type;

    union
    {
        ussr_value_t value;

        struct
        {
            ussr_value_t left;
            ussr_comparison_t operator;
            ussr_value_t right;
        } compare;
    } data;
} ussr_expression_t;
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

    ussr_expression_t *expression;

    struct ussr_command_list_t *command_list;

    struct
    {
        ussr_value_t *values;
        size_t count;
    } parameters;

    struct
    {
        ussr_expression_t *expression;
        char *return_variable;
        struct ussr_command_list_t *then_list;
        struct ussr_command_list_t *else_list;
    } conditional;
}

%token <string> IDENTIFIER
%token <string> STRING
%token <integer> INTEGER
%token <real> REAL
%token <boolean> BOOLEAN

%token NULL_VALUE

%token LBRACKET
%token RBRACKET
%token LBRACE
%token RBRACE

%token COLON
%token LPAREN
%token RPAREN

%token GREATER
%token LESS
%token GREATER_EQUAL
%token LESS_EQUAL
%token EQUAL
%token NOT_EQUAL

%token IF
%token ELSE

%token NEWLINE
%token COMMENT

%type <value> parameter
%type <parameters> parameter_list

%type <expression> expression
%type <expression> boolean_expression

%type <command_list> command_list
%type <command_list> optional_else

%type <conditional> conditional

%start program

%%

program
    : command_list
    ;

command_list
    : LBRACKET command_lines RBRACKET
      {
          $$ = NULL;
      }
    ;

command_lines
    : %empty
    | command_lines command_line
    ;

command_line
    : NEWLINE
    | COMMENT NEWLINE
    | command NEWLINE
    | conditional NEWLINE
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

conditional
    : IF
      LPAREN IDENTIFIER RPAREN
      COLON expression
      command_list
      optional_else
      {
          $$.expression = $6;
          $$.return_variable = $3;
          $$.then_list = $7;
          $$.else_list = $8;

 //         ussr_expression_free($$.expression);
          free($$.return_variable);
      }
    ;

optional_else
    : %empty
      {
          $$ = NULL;
      }

    | ELSE
      COLON
      command_list
      {
          $$ = $3;
      }
    ;

expression
    : LBRACE boolean_expression RBRACE
      {
          $$ = $2;
      }
    ;

boolean_expression
    : parameter GREATER parameter
      {
          $$ = malloc(sizeof(*$$));

          if ($$ == NULL)
              YYABORT;

          $$->type = USSR_EXPR_COMPARE;
          $$->data.compare.left = $1;
          $$->data.compare.operator = USSR_CMP_GT;
          $$->data.compare.right = $3;
      }

    | parameter LESS parameter
      {
          $$ = malloc(sizeof(*$$));

          if ($$ == NULL)
              YYABORT;

          $$->type = USSR_EXPR_COMPARE;
          $$->data.compare.left = $1;
          $$->data.compare.operator = USSR_CMP_LT;
          $$->data.compare.right = $3;
      }

    | parameter GREATER_EQUAL parameter
      {
          $$ = malloc(sizeof(*$$));

          if ($$ == NULL)
              YYABORT;

          $$->type = USSR_EXPR_COMPARE;
          $$->data.compare.left = $1;
          $$->data.compare.operator = USSR_CMP_GE;
          $$->data.compare.right = $3;
      }

    | parameter LESS_EQUAL parameter
      {
          $$ = malloc(sizeof(*$$));

          if ($$ == NULL)
              YYABORT;

          $$->type = USSR_EXPR_COMPARE;
          $$->data.compare.left = $1;
          $$->data.compare.operator = USSR_CMP_LE;
          $$->data.compare.right = $3;
      }

    | parameter EQUAL parameter
      {
          $$ = malloc(sizeof(*$$));

          if ($$ == NULL)
              YYABORT;

          $$->type = USSR_EXPR_COMPARE;
          $$->data.compare.left = $1;
          $$->data.compare.operator = USSR_CMP_EQ;
          $$->data.compare.right = $3;
      }

    | parameter NOT_EQUAL parameter
      {
          $$ = malloc(sizeof(*$$));

          if ($$ == NULL)
              YYABORT;

          $$->type = USSR_EXPR_COMPARE;
          $$->data.compare.left = $1;
          $$->data.compare.operator = USSR_CMP_NE;
          $$->data.compare.right = $3;
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

static void
ussr_expression_free(struct ussr_expression_t *expression)
{
    if (expression == NULL)
        return;

    if (expression->type == USSR_EXPR_VALUE)
    {
        ussr_value_free(&expression->data.value);
    }
    else if (expression->type == USSR_EXPR_COMPARE)
    {
        ussr_value_free(&expression->data.compare.left);
        ussr_value_free(&expression->data.compare.right);
    }

    free(expression);
}

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