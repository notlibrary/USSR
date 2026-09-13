/* A Bison parser, made by GNU Bison 3.7.5.  */

/* Bison implementation for Yacc-like parsers in C

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

/* C LALR(1) parser skeleton written by Richard Stallman, by
   simplifying the original so-called "semantic" parser.  */

/* DO NOT RELY ON FEATURES THAT ARE NOT DOCUMENTED in the manual,
   especially those whose name start with YY_ or yy_.  They are
   private implementation details that can be changed or removed.  */

/* All symbols defined below should begin with yy or YY, to avoid
   infringing on user name space.  This should be done even for local
   variables, as they might otherwise be expanded by user macros.
   There are some unavoidable exceptions within include files to
   define necessary library symbols; they are noted "INFRINGES ON
   USER NAME SPACE" below.  */

/* Identify Bison output, and Bison version.  */
#define YYBISON 30705

/* Bison version string.  */
#define YYBISON_VERSION "3.7.5"

/* Skeleton name.  */
#define YYSKELETON_NAME "yacc.c"

/* Pure parsers.  */
#define YYPURE 0

/* Push parsers.  */
#define YYPUSH 0

/* Pull parsers.  */
#define YYPULL 1




/* First part of user prologue.  */
#line 7 "parser.y"

#include <stdio.h>
#include <stdlib.h>

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

#line 109 "parser.tab.c"

# ifndef YY_CAST
#  ifdef __cplusplus
#   define YY_CAST(Type, Val) static_cast<Type> (Val)
#   define YY_REINTERPRET_CAST(Type, Val) reinterpret_cast<Type> (Val)
#  else
#   define YY_CAST(Type, Val) ((Type) (Val))
#   define YY_REINTERPRET_CAST(Type, Val) ((Type) (Val))
#  endif
# endif
# ifndef YY_NULLPTR
#  if defined __cplusplus
#   if 201103L <= __cplusplus
#    define YY_NULLPTR nullptr
#   else
#    define YY_NULLPTR 0
#   endif
#  else
#   define YY_NULLPTR ((void*)0)
#  endif
# endif

#include "parser.tab.h"
/* Symbol kind.  */
enum yysymbol_kind_t
{
  YYSYMBOL_YYEMPTY = -2,
  YYSYMBOL_YYEOF = 0,                      /* "end of file"  */
  YYSYMBOL_YYerror = 1,                    /* error  */
  YYSYMBOL_YYUNDEF = 2,                    /* "invalid token"  */
  YYSYMBOL_IDENTIFIER = 3,                 /* IDENTIFIER  */
  YYSYMBOL_STRING = 4,                     /* STRING  */
  YYSYMBOL_INTEGER = 5,                    /* INTEGER  */
  YYSYMBOL_REAL = 6,                       /* REAL  */
  YYSYMBOL_BOOLEAN = 7,                    /* BOOLEAN  */
  YYSYMBOL_NULL_VALUE = 8,                 /* NULL_VALUE  */
  YYSYMBOL_LBRACKET = 9,                   /* LBRACKET  */
  YYSYMBOL_RBRACKET = 10,                  /* RBRACKET  */
  YYSYMBOL_LBRACE = 11,                    /* LBRACE  */
  YYSYMBOL_RBRACE = 12,                    /* RBRACE  */
  YYSYMBOL_COLON = 13,                     /* COLON  */
  YYSYMBOL_LPAREN = 14,                    /* LPAREN  */
  YYSYMBOL_RPAREN = 15,                    /* RPAREN  */
  YYSYMBOL_GREATER = 16,                   /* GREATER  */
  YYSYMBOL_LESS = 17,                      /* LESS  */
  YYSYMBOL_GREATER_EQUAL = 18,             /* GREATER_EQUAL  */
  YYSYMBOL_LESS_EQUAL = 19,                /* LESS_EQUAL  */
  YYSYMBOL_EQUAL = 20,                     /* EQUAL  */
  YYSYMBOL_NOT_EQUAL = 21,                 /* NOT_EQUAL  */
  YYSYMBOL_PLUS = 22,                      /* PLUS  */
  YYSYMBOL_MINUS = 23,                     /* MINUS  */
  YYSYMBOL_MULTIPLY = 24,                  /* MULTIPLY  */
  YYSYMBOL_DIVIDE = 25,                    /* DIVIDE  */
  YYSYMBOL_MODULO = 26,                    /* MODULO  */
  YYSYMBOL_LOGICAL_AND = 27,               /* LOGICAL_AND  */
  YYSYMBOL_LOGICAL_OR = 28,                /* LOGICAL_OR  */
  YYSYMBOL_SHIFT_LEFT = 29,                /* SHIFT_LEFT  */
  YYSYMBOL_SHIFT_RIGHT = 30,               /* SHIFT_RIGHT  */
  YYSYMBOL_BITWISE_XOR = 31,               /* BITWISE_XOR  */
  YYSYMBOL_BITWISE_AND = 32,               /* BITWISE_AND  */
  YYSYMBOL_BITWISE_OR = 33,                /* BITWISE_OR  */
  YYSYMBOL_NEWLINE = 34,                   /* NEWLINE  */
  YYSYMBOL_QUESTION = 35,                  /* QUESTION  */
  YYSYMBOL_EXCLAMATION = 36,               /* EXCLAMATION  */
  YYSYMBOL_UNO_LITERAL = 37,               /* UNO_LITERAL  */
  YYSYMBOL_YYACCEPT = 38,                  /* $accept  */
  YYSYMBOL_program = 39,                   /* program  */
  YYSYMBOL_command_lines = 40,             /* command_lines  */
  YYSYMBOL_command_line = 41,              /* command_line  */
  YYSYMBOL_command = 42,                   /* command  */
  YYSYMBOL_argument_list = 43,             /* argument_list  */
  YYSYMBOL_argument = 44,                  /* argument  */
  YYSYMBOL_argument_base = 45,             /* argument_base  */
  YYSYMBOL_block_argument = 46,            /* block_argument  */
  YYSYMBOL_literal = 47,                   /* literal  */
  YYSYMBOL_expression = 48,                /* expression  */
  YYSYMBOL_logical_or_expression = 49,     /* logical_or_expression  */
  YYSYMBOL_logical_and_expression = 50,    /* logical_and_expression  */
  YYSYMBOL_bitwise_or_expression = 51,     /* bitwise_or_expression  */
  YYSYMBOL_bitwise_xor_expression = 52,    /* bitwise_xor_expression  */
  YYSYMBOL_bitwise_and_expression = 53,    /* bitwise_and_expression  */
  YYSYMBOL_comparison_expression = 54,     /* comparison_expression  */
  YYSYMBOL_shift_expression = 55,          /* shift_expression  */
  YYSYMBOL_additive_expression = 56,       /* additive_expression  */
  YYSYMBOL_multiplicative_expression = 57, /* multiplicative_expression  */
  YYSYMBOL_unary_expression = 58,          /* unary_expression  */
  YYSYMBOL_primary_expression = 59         /* primary_expression  */
};
typedef enum yysymbol_kind_t yysymbol_kind_t;




#ifdef short
# undef short
#endif

/* On compilers that do not define __PTRDIFF_MAX__ etc., make sure
   <limits.h> and (if available) <stdint.h> are included
   so that the code can choose integer types of a good width.  */

#ifndef __PTRDIFF_MAX__
# include <limits.h> /* INFRINGES ON USER NAME SPACE */
# if defined __STDC_VERSION__ && 199901 <= __STDC_VERSION__
#  include <stdint.h> /* INFRINGES ON USER NAME SPACE */
#  define YY_STDINT_H
# endif
#endif

/* Narrow types that promote to a signed type and that can represent a
   signed or unsigned integer of at least N bits.  In tables they can
   save space and decrease cache pressure.  Promoting to a signed type
   helps avoid bugs in integer arithmetic.  */

#ifdef __INT_LEAST8_MAX__
typedef __INT_LEAST8_TYPE__ yytype_int8;
#elif defined YY_STDINT_H
typedef int_least8_t yytype_int8;
#else
typedef signed char yytype_int8;
#endif

#ifdef __INT_LEAST16_MAX__
typedef __INT_LEAST16_TYPE__ yytype_int16;
#elif defined YY_STDINT_H
typedef int_least16_t yytype_int16;
#else
typedef short yytype_int16;
#endif

/* Work around bug in HP-UX 11.23, which defines these macros
   incorrectly for preprocessor constants.  This workaround can likely
   be removed in 2023, as HPE has promised support for HP-UX 11.23
   (aka HP-UX 11i v2) only through the end of 2022; see Table 2 of
   <https://h20195.www2.hpe.com/V2/getpdf.aspx/4AA4-7673ENW.pdf>.  */
#ifdef __hpux
# undef UINT_LEAST8_MAX
# undef UINT_LEAST16_MAX
# define UINT_LEAST8_MAX 255
# define UINT_LEAST16_MAX 65535
#endif

