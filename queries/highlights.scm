; USSR Tree-sitter highlight queries

(comment) @comment

(command_head) @function

(identifier) @variable

(string) @string
(uno_literal) @string.special
(integer) @number
(real) @number.float
(boolean) @boolean
(null) @constant.builtin

["@" "[" "]" "{" "}" "(" ")" ":"] @punctuation.bracket

["+" "-" "*" "/" "%" "&&" "||" "^" "&" "|" "<<" ">>"
 ">" "<" ">=" "<=" "=" "!"] @operator

["?" "!"] @punctuation.special
