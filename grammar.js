/**
 * Astro's template syntax layered on the upstream TypeScript/TSX grammar.
 * JS/TS is parsed, not guessed or split into disconnected expression snippets.
 * The scanner handles only Astro fences, HTML tag state, and raw text.
 */
const TSX = require('tree-sitter-typescript/tsx/grammar');

module.exports = grammar(TSX, {
  name: 'astro',

  externals: ($, previous) => previous.concat([
    $._frontmatter_start,
    $._frontmatter_end,
    $._start_tag_name,
    $._end_tag_name,
    $._self_closing_delimiter,
    $._implicit_end_tag,
    $._fragment_open,
    $._fragment_close,
    $._raw_attribute,
    $.raw_text,
    $._return_asi,
  ]),

  conflicts: ($, previous) => previous
    .filter(group => !group.some(rule => ['jsx_namespace_name', 'jsx_opening_element'].includes(rule.name)))
    .concat([
      [$.primary_type, $.type_parameter],
      [$.expression, $.sibling_elements],
    ]),

  rules: {
    program: $ => seq(
      repeat($._jsx_child),
      optional(seq($.frontmatter, repeat($._jsx_child))),
    ),

    frontmatter: $ => seq(
      alias($._frontmatter_start, '---'),
      repeat($.statement),
      alias($._frontmatter_end, '---'),
    ),

    // JavaScript 0.25 has real using declarations; remove the older TS
    // grammar's assignment-expression shortcut for that syntax.
    assignment_expression: $ => prec.right('assign', seq(
      field('left', choice($.parenthesized_expression, $._lhs_expression)),
      '=', field('right', $.expression),
    )),

    return_statement: $ => seq(
      'return',
      choice(
        alias($._return_asi, ';'),
        seq(optional($._expressions), $._semicolon),
      ),
    ),

    // The scanner disables Astro tag names inside frontmatter. This allows
    // TypeScript angle-bracket assertions without confusing them with tags.
    expression: ($, previous) => choice(previous, $.type_assertion),

    _jsx_element: $ => choice(
      $.jsx_element,
      $.jsx_self_closing_element,
      $.fragment,
    ),

    _jsx_child: $ => choice(
      $.jsx_text,
      $.html_character_reference,
      $._jsx_element,
      $.jsx_expression,
      $.html_comment,
      $.doctype,
      $.cdata,
      $.raw_text,
    ),

    jsx_element: $ => seq(
      field('open_tag', $.jsx_opening_element),
      repeat($._jsx_child),
      choice(field('close_tag', $.jsx_closing_element), $._implicit_end_tag),
    ),

    _jsx_start_opening_element: $ => seq(
      '<',
      field('name', alias($._start_tag_name, $.tag_name)),
      repeat(field('attribute', $._jsx_attribute)),
    ),

    jsx_opening_element: $ => seq($._jsx_start_opening_element, '>'),

    jsx_self_closing_element: $ => seq(
      $._jsx_start_opening_element,
      alias($._self_closing_delimiter, '/>'),
    ),

    jsx_closing_element: $ => seq(
      '</', field('name', alias($._end_tag_name, $.tag_name)), '>',
    ),

    fragment: $ => seq(
      alias($._fragment_open, '<>'),
      repeat($._jsx_child),
      alias($._fragment_close, '</>'),
    ),

    jsx_expression: $ => seq(
      '{',
      optional(choice($.expression, $.sequence_expression, $.spread_element, $.sibling_elements)),
      '}',
    ),

    sibling_elements: $ => seq($._jsx_element, repeat1($._jsx_element)),

    _jsx_attribute: $ => choice(
      $.jsx_attribute, $.shorthand_attribute, $.spread_attribute,
    ),

    jsx_attribute: $ => seq(
      field('name', choice($.attribute_name, alias($._raw_attribute, $.attribute_name))),
      optional(seq('=', field('value', choice(
        $.quoted_attribute_value,
        $.unquoted_attribute_value,
        $.template_string,
        $.jsx_expression,
      )))),
    ),

    attribute_name: _ => /[^\s\x00-\x1f"'>/={}]+/,
    // A slash belongs to a URL unless it is the self-closing delimiter.
    unquoted_attribute_value: _ => token(repeat1(choice(
      /[^\s\x00-\x1f"'`=<>/{}]/,
      seq('/', /[^\s\x00-\x1f"'`=<>{}]/),
    ))),

    quoted_attribute_value: $ => choice(
      seq('"', optional(alias(token.immediate(/[^"\x00]+/), $.attribute_value)), '"'),
      seq("'", optional(alias(token.immediate(/[^'\x00]+/), $.attribute_value)), "'"),
    ),

    shorthand_attribute: $ => seq('{', optional($.expression), '}'),
    spread_attribute: $ => seq('{', '...', field('value', $.expression), '}'),

    cdata: _ => token(seq(
      '<![CDATA[', repeat(choice(/[^\]]/, seq(/\]+/, /[^>\]]/))), /\]*/, ']]>',
    )),

    doctype: _ => token(seq('<!', /[Dd][Oo][Cc][Tt][Yy][Pp][Ee]/, /[^>]+/, '>')),
  },
});
