%code requires
{
#include <stddef.h>
#include "ussr.h"
}

%{
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ussr.h"

int yylex(void);
extern int yylineno;
extern FILE *yyin;

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

ussr_expression_t *ussr_make_binary_expression(
    ussr_expression_t *left,
    ussr_operator_t operator,
    ussr_expression_t *right
);

static int ussr_parser_block_allowed(const char *name)
{
    if (name == NULL)
        return 0;

    if (strcmp(name, "if") == 0 ||
        strcmp(name, "while") == 0 ||
        strcmp(name, "method") == 0 ||
        strcmp(name, "each") == 0 ||
        strcmp(name, "map") == 0 ||
        strcmp(name, "filter") == 0)
        return 1;

    /* An otherwise unknown command with a trailing block is a
     * user-defined function declaration. */
    if (strcmp(name, "break") == 0 ||
        strcmp(name, "continue") == 0 ||
        strcmp(name, "return") == 0 ||
        strcmp(name, "set") == 0 ||
        strcmp(name, "print") == 0 ||
        strcmp(name, "add") == 0 ||
        strcmp(name, "sub") == 0 ||
        strcmp(name, "mul") == 0 ||
        strcmp(name, "div") == 0 ||
        strcmp(name, "mod") == 0 ||
        strcmp(name, "concat") == 0 ||
        strcmp(name, "eval") == 0 ||
        strcmp(name, "get") == 0 ||
        strcmp(name, "random64") == 0 ||
        strcmp(name, "seed_random64") == 0 ||
        strcmp(name, "scan") == 0 ||
        strcmp(name, "time") == 0 ||
        strcmp(name, "cd") == 0 ||
        strcmp(name, "chain") == 0 ||
        strcmp(name, "file") == 0)
        return 0;

    return 1;
}

static int ussr_parser_block_count(ussr_argument_t *items, size_t count)
{
    size_t i;
    int blocks = 0;
    for (i = 0; i < count; ++i)
        if (items[i].type == USSR_ARGUMENT_COMMAND_LIST)
            ++blocks;
    return blocks;
}

static int ussr_parser_blocks_are_trailing(ussr_argument_t *items, size_t count)
{
    size_t i;
    if (count == 0)
        return 1;
    for (i = 0; i + 1 < count; ++i)
        if (items[i].type == USSR_ARGUMENT_COMMAND_LIST)
            return 0;
    return 1;
}
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
%token LOGICAL_AND
%token LOGICAL_OR
%token SHIFT_LEFT
%token SHIFT_RIGHT
%token BITWISE_XOR
%token BITWISE_AND
%token BITWISE_OR

%token NEWLINE
%token QUESTION
%token EXCLAMATION

%token <string> UNO_LITERAL

%type <command_list> program command_lines
%type <command> command
%type <arguments> command_tail
%type <arguments> argument_list
%type <argument> argument argument_base block_argument
%type <command> command_line
%type <value> literal
%type <expression> expression logical_or_expression logical_and_expression
%type <expression> bitwise_or_expression bitwise_xor_expression bitwise_and_expression
%type <expression> comparison_expression shift_expression additive_expression
%type <expression> multiplicative_expression unary_expression primary_expression

%%

program
    : command_lines trailing_newlines
      {
          ussr_parsed_program = $1;
          $$ = $1;
      }
    ;

trailing_newlines
    : %empty
    | trailing_newlines NEWLINE
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
    | command NEWLINE
      {
          $$ = $1;
      }
    | command
      {
          $$ = $1;
      }
    ;

command
    : IDENTIFIER LPAREN IDENTIFIER RPAREN command_tail
      {
          $$ = ussr_command_create(
              $1,
              $3,
              $5.items,
              $5.count
          );

          if ($$ == NULL)
          {
              free($1);
              free($3);
              free($5.items);
              YYABORT;
          }

          if (!ussr_parser_blocks_are_trailing($5.items, $5.count))
          {
              fprintf(stderr,
                      "USSR: [] blocks must be trailing command blocks\n");
              free($1);
              free($3);
              free($5.items);
              free($$);
              YYABORT;
          }

          if (ussr_parser_block_count($5.items, $5.count) != 0 &&
              !ussr_parser_block_allowed($1))
          {
              fprintf(stderr,
                      "USSR: command '%s' cannot have a [] block\n",
                      $1);
              free($1);
              free($3);
              free($5.items);
              free($$);
              YYABORT;
          }
      }
    ;

command_tail
    : %empty
      {
          $$.items = NULL;
          $$.count = 0;
      }
    | COLON
      {
          $$.items = NULL;
          $$.count = 0;
      }
    | COLON argument_list
      {
          $$ = $2;
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
    | UNO_LITERAL
      {
          /*
           * Deferred, like block_argument: the struct type(s) this
           * text names may not be registered until execution time
           * (see ussr.h's ussr_argument_t.data.uno_text), so we can't
           * resolve it to a real ussr_value_t here at parse time.
           */
          $$.type = USSR_ARGUMENT_UNO_LITERAL;
          $$.assignment = 0;
          $$.data.uno_text = $1;
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
    : LBRACE logical_or_expression RBRACE
      {
          $$ = $2;
      }
    ;

logical_or_expression
    : logical_and_expression
      {
          $$ = $1;
      }
    | logical_or_expression LOGICAL_OR logical_and_expression
      {
          $$ = ussr_make_binary_expression(
              $1, USSR_OP_LOGICAL_OR, $3
          );
          if ($$ == NULL)
              YYABORT;
      }
    ;

logical_and_expression
    : bitwise_or_expression
      {
          $$ = $1;
      }
    | logical_and_expression LOGICAL_AND bitwise_or_expression
      {
          $$ = ussr_make_binary_expression(
              $1, USSR_OP_LOGICAL_AND, $3
          );
          if ($$ == NULL)
              YYABORT;
      }
    ;

bitwise_or_expression
    : bitwise_xor_expression
      {
          $$ = $1;
      }
    | bitwise_or_expression BITWISE_OR bitwise_xor_expression
      {
          $$ = ussr_make_binary_expression(
              $1, USSR_OP_BITWISE_OR, $3
          );
          if ($$ == NULL)
              YYABORT;
      }
    ;

bitwise_xor_expression
    : bitwise_and_expression
      {
          $$ = $1;
      }
    | bitwise_xor_expression BITWISE_XOR bitwise_and_expression
      {
          $$ = ussr_make_binary_expression(
              $1, USSR_OP_BITWISE_XOR, $3
          );
          if ($$ == NULL)
              YYABORT;
      }
    ;

bitwise_and_expression
    : comparison_expression
      {
          $$ = $1;
      }
    | bitwise_and_expression BITWISE_AND comparison_expression
      {
          $$ = ussr_make_binary_expression(
              $1, USSR_OP_BITWISE_AND, $3
          );
          if ($$ == NULL)
              YYABORT;
      }
    ;

comparison_expression
    : shift_expression
      {
          $$ = $1;
      }
    | shift_expression GREATER shift_expression
      {
          $$ = ussr_make_binary_expression(
              $1, USSR_OP_GT, $3
          );
          if ($$ == NULL)
              YYABORT;
      }
    | shift_expression LESS shift_expression
      {
          $$ = ussr_make_binary_expression(
              $1, USSR_OP_LT, $3
          );
          if ($$ == NULL)
              YYABORT;
      }
    | shift_expression GREATER_EQUAL shift_expression
      {
          $$ = ussr_make_binary_expression(
              $1, USSR_OP_GE, $3
          );
          if ($$ == NULL)
              YYABORT;
      }
    | shift_expression LESS_EQUAL shift_expression
      {
          $$ = ussr_make_binary_expression(
              $1, USSR_OP_LE, $3
          );
          if ($$ == NULL)
              YYABORT;
      }
    | shift_expression EQUAL shift_expression
      {
          $$ = ussr_make_binary_expression(
              $1, USSR_OP_EQ, $3
          );
          if ($$ == NULL)
              YYABORT;
      }
    | shift_expression NOT_EQUAL shift_expression
      {
          $$ = ussr_make_binary_expression(
              $1, USSR_OP_NE, $3
          );
          if ($$ == NULL)
              YYABORT;
      }
    ;

shift_expression
    : additive_expression
      {
          $$ = $1;
      }
    | shift_expression SHIFT_LEFT additive_expression
      {
          $$ = ussr_make_binary_expression(
              $1, USSR_OP_SHIFT_LEFT, $3
          );
          if ($$ == NULL)
              YYABORT;
      }
    | shift_expression SHIFT_RIGHT additive_expression
      {
          $$ = ussr_make_binary_expression(
              $1, USSR_OP_SHIFT_RIGHT, $3
          );
          if ($$ == NULL)
              YYABORT;
      }
    ;

additive_expression
    : multiplicative_expression
      {
          $$ = $1;
      }
    | additive_expression PLUS multiplicative_expression
      {
          $$ = ussr_make_binary_expression(
              $1, USSR_OP_ADD, $3
          );
          if ($$ == NULL)
              YYABORT;
      }
    | additive_expression MINUS multiplicative_expression
      {
          $$ = ussr_make_binary_expression(
              $1, USSR_OP_SUB, $3
          );
          if ($$ == NULL)
              YYABORT;
      }
    ;

multiplicative_expression
    : unary_expression
      {
          $$ = $1;
      }
    | multiplicative_expression MULTIPLY unary_expression
      {
          $$ = ussr_make_binary_expression(
              $1, USSR_OP_MUL, $3
          );
          if ($$ == NULL)
              YYABORT;
      }
    | multiplicative_expression DIVIDE unary_expression
      {
          $$ = ussr_make_binary_expression(
              $1, USSR_OP_DIV, $3
          );
          if ($$ == NULL)
              YYABORT;
      }
    | multiplicative_expression MODULO unary_expression
      {
          $$ = ussr_make_binary_expression(
              $1, USSR_OP_MOD, $3
          );
          if ($$ == NULL)
              YYABORT;
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
    | LPAREN logical_or_expression RPAREN
      {
          $$ = $2;
      }
    ;

%%