#if defined __UINT_LEAST8_MAX__ && __UINT_LEAST8_MAX__ <= __INT_MAX__
typedef __UINT_LEAST8_TYPE__ yytype_uint8;
#elif (!defined __UINT_LEAST8_MAX__ && defined YY_STDINT_H \
       && UINT_LEAST8_MAX <= INT_MAX)
typedef uint_least8_t yytype_uint8;
#elif !defined __UINT_LEAST8_MAX__ && UCHAR_MAX <= INT_MAX
typedef unsigned char yytype_uint8;
#else
typedef short yytype_uint8;
#endif

#if defined __UINT_LEAST16_MAX__ && __UINT_LEAST16_MAX__ <= __INT_MAX__
typedef __UINT_LEAST16_TYPE__ yytype_uint16;
#elif (!defined __UINT_LEAST16_MAX__ && defined YY_STDINT_H \
       && UINT_LEAST16_MAX <= INT_MAX)
typedef uint_least16_t yytype_uint16;
#elif !defined __UINT_LEAST16_MAX__ && USHRT_MAX <= INT_MAX
typedef unsigned short yytype_uint16;
#else
typedef int yytype_uint16;
#endif

#ifndef YYPTRDIFF_T
# if defined __PTRDIFF_TYPE__ && defined __PTRDIFF_MAX__
#  define YYPTRDIFF_T __PTRDIFF_TYPE__
#  define YYPTRDIFF_MAXIMUM __PTRDIFF_MAX__
# elif defined PTRDIFF_MAX
#  ifndef ptrdiff_t
#   include <stddef.h> /* INFRINGES ON USER NAME SPACE */
#  endif
#  define YYPTRDIFF_T ptrdiff_t
#  define YYPTRDIFF_MAXIMUM PTRDIFF_MAX
# else
#  define YYPTRDIFF_T long
#  define YYPTRDIFF_MAXIMUM LONG_MAX
# endif
#endif

#ifndef YYSIZE_T
# ifdef __SIZE_TYPE__
#  define YYSIZE_T __SIZE_TYPE__
# elif defined size_t
#  define YYSIZE_T size_t
# elif defined __STDC_VERSION__ && 199901 <= __STDC_VERSION__
#  include <stddef.h> /* INFRINGES ON USER NAME SPACE */
#  define YYSIZE_T size_t
# else
#  define YYSIZE_T unsigned
# endif
#endif

#define YYSIZE_MAXIMUM                                  \
  YY_CAST (YYPTRDIFF_T,                                 \
           (YYPTRDIFF_MAXIMUM < YY_CAST (YYSIZE_T, -1)  \
            ? YYPTRDIFF_MAXIMUM                         \
            : YY_CAST (YYSIZE_T, -1)))

#define YYSIZEOF(X) YY_CAST (YYPTRDIFF_T, sizeof (X))


/* Stored state numbers (used for stacks). */
typedef yytype_int8 yy_state_t;

/* State numbers in computations.  */
typedef int yy_state_fast_t;

#ifndef YY_
# if defined YYENABLE_NLS && YYENABLE_NLS
#  if ENABLE_NLS
#   include <libintl.h> /* INFRINGES ON USER NAME SPACE */
#   define YY_(Msgid) dgettext ("bison-runtime", Msgid)
#  endif
# endif
# ifndef YY_
#  define YY_(Msgid) Msgid
# endif
#endif


#ifndef YY_ATTRIBUTE_PURE
# if defined __GNUC__ && 2 < __GNUC__ + (96 <= __GNUC_MINOR__)
#  define YY_ATTRIBUTE_PURE __attribute__ ((__pure__))
# else
#  define YY_ATTRIBUTE_PURE
# endif
#endif

#ifndef YY_ATTRIBUTE_UNUSED
# if defined __GNUC__ && 2 < __GNUC__ + (7 <= __GNUC_MINOR__)
#  define YY_ATTRIBUTE_UNUSED __attribute__ ((__unused__))
# else
#  define YY_ATTRIBUTE_UNUSED
# endif
#endif

/* Suppress unused-variable warnings by "using" E.  */
#if ! defined lint || defined __GNUC__
# define YY_USE(E) ((void) (E))
#else
# define YY_USE(E) /* empty */
#endif

#if defined __GNUC__ && ! defined __ICC && 407 <= __GNUC__ * 100 + __GNUC_MINOR__
/* Suppress an incorrect diagnostic about yylval being uninitialized.  */
# define YY_IGNORE_MAYBE_UNINITIALIZED_BEGIN                            \
    _Pragma ("GCC diagnostic push")                                     \
    _Pragma ("GCC diagnostic ignored \"-Wuninitialized\"")              \
    _Pragma ("GCC diagnostic ignored \"-Wmaybe-uninitialized\"")
# define YY_IGNORE_MAYBE_UNINITIALIZED_END      \
    _Pragma ("GCC diagnostic pop")
#else
# define YY_INITIAL_VALUE(Value) Value
#endif
#ifndef YY_IGNORE_MAYBE_UNINITIALIZED_BEGIN
# define YY_IGNORE_MAYBE_UNINITIALIZED_BEGIN
# define YY_IGNORE_MAYBE_UNINITIALIZED_END
#endif
#ifndef YY_INITIAL_VALUE
# define YY_INITIAL_VALUE(Value) /* Nothing. */
#endif

#if defined __cplusplus && defined __GNUC__ && ! defined __ICC && 6 <= __GNUC__
# define YY_IGNORE_USELESS_CAST_BEGIN                          \
    _Pragma ("GCC diagnostic push")                            \
    _Pragma ("GCC diagnostic ignored \"-Wuseless-cast\"")
# define YY_IGNORE_USELESS_CAST_END            \
    _Pragma ("GCC diagnostic pop")
#endif
#ifndef YY_IGNORE_USELESS_CAST_BEGIN
# define YY_IGNORE_USELESS_CAST_BEGIN
# define YY_IGNORE_USELESS_CAST_END
#endif


#define YY_ASSERT(E) ((void) (0 && (E)))

#if !defined yyoverflow

/* The parser invokes alloca or malloc; define the necessary symbols.  */

# ifdef YYSTACK_USE_ALLOCA
#  if YYSTACK_USE_ALLOCA
#   ifdef __GNUC__
#    define YYSTACK_ALLOC __builtin_alloca
#   elif defined __BUILTIN_VA_ARG_INCR
#    include <alloca.h> /* INFRINGES ON USER NAME SPACE */
#   elif defined _AIX
#    define YYSTACK_ALLOC __alloca
#   elif defined _MSC_VER
#    include <malloc.h> /* INFRINGES ON USER NAME SPACE */
#    define alloca _alloca
#   else
#    define YYSTACK_ALLOC alloca
#    if ! defined _ALLOCA_H && ! defined EXIT_SUCCESS
#     include <stdlib.h> /* INFRINGES ON USER NAME SPACE */
      /* Use EXIT_SUCCESS as a witness for stdlib.h.  */
#     ifndef EXIT_SUCCESS
#      define EXIT_SUCCESS 0
#     endif
#    endif
#   endif
#  endif
# endif

# ifdef YYSTACK_ALLOC
   /* Pacify GCC's 'empty if-body' warning.  */
#  define YYSTACK_FREE(Ptr) do { /* empty */; } while (0)
#  ifndef YYSTACK_ALLOC_MAXIMUM
    /* The OS might guarantee only one guard page at the bottom of the stack,
       and a page size can be as small as 4096 bytes.  So we cannot safely
       invoke alloca (N) if N exceeds 4096.  Use a slightly smaller number
       to allow for a few compiler-allocated temporary stack slots.  */
#   define YYSTACK_ALLOC_MAXIMUM 4032 /* reasonable circa 2006 */
#  endif
# else
#  define YYSTACK_ALLOC YYMALLOC
#  define YYSTACK_FREE YYFREE
#  ifndef YYSTACK_ALLOC_MAXIMUM
#   define YYSTACK_ALLOC_MAXIMUM YYSIZE_MAXIMUM
#  endif
#  if (defined __cplusplus && ! defined EXIT_SUCCESS \
       && ! ((defined YYMALLOC || defined malloc) \
             && (defined YYFREE || defined free)))
#   include <stdlib.h> /* INFRINGES ON USER NAME SPACE */
#   ifndef EXIT_SUCCESS
#    define EXIT_SUCCESS 0
#   endif
#  endif
#  ifndef YYMALLOC
#   define YYMALLOC malloc
#   if ! defined malloc && ! defined EXIT_SUCCESS
void *malloc (YYSIZE_T); /* INFRINGES ON USER NAME SPACE */
#   endif
#  endif
#  ifndef YYFREE
#   define YYFREE free
#   if ! defined free && ! defined EXIT_SUCCESS
void free (void *); /* INFRINGES ON USER NAME SPACE */
#   endif
#  endif
# endif
#endif /* !defined yyoverflow */

#if (! defined yyoverflow \
     && (! defined __cplusplus \
         || (defined YYSTYPE_IS_TRIVIAL && YYSTYPE_IS_TRIVIAL)))

