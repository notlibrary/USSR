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

extern int yylineno;

void yyerror(const char *message)
{
    fprintf(stderr, "USSR: %s at line %d\n", message, yylineno);
}

extern FILE *yyin;

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


ussr_expression_t *ussr_make_binary_expression(
    ussr_expression_t *left,
    ussr_operator_t operator,
    ussr_expression_t *right
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
%token <string> UNO_LITERAL
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
%token QUESTION
%token EXCLAMATION
%token LOGICAL_AND
%token LOGICAL_OR
%token SHIFT_LEFT
%token SHIFT_RIGHT
%token BITWISE_XOR
%token BITWISE_AND
%token BITWISE_OR

%token PLUS
%token MINUS
%token MULTIPLY
%token DIVIDE
%token MODULO

%token NEWLINE
%token COMMENT

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
%type <expression> logical_or_expression
%type <expression> logical_and_expression
%type <expression> bitwise_or_expression
%type <expression> bitwise_xor_expression
%type <expression> bitwise_and_expression
%type <expression> shift_expression
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
    | IDENTIFIER LPAREN IDENTIFIER RPAREN
      {
          ussr_argument_t *items;

          /*
           * `command(out)` is source-level shorthand for
           * `command(out): ""`.  Keeping one empty-string
           * argument preserves the existing command ABI.
           */
          items = malloc(sizeof(*items));

          if (items == NULL)
          {
              free($1);
              free($3);
              YYABORT;
          }

          items[0].type = USSR_ARGUMENT_VALUE;
          items[0].assignment = 0;
          items[0].data.value.type = USSR_STRING;
          items[0].data.value.data.string = malloc(1);

          if (items[0].data.value.data.string == NULL)
          {
              free(items);
              free($1);
              free($3);
              YYABORT;
          }

          items[0].data.value.data.string[0] = '\0';

          $$ = ussr_command_create(
              $1,
              $3,
              items,
              1
          );

          if ($$ == NULL)
          {
              free(items[0].data.value.data.string);
              free(items);
              free($1);
              free($3);
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

argument_base
    : literal
      {
          $$.type = USSR_ARGUMENT_VALUE;
          $$.assignment = 0;
          $$.data.value = $1;
      }
    | UNO_LITERAL
      {
          $$.type = USSR_ARGUMENT_UNO_LITERAL;
          $$.assignment = 0;
          $$.data.uno_text = $1;
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
    | expression
      {
          $$.type = USSR_ARGUMENT_EXPRESSION;
          $$.assignment = 0;
          $$.data.expression = $1;
      }
    | block_argument
      {
          $$ = $1;
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
    : logical_or_expression
      { $$ = $1; }
    ;

logical_or_expression
    : logical_and_expression
      { $$ = $1; }
    | logical_or_expression LOGICAL_OR logical_and_expression
      { $$ = ussr_make_binary_expression($1, USSR_OP_LOGICAL_OR, $3); }
    ;

logical_and_expression
    : bitwise_or_expression
      { $$ = $1; }
    | logical_and_expression LOGICAL_AND bitwise_or_expression
      { $$ = ussr_make_binary_expression($1, USSR_OP_LOGICAL_AND, $3); }
    ;

bitwise_or_expression
    : bitwise_xor_expression
      { $$ = $1; }
    | bitwise_or_expression BITWISE_OR bitwise_xor_expression
      { $$ = ussr_make_binary_expression($1, USSR_OP_BITWISE_OR, $3); }
    ;

bitwise_xor_expression
    : bitwise_and_expression
      { $$ = $1; }
    | bitwise_xor_expression BITWISE_XOR bitwise_and_expression
      { $$ = ussr_make_binary_expression($1, USSR_OP_BITWISE_XOR, $3); }
    ;

bitwise_and_expression
    : shift_expression
      { $$ = $1; }
    | bitwise_and_expression BITWISE_AND shift_expression
      { $$ = ussr_make_binary_expression($1, USSR_OP_BITWISE_AND, $3); }
    ;

shift_expression
    : additive_expression
      { $$ = $1; }
    | shift_expression SHIFT_LEFT additive_expression
      { $$ = ussr_make_binary_expression($1, USSR_OP_SHIFT_LEFT, $3); }
    | shift_expression SHIFT_RIGHT additive_expression
      { $$ = ussr_make_binary_expression($1, USSR_OP_SHIFT_RIGHT, $3); }
    ;

additive_expression
    : multiplicative_expression
      { $$ = $1; }
    | additive_expression PLUS multiplicative_expression
      { $$ = ussr_make_binary_expression($1, USSR_OP_ADD, $3); }
    | additive_expression MINUS multiplicative_expression
      { $$ = ussr_make_binary_expression($1, USSR_OP_SUB, $3); }
    ;

multiplicative_expression
    : unary_expression
      { $$ = $1; }
    | multiplicative_expression MULTIPLY unary_expression
      { $$ = ussr_make_binary_expression($1, USSR_OP_MUL, $3); }
    | multiplicative_expression DIVIDE unary_expression
      { $$ = ussr_make_binary_expression($1, USSR_OP_DIV, $3); }
    | multiplicative_expression MODULO unary_expression
      { $$ = ussr_make_binary_expression($1, USSR_OP_MOD, $3); }
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
