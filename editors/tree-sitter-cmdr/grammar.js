/**
 * Tree-sitter grammar for cmdr scripts. It follows the lexer, parser, and
 * compiler in src/cmdr.c: words, quotes, escapes, references, blocks with
 * parameters, and sub-pipelines, in pipelines that newlines end. A backslash
 * before a newline is an error, since /\\./ does not match a newline.
 */

// Words, by context. None starts with a quote, escape, '{', '(', pipe, or
// space, nor with '$', which starts a reference or sub-pipeline; a word
// cannot start with '(', which would hide a block's parameters. '$' and '#'
// are literal inside a word: a$x. Escapes, /\\./, may appear anywhere.
// - At top level, braces and parentheses inside a word are text: a{b x).
// - In a block, parentheses are text, and braces inside a word balance: a{b}.
// - In a sub-pipeline, parentheses nest, so a word holds none; braces balance.
const START = /[^\s|{}('"\\$]/;
const SUB_START = /[^\s|{}()'"\\$]/;
const TOP_REST = /[^\s|'"\\]/;
const BLOCK_REST = choice(/[^\s|{}'"\\]/, /\{[^\s|{}'"\\]*\}/);
const SUB_REST = choice(/[^\s|{}()'"\\]/, /\{[^\s|{}()'"\\]*\}/);
const ESCAPE = /\\./;

// A command and its arguments, or a lone reference, which makes its value the
// result. A sub-pipeline cannot be a command.
const stage = ($, word, argument) => choice(
  seq(field('command', word), repeat(argument)),
  field('value', $.reference),
);

const argument = ($, word, ...more) => choice(
  word, $.number, $.raw_string, $.string, $.reference, $.block, $.sub_pipeline, ...more,
);

const NAME = /[A-Za-z][A-Za-z0-9_-]*/;

module.exports = grammar({
  name: 'cmdr',

  // Newlines end a pipeline, so only spaces, tabs, and carriage returns are
  // free between tokens.
  extras: _ => [/[ \t\r]/],

  // After a body's stage, a newline may lead to '|' or to the body's end;
  // only the token after the gap decides.
  conflicts: $ => [[$.body_pipeline], [$.sub_body]],

  rules: {
    // Lines, each a pipeline, a comment, or blank; the last may lack a newline.
    source_file: $ => seq(
      repeat(seq(optional($._line), $._newline)),
      optional($._line),
    ),

    _line: $ => choice($.pipeline, $.comment),

    // A comment is '#' where a line or a body starts: after '{', a block's
    // parameters, or '$('. It runs to the line's end and hides brackets and
    // quotes, so { # note } leaves the block open. Elsewhere '#' is text.
    comment: _ => token(prec(2, /#[^\n]*/)),

    _newline: _ => /\n/,

    // A '|' may end a line; blank and comment lines after it are skipped. A
    // '#' on the '|' line itself is text.
    pipeline: $ => seq(
      $.stage,
      repeat(seq('|', repeat(seq($._newline, optional($.comment))), $.stage)),
    ),

    stage: $ => stage($, $.word, $._argument),
    _argument: $ => argument($, $.word),

    block_stage: $ => stage($, alias($.block_word, $.word), $._block_argument),
    _block_argument: $ => argument($, alias($.block_word, $.word)),

    sub_stage: $ => stage($, alias($.sub_word, $.word), $._sub_argument),
    _sub_argument: $ => argument($, alias($.sub_word, $.word), $.parenthesized),

    // Defined before word: a token both match at the same length, such as 12,
    // goes to the earlier rule, while a longer word, such as 12a or 8k, wins.
    // Precedence would rank above length, splitting 12a.
    // Decimal only, as cmdr reads numbers: 0x10 is a word.
    number: _ => /-?\d+(\.\d+)?([eE][-+]?\d+)?/,

    // Bare text and escapes, without spaces between them: a\|b is one word.
    // One token, so a missing command shows as (MISSING word).
    word: _ => token(seq(choice(START, ESCAPE), repeat(choice(TOP_REST, ESCAPE)))),
    block_word: _ => token(seq(choice(START, ESCAPE), repeat(choice(BLOCK_REST, ESCAPE)))),
    sub_word: _ => token(seq(choice(SUB_START, ESCAPE), repeat(choice(SUB_REST, ESCAPE)))),

    // Single quotes keep everything, backslashes included.
    raw_string: _ => /'[^'\n]*'/,

    // Double quotes keep spaces and pipes; \n and \t are a newline and a tab,
    // and a backslash escapes any other character.
    string: $ => seq(
      '"',
      repeat(choice(
        alias(token.immediate(prec(1, /[^"\\\n]+/)), $.string_content),
        alias(token.immediate(/\\./), $.escape_sequence),
      )),
      token.immediate('"'),
    ),

    // $name, or $MODULE.NAME for a module's binding.
    reference: _ => token(seq('$', NAME, repeat(seq('.', NAME)))),

    // A body may span lines: a newline, or a comment line, goes next to '|'
    // or at either end, so a missing '|' is an error, as in cmdr.
    block: $ => seq(
      '{',
      optional($.parameters),
      optional($.comment),
      optional($._gap),
      optional(seq(alias($.body_pipeline, $.pipeline), optional($._gap))),
      '}',
    ),

    // Inside a body, a line may also start with '|'.
    body_pipeline: $ => seq(
      alias($.block_stage, $.stage),
      repeat(seq(optional($._gap), '|', optional($._gap), alias($.block_stage, $.stage))),
    ),

    // Newlines, each followed by a comment or nothing: a comment starts a line.
    _gap: $ => repeat1(seq($._newline, optional($.comment))),

    // "(a b)" directly after '{'. _ is a parameter the block ignores; a
    // parameter after the first may have a default, one word or "".
    parameters: $ => seq('(', repeat($.parameter), ')'),

    parameter: $ => seq(
      field('name', alias(choice(NAME, '_'), $.identifier)),
      optional(seq(token.immediate('='), field('default', $.default))),
    ),

    default: _ => token.immediate(/[^\s)]*/),

    // An argument whose value is PIPELINE's result.
    sub_pipeline: $ => seq(
      '$(',
      optional($.comment),
      optional($._gap),
      alias($.sub_body, $.pipeline),
      optional($._gap),
      ')',
    ),

    sub_body: $ => seq(
      alias($.sub_stage, $.stage),
      repeat(seq(optional($._gap), '|', optional($._gap), alias($.sub_stage, $.stage))),
    ),

    // Literal parentheses, which balance, so $(text (a)) ends at its own ')'.
    parenthesized: $ => seq('(', repeat($._sub_argument), ')'),
  },
});