/* A type that is properly aligned for any stack member.  */
union yyalloc
{
  yy_state_t yyss_alloc;
  YYSTYPE yyvs_alloc;
};

/* The size of the maximum gap between one aligned stack and the next.  */
# define YYSTACK_GAP_MAXIMUM (YYSIZEOF (union yyalloc) - 1)

/* The size of an array large to enough to hold all stacks, each with
   N elements.  */
# define YYSTACK_BYTES(N) \
     ((N) * (YYSIZEOF (yy_state_t) + YYSIZEOF (YYSTYPE)) \
      + YYSTACK_GAP_MAXIMUM)

# define YYCOPY_NEEDED 1

/* Relocate STACK from its old location to the new one.  The
   local variables YYSIZE and YYSTACKSIZE give the old and new number of
   elements in the stack, and YYPTR gives the new location of the
   stack.  Advance YYPTR to a properly aligned location for the next
   stack.  */
# define YYSTACK_RELOCATE(Stack_alloc, Stack)                           \
    do                                                                  \
      {                                                                 \
        YYPTRDIFF_T yynewbytes;                                         \
        YYCOPY (&yyptr->Stack_alloc, Stack, yysize);                    \
        Stack = &yyptr->Stack_alloc;                                    \
        yynewbytes = yystacksize * YYSIZEOF (*Stack) + YYSTACK_GAP_MAXIMUM; \
        yyptr += yynewbytes / YYSIZEOF (*yyptr);                        \
      }                                                                 \
    while (0)

#endif

#if defined YYCOPY_NEEDED && YYCOPY_NEEDED
/* Copy COUNT objects from SRC to DST.  The source and destination do
   not overlap.  */
# ifndef YYCOPY
#  if defined __GNUC__ && 1 < __GNUC__
#   define YYCOPY(Dst, Src, Count) \
      __builtin_memcpy (Dst, Src, YY_CAST (YYSIZE_T, (Count)) * sizeof (*(Src)))
#  else
#   define YYCOPY(Dst, Src, Count)              \
      do                                        \
        {                                       \
          YYPTRDIFF_T yyi;                      \
          for (yyi = 0; yyi < (Count); yyi++)   \
            (Dst)[yyi] = (Src)[yyi];            \
        }                                       \
      while (0)
#  endif
# endif
#endif /* !YYCOPY_NEEDED */

/* YYFINAL -- State number of the termination state.  */
#define YYFINAL  5
/* YYLAST -- Last index in YYTABLE.  */
#define YYLAST   86

/* YYNTOKENS -- Number of terminals.  */
#define YYNTOKENS  38
/* YYNNTS -- Number of nonterminals.  */
#define YYNNTS  22
/* YYNRULES -- Number of rules.  */
#define YYNRULES  58
/* YYNSTATES -- Number of states.  */
#define YYNSTATES  91

/* YYMAXUTOK -- Last valid token kind.  */
#define YYMAXUTOK   292


/* YYTRANSLATE(TOKEN-NUM) -- Symbol number corresponding to TOKEN-NUM
   as returned by yylex, with out-of-bounds checking.  */
#define YYTRANSLATE(YYX)                                \
  (0 <= (YYX) && (YYX) <= YYMAXUTOK                     \
   ? YY_CAST (yysymbol_kind_t, yytranslate[YYX])        \
   : YYSYMBOL_YYUNDEF)

/* YYTRANSLATE[TOKEN-NUM] -- Symbol number corresponding to TOKEN-NUM
   as returned by yylex.  */
static const yytype_int8 yytranslate[] =
{
       0,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     1,     2,     3,     4,
       5,     6,     7,     8,     9,    10,    11,    12,    13,    14,
      15,    16,    17,    18,    19,    20,    21,    22,    23,    24,
      25,    26,    27,    28,    29,    30,    31,    32,    33,    34,
      35,    36,    37
};

#if YYDEBUG
  /* YYRLINE[YYN] -- Source line where rule number YYN was defined.  */
static const yytype_int16 yyrline[] =
{
       0,   122,   122,   127,   135,   142,   158,   162,   166,   173,
     193,   205,   228,   232,   250,   258,   264,   282,   288,   292,
     307,   316,   321,   326,   331,   336,   343,   350,   354,   365,
     369,   380,   384,   395,   399,   410,   414,   425,   429,   437,
     445,   453,   461,   469,   480,   484,   492,   503,   507,   515,
     526,   530,   538,   546,   557,   561,   573,   581,   592
};
#endif

/** Accessing symbol of state STATE.  */
#define YY_ACCESSING_SYMBOL(State) YY_CAST (yysymbol_kind_t, yystos[State])

#if YYDEBUG || 0
/* The user-facing name of the symbol whose (internal) number is
   YYSYMBOL.  No bounds checking.  */
static const char *yysymbol_name (yysymbol_kind_t yysymbol) YY_ATTRIBUTE_UNUSED;

/* YYTNAME[SYMBOL-NUM] -- String name of the symbol SYMBOL-NUM.
   First, the terminals, then, starting at YYNTOKENS, nonterminals.  */
static const char *const yytname[] =
{
  "\"end of file\"", "error", "\"invalid token\"", "IDENTIFIER", "STRING",
  "INTEGER", "REAL", "BOOLEAN", "NULL_VALUE", "LBRACKET", "RBRACKET",
  "LBRACE", "RBRACE", "COLON", "LPAREN", "RPAREN", "GREATER", "LESS",
  "GREATER_EQUAL", "LESS_EQUAL", "EQUAL", "NOT_EQUAL", "PLUS", "MINUS",
  "MULTIPLY", "DIVIDE", "MODULO", "LOGICAL_AND", "LOGICAL_OR",
  "SHIFT_LEFT", "SHIFT_RIGHT", "BITWISE_XOR", "BITWISE_AND", "BITWISE_OR",
  "NEWLINE", "QUESTION", "EXCLAMATION", "UNO_LITERAL", "$accept",
  "program", "command_lines", "command_line", "command", "argument_list",
  "argument", "argument_base", "block_argument", "literal", "expression",
  "logical_or_expression", "logical_and_expression",
  "bitwise_or_expression", "bitwise_xor_expression",
  "bitwise_and_expression", "comparison_expression", "shift_expression",
  "additive_expression", "multiplicative_expression", "unary_expression",
  "primary_expression", YY_NULLPTR
};

static const char *
yysymbol_name (yysymbol_kind_t yysymbol)
{
  return yytname[yysymbol];
}
#endif

#ifdef YYPRINT
/* YYTOKNUM[NUM] -- (External) token number corresponding to the
   (internal) symbol number NUM (which must be that of a token).  */
static const yytype_int16 yytoknum[] =
{
       0,   256,   257,   258,   259,   260,   261,   262,   263,   264,
     265,   266,   267,   268,   269,   270,   271,   272,   273,   274,
     275,   276,   277,   278,   279,   280,   281,   282,   283,   284,
     285,   286,   287,   288,   289,   290,   291,   292
};
#endif

#define YYPACT_NINF (-40)

#define yypact_value_is_default(Yyn) \
  ((Yyn) == YYPACT_NINF)

#define YYTABLE_NINF (-1)

#define yytable_value_is_error(Yyn) \
  0

  /* YYPACT[STATE-NUM] -- Index in YYTABLE of the portion describing
     STATE-NUM.  */
static const yytype_int8 yypact[] =
{
       4,   -40,    29,    -2,     7,   -40,    19,   -40,   -40,     8,
     -40,    47,   -40,    36,    39,     0,    18,   -40,   -40,   -40,
     -40,   -40,   -40,    41,   -40,     0,   -40,    26,   -40,   -40,
     -40,   -40,     9,   -40,    41,    41,   -40,     2,    27,    30,
      34,    35,   -40,    40,    -7,    -4,   -40,   -40,   -40,   -40,
     -40,     3,   -40,   -40,    41,    41,    41,    41,    41,    41,
      41,    41,    41,    41,    41,    41,    41,    41,    41,    41,
      41,    41,   -40,    27,    30,    34,    35,   -40,    -5,    -5,
      -5,    -5,    -5,    -5,    -7,    -7,    -4,    -4,   -40,   -40,
     -40
};

  /* YYDEFACT[STATE-NUM] -- Default reduction number in state STATE-NUM.
     Performed when YYTABLE does not specify something else to do.  Zero
     means the default is an error.  */
static const yytype_int8 yydefact[] =
{
       4,     4,     0,     2,     0,     1,     0,     6,     5,     8,
       3,     0,     7,     0,     0,     0,    16,    21,    22,    23,
      24,    25,     4,     0,    19,     9,    10,    12,    18,    15,
      17,    13,     0,    57,     0,     0,    56,     0,    27,    29,
      31,    33,    35,    37,    44,    47,    50,    54,    11,    14,
      20,     0,    55,    26,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,    58,    28,    30,    32,    34,    36,    38,    39,
      40,    41,    42,    43,    45,    46,    48,    49,    51,    52,
      53
};

  /* YYPGOTO[NTERM-NUM].  */
