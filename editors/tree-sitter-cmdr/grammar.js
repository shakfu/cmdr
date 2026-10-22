/**
 * Tree-sitter grammar for cmdr scripts. It follows the lexer, parser, and
 * compiler in src/cmdr.c: words, quotes, escapes, references, blocks with
 * parameters, and sub-pipelines, in pipelines that newlines end.
 */

// A bare word's first character: not a quote, escape, brace, parenthesis,
// pipe, or space, and not '$', which starts a reference or sub-pipeline.
const WORD_START = /[^\s|{}()'"\\$]/;
// Characters after the first. '$' and '#' are literal inside a word: a$x.
const WORD_REST = /[^\s|{}()'"\\]/;
const NAME = /[A-Za-z][A-Za-z0-9_-]*/;

module.exports = grammar({
  name: 'cmdr',

  // Newlines end a pipeline, so only spaces, tabs, and carriage returns are
  // free between tokens.
  extras: _ => [/[ \t\r]/],

  // After a body's stage, a newline may lead to '|' or to the body's end;
  // only the token after the gap decides.
  conflicts: $ => [[$.body_pipeline]],

  rules: {
    // Lines, each a pipeline, a comment, or blank; the last may lack a newline.
    source_file: $ => seq(
      repeat(seq(optional($._line), $._newline)),
      optional($._line),
    ),

    _line: $ => choice($.pipeline, $.comment),

    // A comment is a whole line: '#' where a command could start. Later in a
    // line, '#' is text, and the word rule takes it.
    comment: _ => token(prec(2, /#[^\n]*/)),

    _newline: _ => /\n/,

    // A '|' may end a line; blank and comment lines after it are skipped.
    pipeline: $ => seq(
      $.stage,
      repeat(seq('|', repeat(choice($._newline, $.comment)), $.stage)),
    ),

    // A command and its arguments, or a lone reference, which makes its value
    // the result. A sub-pipeline cannot be a command.
    stage: $ => choice(
      seq(field('command', $.word), repeat($._argument)),
      field('value', $.reference),
    ),

    _argument: $ => choice(
      $.word,
      $.number,
      $.raw_string,
      $.string,
      $.reference,
      $.block,
      $.sub_pipeline,
      $.parenthesized,
    ),

    // Defined before word: a token both match at the same length, such as 12,
    // goes to the earlier rule, while a longer word, such as 12a or 8k, wins.
    // Precedence would rank above length, splitting 12a.
    // Decimal only, as cmdr reads numbers: 0x10 is a word.
    number: _ => /-?\d+(\.\d+)?([eE][-+]?\d+)?/,

    // Bare text and escapes, without spaces between them: a\|b is one word.
    // One token, so a missing command shows as (MISSING word).
    word: _ => token(seq(
      choice(WORD_START, /\\./),
      repeat(choice(WORD_REST, /\\./)),
    )),

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
      optional($._gap),
      optional(seq(alias($.body_pipeline, $.pipeline), optional($._gap))),
      '}',
    ),

    // Inside a body, a line may also start with '|'.
    body_pipeline: $ => seq(
      $.stage,
      repeat(seq(optional($._gap), '|', optional($._gap), $.stage)),
    ),

    _gap: $ => repeat1(choice($._newline, $.comment)),

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
      optional($._gap),
      alias($.body_pipeline, $.pipeline),
      optional($._gap),
      ')',
    ),

    // Literal parentheses, which balance, so $(text (a)) ends at its own ')'.
    parenthesized: $ => seq('(', repeat($._argument), ')'),
  },
});
