/**
 * Tree-sitter grammar for USSR (Unified Shell Script REPL).
 *
 * Derived from the current Flex lexer and Bison grammar:
 *   src/lexer.l
 *   src/parser.y (conflict-free) and docs/grammar.bnf
 *
 * Preprocessor lines ($define, $include, $if ... $endif) are removed by
 * src/pp.c before the Bison parser runs; here they are parsed as their
 * own line items so editors can highlight them.
 */
module.exports = grammar({
  name: 'ussr',

  extras: $ => [
    /[ \t\r]+/,
    $.comment,
  ],

  word: $ => $.identifier,

  rules: {
    // program ::= command_lines, where a newline is an empty command line
    // of its own (see parser.y's command_line).
    source_file: $ => repeat($._line_item),

    _line_item: $ => choice(
      $.newline,
      $.command,
      $.advanced_control,
      $.preprocessor,
    ),

    // The Flex lexer recognizes COMMAND_HEAD as everything up to the
    // opening parenthesis, which must follow without a space. Keeping
    // that as one token avoids lexical ambiguity between command names
    // and ordinary identifiers.
    command: $ => seq(
      field('name', $.command_head),
      field('result', $.identifier),
      ')',
      $.command_tail,
    ),

    command_head: $ => token(seq(
      /[A-Za-z0-9_./\\:-]+/,
      '(',
    )),

    command_tail: $ => seq(
      ':',
      optional($.argument_list),
    ),

    argument_list: $ => repeat1($.argument),

    argument: $ => choice(
      seq($.identifier, '?'),
      prec(2, seq($.argument_base, '!')),
      $.argument_base,
    ),

    argument_base: $ => choice(
      $.literal,
      $.identifier,
      $.expression,
      $.block_argument,
      $.uno_literal,
    ),

    block_argument: $ => seq(
      '[',
      repeat($._line_item),
      ']',
    ),

    // @[ control commands ]
    // [ target commands ]
    //
    // The separator may contain zero or more newlines.
    advanced_control: $ => seq(
      '@',
      $.block_argument,
      repeat($.newline),
      $.block_argument,
    ),

    expression: $ => seq(
      '{',
      $._expression,
      '}',
    ),

    // Precedence mirrors the nonterminal chain in parser.y, lowest first.
    // Comparisons do not chain there, so they are prec.left with a
    // single operand on each side.
    _expression: $ => choice(
      $.binary_expression,
      $.unary_expression,
      $._primary_expression,
    ),

    binary_expression: $ => {
      const table = [
        [1, '||'],
        [2, '&&'],
        [3, '|'],
        [4, '^'],
        [5, '&'],
        [6, choice('>', '<', '>=', '<=', '=', '!=')],
        [7, choice('<<', '>>')],
        [8, choice('+', '-')],
        [9, choice('*', '/', '%')],
      ];

      return choice(...table.map(([level, operator]) =>
        prec.left(level, seq(
          field('left', $._expression),
          field('operator', operator),
          field('right', $._expression),
        ))));
    },

    unary_expression: $ => prec(10, seq(
      field('operator', '-'),
      field('operand', $._expression),
    )),

    _primary_expression: $ => choice(
      $._unsigned_literal,
      $.identifier,
      $.parenthesized_expression,
    ),

    parenthesized_expression: $ => seq('(', $._expression, ')'),

    // A negative number is one literal in argument position; inside {...}
    // "-" is unary minus instead.
    literal: $ => choice(
      $._unsigned_literal,
      $.negative_number,
    ),

    negative_number: $ => seq('-', choice($.integer, $.real)),

    _unsigned_literal: $ => choice(
      $.string,
      $.integer,
      $.real,
      $.boolean,
      $.null,
    ),

    identifier: $ => /[a-zA-Z_][a-zA-Z0-9_]*/,

    string: $ => seq(
      '"',
      repeat(choice(
        token.immediate(prec(1, /[^"\\\n]+/)),
        $.escape_sequence,
      )),
      token.immediate('"'),
    ),

    escape_sequence: $ => token.immediate(/\\./),

    uno_literal: $ => token(seq(
      "'",
      repeat(choice(/[^'\\\n]/, /\\./)),
      "'",
    )),

    real: $ => /[0-9]+\.[0-9]+/,
    integer: $ => /[0-9]+/,
    boolean: $ => choice('true', 'false'),
    null: $ => 'null',

    newline: $ => /\n/,

    comment: $ => token(seq('#', /[^\r\n]*/)),

    // $define NAME value / $include "file" / $once
    // $ifdef NAME / $if defined(NAME) / $elif defined NAME / $else / $endif
    preprocessor: $ => seq(
      field('directive', $.preprocessor_directive),
      optional(field('argument', $.preprocessor_argument)),
    ),

    preprocessor_directive: $ => token(seq(
      '$',
      choice('define', 'include', 'once', 'ifdef', 'if', 'elif', 'else', 'endif'),
    )),

    preprocessor_argument: $ => token(prec(-1, /[^\r\n]+/)),
  },
});