static const yytype_int8 yypgoto[] =
{
     -40,   -40,     1,   -40,   -40,   -40,    43,   -40,   -40,    13,
     -40,    32,    23,    24,    22,    25,    28,    12,   -39,   -28,
     -35,   -40
};

  /* YYDEFGOTO[NTERM-NUM].  */
static const yytype_int8 yydefgoto[] =
{
       0,     2,     3,     8,     9,    25,    26,    27,    28,    36,
      30,    37,    38,    39,    40,    41,    42,    43,    44,    45,
      46,    47
};

  /* YYTABLE[YYPACT[STATE-NUM]] -- What to do in state STATE-NUM.  If
     positive, shift that token.  If negative, reduce the rule whose
     number is the opposite.  If YYTABLE_NINF, syntax error.  */
static const yytype_int8 yytable[] =
{
      52,     6,     4,    16,    17,    18,    19,    20,    21,    22,
       6,    23,     6,     1,    53,    67,    68,    10,    72,    50,
      69,    70,    71,    32,    65,    66,    84,    85,    29,     5,
      54,    54,     7,    11,    88,    89,    90,    24,    29,    86,
      87,     7,    12,     7,    33,    17,    18,    19,    20,    21,
      13,    14,    15,    31,    55,    34,    59,    60,    61,    62,
      63,    64,    49,    56,    35,    57,    51,    58,    48,    65,
      66,    78,    79,    80,    81,    82,    83,    73,    75,    74,
       0,     0,    76,     0,     0,     0,    77
};

static const yytype_int8 yycheck[] =
{
      35,     3,     1,     3,     4,     5,     6,     7,     8,     9,
       3,    11,     3,     9,    12,    22,    23,    10,    15,    10,
      24,    25,    26,    22,    29,    30,    65,    66,    15,     0,
      28,    28,    34,    14,    69,    70,    71,    37,    25,    67,
      68,    34,    34,    34,     3,     4,     5,     6,     7,     8,
       3,    15,    13,    35,    27,    14,    16,    17,    18,    19,
      20,    21,    36,    33,    23,    31,    34,    32,    25,    29,
      30,    59,    60,    61,    62,    63,    64,    54,    56,    55,
      -1,    -1,    57,    -1,    -1,    -1,    58
};

  /* YYSTOS[STATE-NUM] -- The (internal number of the) accessing
     symbol of state STATE-NUM.  */
static const yytype_int8 yystos[] =
{
       0,     9,    39,    40,    40,     0,     3,    34,    41,    42,
      10,    14,    34,     3,    15,    13,     3,     4,     5,     6,
       7,     8,     9,    11,    37,    43,    44,    45,    46,    47,
      48,    35,    40,     3,    14,    23,    47,    49,    50,    51,
      52,    53,    54,    55,    56,    57,    58,    59,    44,    36,
      10,    49,    58,    12,    28,    27,    33,    31,    32,    16,
      17,    18,    19,    20,    21,    29,    30,    22,    23,    24,
      25,    26,    15,    50,    51,    52,    53,    54,    55,    55,
      55,    55,    55,    55,    56,    56,    57,    57,    58,    58,
      58
};

  /* YYR1[YYN] -- Symbol number of symbol that rule YYN derives.  */
static const yytype_int8 yyr1[] =
{
       0,    38,    39,    39,    40,    40,    41,    41,    41,    42,
      43,    43,    44,    44,    44,    45,    45,    45,    45,    45,
      46,    47,    47,    47,    47,    47,    48,    49,    49,    50,
      50,    51,    51,    52,    52,    53,    53,    54,    54,    54,
      54,    54,    54,    54,    55,    55,    55,    56,    56,    56,
      57,    57,    57,    57,    58,    58,    59,    59,    59
};

  /* YYR2[YYN] -- Number of symbols on the right hand side of rule YYN.  */
static const yytype_int8 yyr2[] =
{
       0,     2,     1,     3,     0,     2,     1,     2,     1,     6,
       1,     2,     1,     2,     2,     1,     1,     1,     1,     1,
       3,     1,     1,     1,     1,     1,     3,     1,     3,     1,
       3,     1,     3,     1,     3,     1,     3,     1,     3,     3,
       3,     3,     3,     3,     1,     3,     3,     1,     3,     3,
       1,     3,     3,     3,     1,     2,     1,     1,     3
};


enum { YYENOMEM = -2 };

#define yyerrok         (yyerrstatus = 0)
#define yyclearin       (yychar = YYEMPTY)

#define YYACCEPT        goto yyacceptlab
#define YYABORT         goto yyabortlab
#define YYERROR         goto yyerrorlab


#define YYRECOVERING()  (!!yyerrstatus)

#define YYBACKUP(Token, Value)                                    \
  do                                                              \
    if (yychar == YYEMPTY)                                        \
      {                                                           \
        yychar = (Token);                                         \
        yylval = (Value);                                         \
        YYPOPSTACK (yylen);                                       \
        yystate = *yyssp;                                         \
        goto yybackup;                                            \
      }                                                           \
    else                                                          \
      {                                                           \
        yyerror (YY_("syntax error: cannot back up")); \
        YYERROR;                                                  \
      }                                                           \
  while (0)

/* Backward compatibility with an undocumented macro.
   Use YYerror or YYUNDEF. */
#define YYERRCODE YYUNDEF


/* Enable debugging if requested.  */
#if YYDEBUG

# ifndef YYFPRINTF
#  include <stdio.h> /* INFRINGES ON USER NAME SPACE */
#  define YYFPRINTF fprintf
# endif

# define YYDPRINTF(Args)                        \
do {                                            \
  if (yydebug)                                  \
    YYFPRINTF Args;                             \
} while (0)

/* This macro is provided for backward compatibility. */
# ifndef YY_LOCATION_PRINT
#  define YY_LOCATION_PRINT(File, Loc) ((void) 0)
# endif


# define YY_SYMBOL_PRINT(Title, Kind, Value, Location)                    \
do {                                                                      \
  if (yydebug)                                                            \
    {                                                                     \
      YYFPRINTF (stderr, "%s ", Title);                                   \
      yy_symbol_print (stderr,                                            \
                  Kind, Value); \
      YYFPRINTF (stderr, "\n");                                           \
    }                                                                     \
} while (0)


/*-----------------------------------.
| Print this symbol's value on YYO.  |
`-----------------------------------*/

static void
yy_symbol_value_print (FILE *yyo,
                       yysymbol_kind_t yykind, YYSTYPE const * const yyvaluep)
{
  FILE *yyoutput = yyo;
  YY_USE (yyoutput);
  if (!yyvaluep)
    return;
# ifdef YYPRINT
  if (yykind < YYNTOKENS)
    YYPRINT (yyo, yytoknum[yykind], *yyvaluep);
# endif
  YY_IGNORE_MAYBE_UNINITIALIZED_BEGIN
  YY_USE (yykind);
  YY_IGNORE_MAYBE_UNINITIALIZED_END
}


/*---------------------------.
| Print this symbol on YYO.  |
`---------------------------*/

static void
yy_symbol_print (FILE *yyo,
                 yysymbol_kind_t yykind, YYSTYPE const * const yyvaluep)
{
  YYFPRINTF (yyo, "%s %s (",
             yykind < YYNTOKENS ? "token" : "nterm", yysymbol_name (yykind));

  yy_symbol_value_print (yyo, yykind, yyvaluep);
  YYFPRINTF (yyo, ")");
}

/*------------------------------------------------------------------.
| yy_stack_print -- Print the state stack from its BOTTOM up to its |
| TOP (included).                                                   |
`------------------------------------------------------------------*/

static void
yy_stack_print (yy_state_t *yybottom, yy_state_t *yytop)
{
  YYFPRINTF (stderr, "Stack now");
  for (; yybottom <= yytop; yybottom++)
    {
      int yybot = *yybottom;
      YYFPRINTF (stderr, " %d", yybot);
    }
  YYFPRINTF (stderr, "\n");
}

# define YY_STACK_PRINT(Bottom, Top)                            \
do {                                                            \
  if (yydebug)                                                  \
    yy_stack_print ((Bottom), (Top));                           \
} while (0)


/*------------------------------------------------.
| Report that the YYRULE is going to be reduced.  |
`------------------------------------------------*/

static void
yy_reduce_print (yy_state_t *yyssp, YYSTYPE *yyvsp,
                 int yyrule)
{
  int yylno = yyrline[yyrule];
  int yynrhs = yyr2[yyrule];
  int yyi;
  YYFPRINTF (stderr, "Reducing stack by rule %d (line %d):\n",
             yyrule - 1, yylno);
  /* The symbols being reduced.  */
  for (yyi = 0; yyi < yynrhs; yyi++)
    {
      YYFPRINTF (stderr, "   $%d = ", yyi + 1);
      yy_symbol_print (stderr,
                       YY_ACCESSING_SYMBOL (+yyssp[yyi + 1 - yynrhs]),
                       &yyvsp[(yyi + 1) - (yynrhs)]);
      YYFPRINTF (stderr, "\n");
    }
}

