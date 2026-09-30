/**
 * Tree-sitter grammar for USSR (Unified Shell Script REPL).
 *
 * Derived from the current Flex lexer and Bison grammar:
 *   src/lexer.l
 *   src/parser.y / generated parser.tab.c
 */
module.exports = grammar({
  name: 'ussr',

  extras: $ => [
    /[ \t\r]+/,
    $.comment,
  ],

  word: $ => $.identifier,

  rules: {
    source_file: $ => seq(
      repeat($.newline),
      optional($.command_sequence),
      repeat($.newline),
    ),

    command_sequence: $ => seq(
      $.command_line,
      repeat(seq($.newline, $.command_line)),
    ),

    command_line: $ => choice(
      $.command,
      $.advanced_control,
    ),

    // The Flex lexer recognizes COMMAND_HEAD as everything up to and
    // including the opening parenthesis. Keeping that as one token avoids
    // lexical ambiguity between command names and ordinary identifiers.
    command: $ => seq(
      $.command_head,
      $.identifier,
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
      repeat($.command_line_with_newline),
      optional($.newline),
      ']',
    ),

    command_line_with_newline: $ => seq(
      $.command_line,
      $.newline,
    ),

    // The @ form is a syntactically distinct command-list pipeline:
    //
    // @[ control commands ]
    // [ target commands ]
    //
    // The separator may contain zero or more newlines.
    advanced_control: $ => seq(
      '@',
      $.block_contents,
      repeat($.newline),
      $.block_contents,
    ),

    block_contents: $ => seq(
      '[',
      repeat($.command_line_with_newline),
      optional($.newline),
      ']',
    ),

    expression: $ => seq(
      '{',
      $.logical_or_expression,
      '}',
    ),

    logical_or_expression: $ => prec.left(1, seq(
      $.logical_and_expression,
      repeat(seq('||', $.logical_and_expression)),
    )),

    logical_and_expression: $ => prec.left(2, seq(
      $.bitwise_or_expression,
      repeat(seq('&&', $.bitwise_or_expression)),
    )),

    bitwise_or_expression: $ => prec.left(3, seq(
      $.bitwise_xor_expression,
      repeat(seq('|', $.bitwise_xor_expression)),
    )),

    bitwise_xor_expression: $ => prec.left(4, seq(
      $.bitwise_and_expression,
      repeat(seq('^', $.bitwise_and_expression)),
    )),

    bitwise_and_expression: $ => prec.left(5, seq(
      $.comparison_expression,
      repeat(seq('&', $.comparison_expression)),
    )),

    comparison_expression: $ => prec.left(6, seq(
      $.shift_expression,
      repeat(seq(
        choice('>', '<', '>=', '<=', '=', '!='),
        $.shift_expression,
      )),
    )),

    shift_expression: $ => prec.left(7, seq(
      $.additive_expression,
      repeat(seq(choice('<<', '>>'), $.additive_expression)),
    )),

    additive_expression: $ => prec.left(8, seq(
      $.multiplicative_expression,
      repeat(seq(choice('+', '-'), $.multiplicative_expression)),
    )),

    multiplicative_expression: $ => prec.left(9, seq(
      $.unary_expression,
      repeat(seq(choice('*', '/', '%'), $.unary_expression)),
    )),

    unary_expression: $ => choice(
      $.primary_expression,
      prec(10, seq('-', $.unary_expression)),
    ),

    primary_expression: $ => choice(
      $.literal,
      $.identifier,
      seq('(', $.logical_or_expression, ')'),
    ),

    literal: $ => choice(
      $.string,
      $.integer,
      $.real,
      $.boolean,
      $.null,
    ),

    identifier: $ => /[a-zA-Z_][a-zA-Z0-9_]*/,

    string: $ => token(seq(
      '"',
      repeat(choice(/[^"\\\n]/, /\\./)),
      '"',
    )),

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
  },
});
