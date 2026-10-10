(comment) @comment
(html_comment) @comment
(doctype) @constant
(cdata) @string
(tag_name) @tag
(attribute_name) @attribute
(quoted_attribute_value) @string
(unquoted_attribute_value) @string
(html_character_reference) @string.special
(identifier) @variable
(property_identifier) @property
(type_identifier) @type
(predefined_type) @type.builtin
(string) @string
(template_string) @string
(regex) @string.regexp
(number) @number
[(true) (false)] @boolean
(null) @constant.builtin
(function_declaration name: (identifier) @function)
(call_expression function: (identifier) @function.call)
(call_expression function: (member_expression property: (property_identifier) @function.method.call))
["import" "export" "from" "as" "const" "let" "var" "function" "return"
 "if" "else" "for" "while" "do" "switch" "case" "try" "catch" "finally"
 "throw" "await" "async" "class" "new" "interface" "type" "extends"
 "satisfies" "using"] @keyword
["---" "<" ">" "</" "/>" "<>" "</>" "{" "}" "(" ")" "[" "]"] @punctuation.bracket