# define YY_REDUCE_PRINT(Rule)          \
do {                                    \
  if (yydebug)                          \
    yy_reduce_print (yyssp, yyvsp, Rule); \
} while (0)

/* Nonzero means print parse trace.  It is left uninitialized so that
   multiple parsers can coexist.  */
int yydebug;
#else /* !YYDEBUG */
# define YYDPRINTF(Args) ((void) 0)
# define YY_SYMBOL_PRINT(Title, Kind, Value, Location)
# define YY_STACK_PRINT(Bottom, Top)
# define YY_REDUCE_PRINT(Rule)
#endif /* !YYDEBUG */


/* YYINITDEPTH -- initial size of the parser's stacks.  */
#ifndef YYINITDEPTH
# define YYINITDEPTH 200
#endif

/* YYMAXDEPTH -- maximum size the stacks can grow to (effective only
   if the built-in stack extension method is used).

   Do not make this value too large; the results are undefined if
   YYSTACK_ALLOC_MAXIMUM < YYSTACK_BYTES (YYMAXDEPTH)
   evaluated with infinite-precision integer arithmetic.  */

#ifndef YYMAXDEPTH
# define YYMAXDEPTH 10000
#endif






/*-----------------------------------------------.
| Release the memory associated to this symbol.  |
`-----------------------------------------------*/

static void
yydestruct (const char *yymsg,
            yysymbol_kind_t yykind, YYSTYPE *yyvaluep)
{
  YY_USE (yyvaluep);
  if (!yymsg)
    yymsg = "Deleting";
  YY_SYMBOL_PRINT (yymsg, yykind, yyvaluep, yylocationp);

  YY_IGNORE_MAYBE_UNINITIALIZED_BEGIN
  YY_USE (yykind);
  YY_IGNORE_MAYBE_UNINITIALIZED_END
}


/* Lookahead token kind.  */
int yychar;

/* The semantic value of the lookahead symbol.  */
YYSTYPE yylval;
/* Number of syntax errors so far.  */
int yynerrs;




/*----------.
| yyparse.  |
`----------*/

