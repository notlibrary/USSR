%code requires
{
#include <stddef.h>
#include "ussr.h"
}

%{
#include <stdio.h>
#include <stdlib.h>

#include "ussr.h"

extern int yylineno;
extern FILE *yyin;

int yylex(void);
void yyerror(const char *message)
{
    fprintf(stderr, "USSR: %s at line %d\n", message, yylineno);
}


ussr_command_list_t *ussr_parsed_program = NULL;

ussr_command_t *ussr_command_create(
    char *name,
    char *return_name,
    ussr_argument_t *arguments,
    size_t argument_count
);

ussr_command_list_t *ussr_command_list_create(void);

int ussr_command_list_append(
    ussr_command_list_t *list,
    ussr_command_t *command
);
%}

%union
{
    char *string;
    long integer;
    double real;
    int boolean;

    ussr_value_t value;
    ussr_expression_t *expression;
    ussr_command_list_t *command_list;
    ussr_command_t *command;

    ussr_argument_t argument;

    struct
    {
        ussr_argument_t *items;
        size_t count;
    } arguments;
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

%token PLUS
%token MINUS
%token MULTIPLY
%token DIVIDE
%token MODULO

%token NEWLINE
%token COMMENT
%token QUESTION
%token EXCLAMATION

%type <command_list> program
%type <command_list> command_lines
%type <command> command
%type <command> command_line
%type <arguments> argument_list
%type <argument> argument
%type <argument> argument_base
%type <argument> block_argument
%type <expression> expression
%type <expression> comparison_expression
%type <expression> additive_expression
%type <expression> multiplicative_expression
%type <expression> unary_expression
%type <expression> primary_expression
%type <value> literal

%start program

%%

program
    : command_lines
      {
          ussr_parsed_program = $1;
          $$ = $1;
      }
    | LBRACKET command_lines RBRACKET
      {
          ussr_parsed_program = $2;
          $$ = $2;
      }
    ;

command_lines
    : %empty
      {
          $$ = ussr_command_list_create();

          if ($$ == NULL)
              YYABORT;
      }
    | command_lines command_line
      {
          if ($2 != NULL)
          {
              if (ussr_command_list_append($1, $2) != 0)
              {
                  ussr_command_list_free($1);
                  YYABORT;
              }
          }

          $$ = $1;
      }
    ;

command_line
    : NEWLINE
      {
          $$ = NULL;
      }
    | COMMENT NEWLINE
      {
          $$ = NULL;
      }
    | command NEWLINE
      {
          $$ = $1;
      }
    ;

command
    : IDENTIFIER LPAREN IDENTIFIER RPAREN COLON argument_list
      {
          $$ = ussr_command_create(
              $1,
              $3,
              $6.items,
              $6.count
          );

          if ($$ == NULL)
          {
              free($1);
              free($3);
              free($6.items);
              YYABORT;
          }
      }
    ;

argument_list
    : argument
      {
          $$.items = malloc(sizeof(*$$.items));

          if ($$.items == NULL)
          {
              YYABORT;
          }

          $$.items[0] = $1;
          $$.count = 1;
      }
    | argument_list argument
      {
          ussr_argument_t *items;

          items = realloc(
              $1.items,
              ($1.count + 1) * sizeof(*items)
          );

          if (items == NULL)
          {
              free($1.items);
              YYABORT;
          }

          items[$1.count] = $2;

          $$.items = items;
          $$.count = $1.count + 1;
      }
    ;

argument
    : argument_base
      {
          $$ = $1;
      }
    | IDENTIFIER QUESTION
      {
          ussr_expression_t *expression;

          expression = malloc(sizeof(*expression));
          if (expression == NULL)
          {
              free($1);
              YYABORT;
          }

          expression->type = USSR_EXPR_VARIABLE;
          expression->data.variable = $1;

          $$.type = USSR_ARGUMENT_LOOKUP;
          $$.assignment = 0;
          $$.data.expression = expression;
      }
    | argument_base EXCLAMATION
      {
          $$ = $1;
          $$.assignment = 1;
      }
    ;


block_argument
    : LBRACKET command_lines RBRACKET
      {
          $$.type = USSR_ARGUMENT_COMMAND_LIST;
          $$.assignment = 0;
          $$.data.command_list = $2;
      }
    ;

argument_base
    : STRING
      {
          $$.type = USSR_ARGUMENT_VALUE;
          $$.assignment = 0;
          $$.data.value = ussr_string($1);
          free($1);
      }
    | INTEGER
      {
          $$.type = USSR_ARGUMENT_VALUE;
          $$.assignment = 0;
          $$.data.value = ussr_integer($1);
      }
    | REAL
      {
          $$.type = USSR_ARGUMENT_VALUE;
          $$.assignment = 0;
          $$.data.value = ussr_real($1);
      }
    | BOOLEAN
      {
          $$.type = USSR_ARGUMENT_VALUE;
          $$.assignment = 0;
          $$.data.value = ussr_boolean($1);
      }
    | IDENTIFIER
      {
          ussr_expression_t *expression;

          expression = malloc(sizeof(*expression));
          if (expression == NULL)
          {
              free($1);
              YYABORT;
          }

          expression->type = USSR_EXPR_VARIABLE;
          expression->data.variable = $1;

          $$.type = USSR_ARGUMENT_EXPRESSION;
          $$.assignment = 0;
          $$.data.expression = expression;
      }
    | LPAREN expression RPAREN
      {
          $$.type = USSR_ARGUMENT_EXPRESSION;
          $$.assignment = 0;
          $$.data.expression = $2;
      }
    | block_argument
      {
          $$ = $1;
      }
    ;

literal
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
    ;

expression
    : LBRACE comparison_expression RBRACE
      {
          $$ = $2;
      }
    ;

comparison_expression
    : additive_expression
      {
          $$ = $1;
      }
    | additive_expression GREATER additive_expression
      {
          $$ = malloc(sizeof(*$$));

          if ($$ == NULL)
              YYABORT;

          $$->type = USSR_EXPR_BINARY;
          $$->data.binary.left = $1;
          $$->data.binary.operator = USSR_OP_GT;
          $$->data.binary.right = $3;
      }
    | additive_expression LESS additive_expression
      {
          $$ = malloc(sizeof(*$$));

          if ($$ == NULL)
              YYABORT;

          $$->type = USSR_EXPR_BINARY;
          $$->data.binary.left = $1;
          $$->data.binary.operator = USSR_OP_LT;
          $$->data.binary.right = $3;
      }
    | additive_expression GREATER_EQUAL additive_expression
      {
          $$ = malloc(sizeof(*$$));

          if ($$ == NULL)
              YYABORT;

          $$->type = USSR_EXPR_BINARY;
          $$->data.binary.left = $1;
          $$->data.binary.operator = USSR_OP_GE;
          $$->data.binary.right = $3;
      }
    | additive_expression LESS_EQUAL additive_expression
      {
          $$ = malloc(sizeof(*$$));

          if ($$ == NULL)
              YYABORT;

          $$->type = USSR_EXPR_BINARY;
          $$->data.binary.left = $1;
          $$->data.binary.operator = USSR_OP_LE;
          $$->data.binary.right = $3;
      }
    | additive_expression EQUAL additive_expression
      {
          $$ = malloc(sizeof(*$$));

          if ($$ == NULL)
              YYABORT;

          $$->type = USSR_EXPR_BINARY;
          $$->data.binary.left = $1;
          $$->data.binary.operator = USSR_OP_EQ;
          $$->data.binary.right = $3;
      }
    | additive_expression NOT_EQUAL additive_expression
      {
          $$ = malloc(sizeof(*$$));

          if ($$ == NULL)
              YYABORT;

          $$->type = USSR_EXPR_BINARY;
          $$->data.binary.left = $1;
          $$->data.binary.operator = USSR_OP_NE;
          $$->data.binary.right = $3;
      }
    ;

additive_expression
    : multiplicative_expression
      {
          $$ = $1;
      }
    | additive_expression PLUS multiplicative_expression
      {
          $$ = malloc(sizeof(*$$));

          if ($$ == NULL)
              YYABORT;

          $$->type = USSR_EXPR_BINARY;
          $$->data.binary.left = $1;
          $$->data.binary.operator = USSR_OP_ADD;
          $$->data.binary.right = $3;
      }
    | additive_expression MINUS multiplicative_expression
      {
          $$ = malloc(sizeof(*$$));

          if ($$ == NULL)
              YYABORT;

          $$->type = USSR_EXPR_BINARY;
          $$->data.binary.left = $1;
          $$->data.binary.operator = USSR_OP_SUB;
          $$->data.binary.right = $3;
      }
    ;

multiplicative_expression
    : unary_expression
      {
          $$ = $1;
      }
    | multiplicative_expression MULTIPLY unary_expression
      {
          $$ = malloc(sizeof(*$$));

          if ($$ == NULL)
              YYABORT;

          $$->type = USSR_EXPR_BINARY;
          $$->data.binary.left = $1;
          $$->data.binary.operator = USSR_OP_MUL;
          $$->data.binary.right = $3;
      }
    | multiplicative_expression DIVIDE unary_expression
      {
          $$ = malloc(sizeof(*$$));

          if ($$ == NULL)
              YYABORT;

          $$->type = USSR_EXPR_BINARY;
          $$->data.binary.left = $1;
          $$->data.binary.operator = USSR_OP_DIV;
          $$->data.binary.right = $3;
      }
    | multiplicative_expression MODULO unary_expression
      {
          $$ = malloc(sizeof(*$$));

          if ($$ == NULL)
              YYABORT;

          $$->type = USSR_EXPR_BINARY;
          $$->data.binary.left = $1;
          $$->data.binary.operator = USSR_OP_MOD;
          $$->data.binary.right = $3;
      }
    ;

unary_expression
    : primary_expression
      {
          $$ = $1;
      }
    | MINUS unary_expression
      {
          $$ = malloc(sizeof(*$$));

          if ($$ == NULL)
              YYABORT;

          $$->type = USSR_EXPR_UNARY;
          $$->data.unary.operator = USSR_OP_SUB;
          $$->data.unary.operand = $2;
      }
    ;

primary_expression
    : literal
      {
          $$ = malloc(sizeof(*$$));

          if ($$ == NULL)
              YYABORT;

          $$->type = USSR_EXPR_VALUE;
          $$->data.value = $1;
      }
    | IDENTIFIER
      {
          $$ = malloc(sizeof(*$$));

          if ($$ == NULL)
          {
              free($1);
              YYABORT;
          }

          $$->type = USSR_EXPR_VARIABLE;
          $$->data.variable = $1;
      }
    | LPAREN comparison_expression RPAREN
      {
          $$ = $2;
      }
    ;

%%
