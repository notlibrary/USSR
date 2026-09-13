/* A Bison parser, made by GNU Bison 3.7.5.  */

/* Bison interface for Yacc-like parsers in C

   Copyright (C) 1984, 1989-1990, 2000-2015, 2018-2021 Free Software Foundation,
   Inc.

   This program is free software: you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation, either version 3 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program.  If not, see <http://www.gnu.org/licenses/>.  */

/* As a special exception, you may create a larger work that contains
   part or all of the Bison parser skeleton and distribute that work
   under terms of your choice, so long as that work isn't itself a
   parser generator using the skeleton or a modified version thereof
   as a parser skeleton.  Alternatively, if you modify or redistribute
   the parser skeleton itself, you may (at your option) remove this
   special exception, which will cause the skeleton and the resulting
   Bison output files to be licensed under the GNU General Public
   License without this special exception.

   This special exception was added by the Free Software Foundation in
   version 2.2 of Bison.  */

/* DO NOT RELY ON FEATURES THAT ARE NOT DOCUMENTED in the manual,
   especially those whose name start with YY_ or yy_.  They are
   private implementation details that can be changed or removed.  */

#ifndef YY_YY_PARSER_TAB_H_INCLUDED
# define YY_YY_PARSER_TAB_H_INCLUDED
/* Debug traces.  */
#ifndef YYDEBUG
# define YYDEBUG 0
#endif
#if YYDEBUG
extern int yydebug;
#endif
/* "%code requires" blocks.  */
#line 2 "parser.y"

#include <stddef.h>
#include "ussr.h"

#line 54 "parser.tab.h"

/* Token kinds.  */
#ifndef YYTOKENTYPE
# define YYTOKENTYPE
  enum yytokentype
  {
    YYEMPTY = -2,
    YYEOF = 0,                     /* "end of file"  */
    YYerror = 256,                 /* error  */
    YYUNDEF = 257,                 /* "invalid token"  */
    IDENTIFIER = 258,              /* IDENTIFIER  */
    STRING = 259,                  /* STRING  */
    INTEGER = 260,                 /* INTEGER  */
    REAL = 261,                    /* REAL  */
    BOOLEAN = 262,                 /* BOOLEAN  */
    NULL_VALUE = 263,              /* NULL_VALUE  */
    LBRACKET = 264,                /* LBRACKET  */
    RBRACKET = 265,                /* RBRACKET  */
    LBRACE = 266,                  /* LBRACE  */
    RBRACE = 267,                  /* RBRACE  */
    COLON = 268,                   /* COLON  */
    LPAREN = 269,                  /* LPAREN  */
    RPAREN = 270,                  /* RPAREN  */
    GREATER = 271,                 /* GREATER  */
    LESS = 272,                    /* LESS  */
    GREATER_EQUAL = 273,           /* GREATER_EQUAL  */
    LESS_EQUAL = 274,              /* LESS_EQUAL  */
    EQUAL = 275,                   /* EQUAL  */
    NOT_EQUAL = 276,               /* NOT_EQUAL  */
    PLUS = 277,                    /* PLUS  */
    MINUS = 278,                   /* MINUS  */
    MULTIPLY = 279,                /* MULTIPLY  */
    DIVIDE = 280,                  /* DIVIDE  */
    MODULO = 281,                  /* MODULO  */
    LOGICAL_AND = 282,             /* LOGICAL_AND  */
    LOGICAL_OR = 283,              /* LOGICAL_OR  */
    SHIFT_LEFT = 284,              /* SHIFT_LEFT  */
    SHIFT_RIGHT = 285,             /* SHIFT_RIGHT  */
    BITWISE_XOR = 286,             /* BITWISE_XOR  */
    BITWISE_AND = 287,             /* BITWISE_AND  */
    BITWISE_OR = 288,              /* BITWISE_OR  */
    NEWLINE = 289,                 /* NEWLINE  */
    QUESTION = 290,                /* QUESTION  */
    EXCLAMATION = 291,             /* EXCLAMATION  */
    UNO_LITERAL = 292              /* UNO_LITERAL  */
  };
  typedef enum yytokentype yytoken_kind_t;
#endif

/* Value type.  */
#if ! defined YYSTYPE && ! defined YYSTYPE_IS_DECLARED
union YYSTYPE
{
#line 46 "parser.y"

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

#line 128 "parser.tab.h"

};
typedef union YYSTYPE YYSTYPE;
# define YYSTYPE_IS_TRIVIAL 1
# define YYSTYPE_IS_DECLARED 1
#endif


extern YYSTYPE yylval;

int yyparse (void);

#endif /* !YY_YY_PARSER_TAB_H_INCLUDED  */