int
yyparse (void)
{
    yy_state_fast_t yystate = 0;
    /* Number of tokens to shift before error messages enabled.  */
    int yyerrstatus = 0;

    /* Refer to the stacks through separate pointers, to allow yyoverflow
       to reallocate them elsewhere.  */

    /* Their size.  */
    YYPTRDIFF_T yystacksize = YYINITDEPTH;

    /* The state stack: array, bottom, top.  */
    yy_state_t yyssa[YYINITDEPTH];
    yy_state_t *yyss = yyssa;
    yy_state_t *yyssp = yyss;

    /* The semantic value stack: array, bottom, top.  */
    YYSTYPE yyvsa[YYINITDEPTH];
    YYSTYPE *yyvs = yyvsa;
    YYSTYPE *yyvsp = yyvs;

  int yyn;
  /* The return value of yyparse.  */
  int yyresult;
  /* Lookahead symbol kind.  */
  yysymbol_kind_t yytoken = YYSYMBOL_YYEMPTY;
  /* The variables used to return semantic value and location from the
     action routines.  */
  YYSTYPE yyval;



#define YYPOPSTACK(N)   (yyvsp -= (N), yyssp -= (N))

  /* The number of symbols on the RHS of the reduced rule.
     Keep to zero when no symbol should be popped.  */
  int yylen = 0;

  YYDPRINTF ((stderr, "Starting parse\n"));

  yychar = YYEMPTY; /* Cause a token to be read.  */
  goto yysetstate;


/*------------------------------------------------------------.
| yynewstate -- push a new state, which is found in yystate.  |
`------------------------------------------------------------*/
yynewstate:
  /* In all cases, when you get here, the value and location stacks
     have just been pushed.  So pushing a state here evens the stacks.  */
  yyssp++;


/*--------------------------------------------------------------------.
| yysetstate -- set current state (the top of the stack) to yystate.  |
`--------------------------------------------------------------------*/
yysetstate:
  YYDPRINTF ((stderr, "Entering state %d\n", yystate));
  YY_ASSERT (0 <= yystate && yystate < YYNSTATES);
  YY_IGNORE_USELESS_CAST_BEGIN
  *yyssp = YY_CAST (yy_state_t, yystate);
  YY_IGNORE_USELESS_CAST_END
  YY_STACK_PRINT (yyss, yyssp);

  if (yyss + yystacksize - 1 <= yyssp)
#if !defined yyoverflow && !defined YYSTACK_RELOCATE
    goto yyexhaustedlab;
#else
    {
      /* Get the current used size of the three stacks, in elements.  */
      YYPTRDIFF_T yysize = yyssp - yyss + 1;

# if defined yyoverflow
      {
        /* Give user a chance to reallocate the stack.  Use copies of
           these so that the &'s don't force the real ones into
           memory.  */
        yy_state_t *yyss1 = yyss;
        YYSTYPE *yyvs1 = yyvs;

        /* Each stack pointer address is followed by the size of the
           data in use in that stack, in bytes.  This used to be a
           conditional around just the two extra args, but that might
           be undefined if yyoverflow is a macro.  */
        yyoverflow (YY_("memory exhausted"),
                    &yyss1, yysize * YYSIZEOF (*yyssp),
                    &yyvs1, yysize * YYSIZEOF (*yyvsp),
                    &yystacksize);
        yyss = yyss1;
        yyvs = yyvs1;
      }
# else /* defined YYSTACK_RELOCATE */
      /* Extend the stack our own way.  */
      if (YYMAXDEPTH <= yystacksize)
        goto yyexhaustedlab;
      yystacksize *= 2;
      if (YYMAXDEPTH < yystacksize)
        yystacksize = YYMAXDEPTH;

      {
        yy_state_t *yyss1 = yyss;
        union yyalloc *yyptr =
          YY_CAST (union yyalloc *,
                   YYSTACK_ALLOC (YY_CAST (YYSIZE_T, YYSTACK_BYTES (yystacksize))));
        if (! yyptr)
          goto yyexhaustedlab;
        YYSTACK_RELOCATE (yyss_alloc, yyss);
        YYSTACK_RELOCATE (yyvs_alloc, yyvs);
#  undef YYSTACK_RELOCATE
        if (yyss1 != yyssa)
          YYSTACK_FREE (yyss1);
      }
# endif

      yyssp = yyss + yysize - 1;
      yyvsp = yyvs + yysize - 1;

      YY_IGNORE_USELESS_CAST_BEGIN
      YYDPRINTF ((stderr, "Stack size increased to %ld\n",
                  YY_CAST (long, yystacksize)));
      YY_IGNORE_USELESS_CAST_END

      if (yyss + yystacksize - 1 <= yyssp)
        YYABORT;
    }
#endif /* !defined yyoverflow && !defined YYSTACK_RELOCATE */

  if (yystate == YYFINAL)
    YYACCEPT;

  goto yybackup;


/*-----------.
| yybackup.  |
`-----------*/
yybackup:
  /* Do appropriate processing given the current state.  Read a
     lookahead token if we need one and don't already have one.  */

  /* First try to decide what to do without reference to lookahead token.  */
  yyn = yypact[yystate];
  if (yypact_value_is_default (yyn))
    goto yydefault;

  /* Not known => get a lookahead token if don't already have one.  */

  /* YYCHAR is either empty, or end-of-input, or a valid lookahead.  */
  if (yychar == YYEMPTY)
    {
      YYDPRINTF ((stderr, "Reading a token\n"));
      yychar = yylex ();
    }

  if (yychar <= YYEOF)
    {
      yychar = YYEOF;
      yytoken = YYSYMBOL_YYEOF;
      YYDPRINTF ((stderr, "Now at end of input.\n"));
    }
  else if (yychar == YYerror)
    {
      /* The scanner already issued an error message, process directly
         to error recovery.  But do not keep the error token as
         lookahead, it is too special and may lead us to an endless
         loop in error recovery. */
      yychar = YYUNDEF;
      yytoken = YYSYMBOL_YYerror;
      goto yyerrlab1;
    }
  else
    {
      yytoken = YYTRANSLATE (yychar);
      YY_SYMBOL_PRINT ("Next token is", yytoken, &yylval, &yylloc);
    }

  /* If the proper action on seeing token YYTOKEN is to reduce or to
     detect an error, take that action.  */
  yyn += yytoken;
  if (yyn < 0 || YYLAST < yyn || yycheck[yyn] != yytoken)
    goto yydefault;
  yyn = yytable[yyn];
  if (yyn <= 0)
    {
      if (yytable_value_is_error (yyn))
        goto yyerrlab;
      yyn = -yyn;
      goto yyreduce;
    }

  /* Count tokens shifted since error; after three, turn off error
     status.  */
  if (yyerrstatus)
    yyerrstatus--;

  /* Shift the lookahead token.  */
  YY_SYMBOL_PRINT ("Shifting", yytoken, &yylval, &yylloc);
  yystate = yyn;
  YY_IGNORE_MAYBE_UNINITIALIZED_BEGIN
  *++yyvsp = yylval;
  YY_IGNORE_MAYBE_UNINITIALIZED_END

  /* Discard the shifted token.  */
  yychar = YYEMPTY;
  goto yynewstate;


/*-----------------------------------------------------------.
| yydefault -- do the default action for the current state.  |
`-----------------------------------------------------------*/
yydefault:
  yyn = yydefact[yystate];
  if (yyn == 0)
    goto yyerrlab;
  goto yyreduce;


/*-----------------------------.
| yyreduce -- do a reduction.  |
`-----------------------------*/
yyreduce:
  /* yyn is the number of a rule to reduce with.  */
  yylen = yyr2[yyn];

  /* If YYLEN is nonzero, implement the default value of the action:
     '$$ = $1'.

     Otherwise, the following line sets YYVAL to garbage.
     This behavior is undocumented and Bison
     users should not rely upon it.  Assigning to YYVAL
     unconditionally makes the parser a bit smaller, and it avoids a
     GCC warning that YYVAL may be used uninitialized.  */
  yyval = yyvsp[1-yylen];


  YY_REDUCE_PRINT (yyn);
  switch (yyn)
    {
  case 2: /* program: command_lines  */
#line 123 "parser.y"
      {
          ussr_parsed_program = (yyvsp[0].command_list);
          (yyval.command_list) = (yyvsp[0].command_list);
      }
#line 1232 "parser.tab.c"
    break;

  case 3: /* program: LBRACKET command_lines RBRACKET  */
#line 128 "parser.y"
      {
          ussr_parsed_program = (yyvsp[-1].command_list);
          (yyval.command_list) = (yyvsp[-1].command_list);
      }
#line 1241 "parser.tab.c"
    break;

  case 4: /* command_lines: %empty  */
#line 136 "parser.y"
      {
          (yyval.command_list) = ussr_command_list_create();

          if ((yyval.command_list) == NULL)
              YYABORT;
      }
#line 1252 "parser.tab.c"
    break;

  case 5: /* command_lines: command_lines command_line  */
#line 143 "parser.y"
      {
          if ((yyvsp[0].command) != NULL)
          {
              if (ussr_command_list_append((yyvsp[-1].command_list), (yyvsp[0].command)) != 0)
              {
                  ussr_command_list_free((yyvsp[-1].command_list));
                  YYABORT;
              }
          }

          (yyval.command_list) = (yyvsp[-1].command_list);
      }
#line 1269 "parser.tab.c"
    break;

  case 6: /* command_line: NEWLINE  */
#line 159 "parser.y"
      {
          (yyval.command) = NULL;
      }
#line 1277 "parser.tab.c"
    break;

  case 7: /* command_line: command NEWLINE  */
#line 163 "parser.y"
      {
          (yyval.command) = (yyvsp[-1].command);
      }
#line 1285 "parser.tab.c"
    break;

  case 8: /* command_line: command  */
#line 167 "parser.y"
      {
          (yyval.command) = (yyvsp[0].command);
      }
#line 1293 "parser.tab.c"
    break;

  case 9: /* command: IDENTIFIER LPAREN IDENTIFIER RPAREN COLON argument_list  */
#line 174 "parser.y"
      {
          (yyval.command) = ussr_command_create(
              (yyvsp[-5].string),
              (yyvsp[-3].string),
              (yyvsp[0].arguments).items,
              (yyvsp[0].arguments).count
          );

          if ((yyval.command) == NULL)
          {
              free((yyvsp[-5].string));
              free((yyvsp[-3].string));
              free((yyvsp[0].arguments).items);
              YYABORT;
          }
      }
#line 1314 "parser.tab.c"
    break;

  case 10: /* argument_list: argument  */
#line 194 "parser.y"
      {
          (yyval.arguments).items = malloc(sizeof(*(yyval.arguments).items));

          if ((yyval.arguments).items == NULL)
          {
              YYABORT;
          }

          (yyval.arguments).items[0] = (yyvsp[0].argument);
          (yyval.arguments).count = 1;
      }
#line 1330 "parser.tab.c"
    break;

  case 11: /* argument_list: argument_list argument  */
#line 206 "parser.y"
      {
          ussr_argument_t *items;

          items = realloc(
              (yyvsp[-1].arguments).items,
              ((yyvsp[-1].arguments).count + 1) * sizeof(*items)
          );

          if (items == NULL)
          {
              free((yyvsp[-1].arguments).items);
              YYABORT;
          }

          items[(yyvsp[-1].arguments).count] = (yyvsp[0].argument);

          (yyval.arguments).items = items;
          (yyval.arguments).count = (yyvsp[-1].arguments).count + 1;
      }
#line 1354 "parser.tab.c"
    break;

  case 12: /* argument: argument_base  */
#line 229 "parser.y"
      {
          (yyval.argument) = (yyvsp[0].argument);
      }
#line 1362 "parser.tab.c"
    break;

  case 13: /* argument: IDENTIFIER QUESTION  */
#line 233 "parser.y"
      {
          ussr_expression_t *expression;

          expression = malloc(sizeof(*expression));
          if (expression == NULL)
          {
              free((yyvsp[-1].string));
              YYABORT;
          }

          expression->type = USSR_EXPR_VARIABLE;
          expression->data.variable = (yyvsp[-1].string);

          (yyval.argument).type = USSR_ARGUMENT_LOOKUP;
          (yyval.argument).assignment = 0;
          (yyval.argument).data.expression = expression;
      }
#line 1384 "parser.tab.c"
    break;

  case 14: /* argument: argument_base EXCLAMATION  */
#line 251 "parser.y"
      {
          (yyval.argument) = (yyvsp[-1].argument);
          (yyval.argument).assignment = 1;
      }
#line 1393 "parser.tab.c"
    break;

  case 15: /* argument_base: literal  */
#line 259 "parser.y"
      {
          (yyval.argument).type = USSR_ARGUMENT_VALUE;
          (yyval.argument).assignment = 0;
          (yyval.argument).data.value = (yyvsp[0].value);
      }
#line 1403 "parser.tab.c"
    break;

  case 16: /* argument_base: IDENTIFIER  */
#line 265 "parser.y"
      {
          ussr_expression_t *expression;

          expression = malloc(sizeof(*expression));
          if (expression == NULL)
          {
              free((yyvsp[0].string));
              YYABORT;
          }

          expression->type = USSR_EXPR_VARIABLE;
          expression->data.variable = (yyvsp[0].string);

          (yyval.argument).type = USSR_ARGUMENT_EXPRESSION;
          (yyval.argument).assignment = 0;
          (yyval.argument).data.expression = expression;
      }
#line 1425 "parser.tab.c"
    break;

  case 17: /* argument_base: expression  */
#line 283 "parser.y"
      {
          (yyval.argument).type = USSR_ARGUMENT_EXPRESSION;
          (yyval.argument).assignment = 0;
          (yyval.argument).data.expression = (yyvsp[0].expression);
      }
#line 1435 "parser.tab.c"
    break;

  case 18: /* argument_base: block_argument  */
#line 289 "parser.y"
      {
          (yyval.argument) = (yyvsp[0].argument);
      }
#line 1443 "parser.tab.c"
    break;

  case 19: /* argument_base: UNO_LITERAL  */
#line 293 "parser.y"
      {
          /*
           * Deferred, like block_argument: the struct type(s) this
           * text names may not be registered until execution time
           * (see ussr.h's ussr_argument_t.data.uno_text), so we can't
           * resolve it to a real ussr_value_t here at parse time.
           */
          (yyval.argument).type = USSR_ARGUMENT_UNO_LITERAL;
          (yyval.argument).assignment = 0;
          (yyval.argument).data.uno_text = (yyvsp[0].string);
      }
#line 1459 "parser.tab.c"
    break;

  case 20: /* block_argument: LBRACKET command_lines RBRACKET  */
#line 308 "parser.y"
      {
          (yyval.argument).type = USSR_ARGUMENT_COMMAND_LIST;
          (yyval.argument).assignment = 0;
          (yyval.argument).data.command_list = (yyvsp[-1].command_list);
      }
#line 1469 "parser.tab.c"
    break;

  case 21: /* literal: STRING  */
#line 317 "parser.y"
      {
          (yyval.value).type = USSR_STRING;
          (yyval.value).data.string = (yyvsp[0].string);
      }
#line 1478 "parser.tab.c"
    break;

  case 22: /* literal: INTEGER  */
#line 322 "parser.y"
      {
          (yyval.value).type = USSR_INTEGER;
          (yyval.value).data.integer = (yyvsp[0].integer);
      }
#line 1487 "parser.tab.c"
    break;

  case 23: /* literal: REAL  */
#line 327 "parser.y"
      {
          (yyval.value).type = USSR_REAL;
          (yyval.value).data.real = (yyvsp[0].real);
      }
#line 1496 "parser.tab.c"
    break;

  case 24: /* literal: BOOLEAN  */
#line 332 "parser.y"
      {
          (yyval.value).type = USSR_BOOLEAN;
          (yyval.value).data.boolean = (yyvsp[0].boolean);
      }
#line 1505 "parser.tab.c"
    break;

  case 25: /* literal: NULL_VALUE  */
#line 337 "parser.y"
      {
          (yyval.value).type = USSR_NULL;
      }
#line 1513 "parser.tab.c"
    break;

  case 26: /* expression: LBRACE logical_or_expression RBRACE  */
#line 344 "parser.y"
      {
          (yyval.expression) = (yyvsp[-1].expression);
      }
#line 1521 "parser.tab.c"
    break;

  case 27: /* logical_or_expression: logical_and_expression  */
#line 351 "parser.y"
      {
          (yyval.expression) = (yyvsp[0].expression);
      }
#line 1529 "parser.tab.c"
    break;

  case 28: /* logical_or_expression: logical_or_expression LOGICAL_OR logical_and_expression  */
#line 355 "parser.y"
      {
          (yyval.expression) = ussr_make_binary_expression(
              (yyvsp[-2].expression), USSR_OP_LOGICAL_OR, (yyvsp[0].expression)
          );
          if ((yyval.expression) == NULL)
              YYABORT;
      }
#line 1541 "parser.tab.c"
    break;

  case 29: /* logical_and_expression: bitwise_or_expression  */
#line 366 "parser.y"
      {
          (yyval.expression) = (yyvsp[0].expression);
      }
#line 1549 "parser.tab.c"
    break;

  case 30: /* logical_and_expression: logical_and_expression LOGICAL_AND bitwise_or_expression  */
#line 370 "parser.y"
      {
          (yyval.expression) = ussr_make_binary_expression(
              (yyvsp[-2].expression), USSR_OP_LOGICAL_AND, (yyvsp[0].expression)
          );
          if ((yyval.expression) == NULL)
              YYABORT;
      }
#line 1561 "parser.tab.c"
    break;

  case 31: /* bitwise_or_expression: bitwise_xor_expression  */
#line 381 "parser.y"
      {
          (yyval.expression) = (yyvsp[0].expression);
      }
#line 1569 "parser.tab.c"
    break;

  case 32: /* bitwise_or_expression: bitwise_or_expression BITWISE_OR bitwise_xor_expression  */
#line 385 "parser.y"
      {
          (yyval.expression) = ussr_make_binary_expression(
              (yyvsp[-2].expression), USSR_OP_BITWISE_OR, (yyvsp[0].expression)
          );
          if ((yyval.expression) == NULL)
              YYABORT;
      }
#line 1581 "parser.tab.c"
    break;

  case 33: /* bitwise_xor_expression: bitwise_and_expression  */
#line 396 "parser.y"
      {
          (yyval.expression) = (yyvsp[0].expression);
      }
#line 1589 "parser.tab.c"
    break;

  case 34: /* bitwise_xor_expression: bitwise_xor_expression BITWISE_XOR bitwise_and_expression  */
#line 400 "parser.y"
      {
          (yyval.expression) = ussr_make_binary_expression(
              (yyvsp[-2].expression), USSR_OP_BITWISE_XOR, (yyvsp[0].expression)
          );
          if ((yyval.expression) == NULL)
              YYABORT;
      }
#line 1601 "parser.tab.c"
    break;

  case 35: /* bitwise_and_expression: comparison_expression  */
#line 411 "parser.y"
      {
          (yyval.expression) = (yyvsp[0].expression);
      }
#line 1609 "parser.tab.c"
    break;

  case 36: /* bitwise_and_expression: bitwise_and_expression BITWISE_AND comparison_expression  */
#line 415 "parser.y"
      {
          (yyval.expression) = ussr_make_binary_expression(
              (yyvsp[-2].expression), USSR_OP_BITWISE_AND, (yyvsp[0].expression)
          );
          if ((yyval.expression) == NULL)
              YYABORT;
      }
#line 1621 "parser.tab.c"
    break;

  case 37: /* comparison_expression: shift_expression  */
#line 426 "parser.y"
      {
          (yyval.expression) = (yyvsp[0].expression);
      }
#line 1629 "parser.tab.c"
    break;

  case 38: /* comparison_expression: shift_expression GREATER shift_expression  */
#line 430 "parser.y"
      {
          (yyval.expression) = ussr_make_binary_expression(
              (yyvsp[-2].expression), USSR_OP_GT, (yyvsp[0].expression)
          );
          if ((yyval.expression) == NULL)
              YYABORT;
      }
#line 1641 "parser.tab.c"
    break;

  case 39: /* comparison_expression: shift_expression LESS shift_expression  */
#line 438 "parser.y"
      {
          (yyval.expression) = ussr_make_binary_expression(
              (yyvsp[-2].expression), USSR_OP_LT, (yyvsp[0].expression)
          );
          if ((yyval.expression) == NULL)
              YYABORT;
      }
#line 1653 "parser.tab.c"
    break;

  case 40: /* comparison_expression: shift_expression GREATER_EQUAL shift_expression  */
#line 446 "parser.y"
      {
          (yyval.expression) = ussr_make_binary_expression(
              (yyvsp[-2].expression), USSR_OP_GE, (yyvsp[0].expression)
          );
          if ((yyval.expression) == NULL)
              YYABORT;
      }
#line 1665 "parser.tab.c"
    break;

  case 41: /* comparison_expression: shift_expression LESS_EQUAL shift_expression  */
#line 454 "parser.y"
      {
          (yyval.expression) = ussr_make_binary_expression(
              (yyvsp[-2].expression), USSR_OP_LE, (yyvsp[0].expression)
          );
          if ((yyval.expression) == NULL)
              YYABORT;
      }
#line 1677 "parser.tab.c"
    break;

  case 42: /* comparison_expression: shift_expression EQUAL shift_expression  */
#line 462 "parser.y"
      {
          (yyval.expression) = ussr_make_binary_expression(
              (yyvsp[-2].expression), USSR_OP_EQ, (yyvsp[0].expression)
          );
          if ((yyval.expression) == NULL)
              YYABORT;
      }
#line 1689 "parser.tab.c"
    break;

  case 43: /* comparison_expression: shift_expression NOT_EQUAL shift_expression  */
#line 470 "parser.y"
      {
          (yyval.expression) = ussr_make_binary_expression(
              (yyvsp[-2].expression), USSR_OP_NE, (yyvsp[0].expression)
          );
          if ((yyval.expression) == NULL)
              YYABORT;
      }
#line 1701 "parser.tab.c"
    break;

  case 44: /* shift_expression: additive_expression  */
#line 481 "parser.y"
      {
          (yyval.expression) = (yyvsp[0].expression);
      }
#line 1709 "parser.tab.c"
    break;

  case 45: /* shift_expression: shift_expression SHIFT_LEFT additive_expression  */
#line 485 "parser.y"
      {
          (yyval.expression) = ussr_make_binary_expression(
              (yyvsp[-2].expression), USSR_OP_SHIFT_LEFT, (yyvsp[0].expression)
          );
          if ((yyval.expression) == NULL)
              YYABORT;
      }
#line 1721 "parser.tab.c"
    break;

  case 46: /* shift_expression: shift_expression SHIFT_RIGHT additive_expression  */
#line 493 "parser.y"
      {
          (yyval.expression) = ussr_make_binary_expression(
              (yyvsp[-2].expression), USSR_OP_SHIFT_RIGHT, (yyvsp[0].expression)
          );
          if ((yyval.expression) == NULL)
              YYABORT;
      }
#line 1733 "parser.tab.c"
    break;

  case 47: /* additive_expression: multiplicative_expression  */
#line 504 "parser.y"
      {
          (yyval.expression) = (yyvsp[0].expression);
      }
#line 1741 "parser.tab.c"
    break;

  case 48: /* additive_expression: additive_expression PLUS multiplicative_expression  */
#line 508 "parser.y"
      {
          (yyval.expression) = ussr_make_binary_expression(
              (yyvsp[-2].expression), USSR_OP_ADD, (yyvsp[0].expression)
          );
          if ((yyval.expression) == NULL)
              YYABORT;
      }
#line 1753 "parser.tab.c"
    break;

  case 49: /* additive_expression: additive_expression MINUS multiplicative_expression  */
#line 516 "parser.y"
      {
          (yyval.expression) = ussr_make_binary_expression(
              (yyvsp[-2].expression), USSR_OP_SUB, (yyvsp[0].expression)
          );
          if ((yyval.expression) == NULL)
              YYABORT;
      }
#line 1765 "parser.tab.c"
    break;

  case 50: /* multiplicative_expression: unary_expression  */
#line 527 "parser.y"
      {
          (yyval.expression) = (yyvsp[0].expression);
      }
#line 1773 "parser.tab.c"
    break;

  case 51: /* multiplicative_expression: multiplicative_expression MULTIPLY unary_expression  */
#line 531 "parser.y"
      {
          (yyval.expression) = ussr_make_binary_expression(
              (yyvsp[-2].expression), USSR_OP_MUL, (yyvsp[0].expression)
          );
          if ((yyval.expression) == NULL)
              YYABORT;
      }
#line 1785 "parser.tab.c"
    break;

  case 52: /* multiplicative_expression: multiplicative_expression DIVIDE unary_expression  */
#line 539 "parser.y"
      {
          (yyval.expression) = ussr_make_binary_expression(
              (yyvsp[-2].expression), USSR_OP_DIV, (yyvsp[0].expression)
          );
          if ((yyval.expression) == NULL)
              YYABORT;
      }
#line 1797 "parser.tab.c"
    break;

  case 53: /* multiplicative_expression: multiplicative_expression MODULO unary_expression  */
#line 547 "parser.y"
      {
          (yyval.expression) = ussr_make_binary_expression(
              (yyvsp[-2].expression), USSR_OP_MOD, (yyvsp[0].expression)
          );
          if ((yyval.expression) == NULL)
              YYABORT;
      }
#line 1809 "parser.tab.c"
    break;

  case 54: /* unary_expression: primary_expression  */
#line 558 "parser.y"
      {
          (yyval.expression) = (yyvsp[0].expression);
      }
#line 1817 "parser.tab.c"
    break;

  case 55: /* unary_expression: MINUS unary_expression  */
#line 562 "parser.y"
      {
          (yyval.expression) = malloc(sizeof(*(yyval.expression)));
          if ((yyval.expression) == NULL)
              YYABORT;
          (yyval.expression)->type = USSR_EXPR_UNARY;
          (yyval.expression)->data.unary.operator = USSR_OP_SUB;
          (yyval.expression)->data.unary.operand = (yyvsp[0].expression);
      }
#line 1830 "parser.tab.c"
    break;

  case 56: /* primary_expression: literal  */
#line 574 "parser.y"
      {
          (yyval.expression) = malloc(sizeof(*(yyval.expression)));
          if ((yyval.expression) == NULL)
              YYABORT;
          (yyval.expression)->type = USSR_EXPR_VALUE;
          (yyval.expression)->data.value = (yyvsp[0].value);
      }
#line 1842 "parser.tab.c"
    break;

  case 57: /* primary_expression: IDENTIFIER  */
#line 582 "parser.y"
      {
          (yyval.expression) = malloc(sizeof(*(yyval.expression)));
          if ((yyval.expression) == NULL)
          {
              free((yyvsp[0].string));
              YYABORT;
          }
          (yyval.expression)->type = USSR_EXPR_VARIABLE;
          (yyval.expression)->data.variable = (yyvsp[0].string);
      }
#line 1857 "parser.tab.c"
    break;

  case 58: /* primary_expression: LPAREN logical_or_expression RPAREN  */
#line 593 "parser.y"
      {
          (yyval.expression) = (yyvsp[-1].expression);
      }
#line 1865 "parser.tab.c"
    break;


#line 1869 "parser.tab.c"

      default: break;
    }
  /* User semantic actions sometimes alter yychar, and that requires
     that yytoken be updated with the new translation.  We take the
     approach of translating immediately before every use of yytoken.
     One alternative is translating here after every semantic action,
     but that translation would be missed if the semantic action invokes
     YYABORT, YYACCEPT, or YYERROR immediately after altering yychar or
     if it invokes YYBACKUP.  In the case of YYABORT or YYACCEPT, an
     incorrect destructor might then be invoked immediately.  In the
     case of YYERROR or YYBACKUP, subsequent parser actions might lead
     to an incorrect destructor call or verbose syntax error message
     before the lookahead is translated.  */
  YY_SYMBOL_PRINT ("-> $$ =", YY_CAST (yysymbol_kind_t, yyr1[yyn]), &yyval, &yyloc);

  YYPOPSTACK (yylen);
  yylen = 0;

  *++yyvsp = yyval;

  /* Now 'shift' the result of the reduction.  Determine what state
     that goes to, based on the state we popped back to and the rule
     number reduced by.  */
  {
    const int yylhs = yyr1[yyn] - YYNTOKENS;
    const int yyi = yypgoto[yylhs] + *yyssp;
    yystate = (0 <= yyi && yyi <= YYLAST && yycheck[yyi] == *yyssp
               ? yytable[yyi]
               : yydefgoto[yylhs]);
  }

  goto yynewstate;


/*--------------------------------------.
| yyerrlab -- here on detecting error.  |
`--------------------------------------*/
yyerrlab:
  /* Make sure we have latest lookahead translation.  See comments at
     user semantic actions for why this is necessary.  */
  yytoken = yychar == YYEMPTY ? YYSYMBOL_YYEMPTY : YYTRANSLATE (yychar);
  /* If not already recovering from an error, report this error.  */
  if (!yyerrstatus)
    {
      ++yynerrs;
      yyerror (YY_("syntax error"));
    }

  if (yyerrstatus == 3)
    {
      /* If just tried and failed to reuse lookahead token after an
         error, discard it.  */

      if (yychar <= YYEOF)
        {
          /* Return failure if at end of input.  */
          if (yychar == YYEOF)
            YYABORT;
        }
      else
        {
          yydestruct ("Error: discarding",
                      yytoken, &yylval);
          yychar = YYEMPTY;
        }
    }

  /* Else will try to reuse lookahead token after shifting the error
     token.  */
  goto yyerrlab1;


/*---------------------------------------------------.
| yyerrorlab -- error raised explicitly by YYERROR.  |
`---------------------------------------------------*/
yyerrorlab:
  /* Pacify compilers when the user code never invokes YYERROR and the
     label yyerrorlab therefore never appears in user code.  */
  if (0)
    YYERROR;

  /* Do not reclaim the symbols of the rule whose action triggered
     this YYERROR.  */
  YYPOPSTACK (yylen);
  yylen = 0;
  YY_STACK_PRINT (yyss, yyssp);
  yystate = *yyssp;
  goto yyerrlab1;


/*-------------------------------------------------------------.
| yyerrlab1 -- common code for both syntax error and YYERROR.  |
`-------------------------------------------------------------*/
yyerrlab1:
  yyerrstatus = 3;      /* Each real token shifted decrements this.  */

  /* Pop stack until we find a state that shifts the error token.  */
  for (;;)
    {
      yyn = yypact[yystate];
      if (!yypact_value_is_default (yyn))
        {
          yyn += YYSYMBOL_YYerror;
          if (0 <= yyn && yyn <= YYLAST && yycheck[yyn] == YYSYMBOL_YYerror)
            {
              yyn = yytable[yyn];
              if (0 < yyn)
                break;
            }
        }

      /* Pop the current state because it cannot handle the error token.  */
      if (yyssp == yyss)
        YYABORT;


      yydestruct ("Error: popping",
                  YY_ACCESSING_SYMBOL (yystate), yyvsp);
      YYPOPSTACK (1);
      yystate = *yyssp;
      YY_STACK_PRINT (yyss, yyssp);
    }

  YY_IGNORE_MAYBE_UNINITIALIZED_BEGIN
  *++yyvsp = yylval;
  YY_IGNORE_MAYBE_UNINITIALIZED_END


  /* Shift the error token.  */
  YY_SYMBOL_PRINT ("Shifting", YY_ACCESSING_SYMBOL (yyn), yyvsp, yylsp);

  yystate = yyn;
  goto yynewstate;


/*-------------------------------------.
| yyacceptlab -- YYACCEPT comes here.  |
`-------------------------------------*/
yyacceptlab:
  yyresult = 0;
  goto yyreturn;


/*-----------------------------------.
| yyabortlab -- YYABORT comes here.  |
`-----------------------------------*/
yyabortlab:
  yyresult = 1;
  goto yyreturn;


#if !defined yyoverflow
/*-------------------------------------------------.
| yyexhaustedlab -- memory exhaustion comes here.  |
`-------------------------------------------------*/
yyexhaustedlab:
  yyerror (YY_("memory exhausted"));
  yyresult = 2;
  goto yyreturn;
#endif


/*-------------------------------------------------------.
| yyreturn -- parsing is finished, clean up and return.  |
`-------------------------------------------------------*/
yyreturn:
  if (yychar != YYEMPTY)
    {
      /* Make sure we have latest lookahead translation.  See comments at
         user semantic actions for why this is necessary.  */
      yytoken = YYTRANSLATE (yychar);
      yydestruct ("Cleanup: discarding lookahead",
                  yytoken, &yylval);
    }
  /* Do not reclaim the symbols of the rule whose action triggered
     this YYABORT or YYACCEPT.  */
  YYPOPSTACK (yylen);
  YY_STACK_PRINT (yyss, yyssp);
  while (yyssp != yyss)
    {
      yydestruct ("Cleanup: popping",
                  YY_ACCESSING_SYMBOL (+*yyssp), yyvsp);
      YYPOPSTACK (1);
    }
#ifndef yyoverflow
  if (yyss != yyssa)
    YYSTACK_FREE (yyss);
#endif

  return yyresult;
}

#line 598 "parser.y"

