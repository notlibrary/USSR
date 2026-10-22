; USSR Tree-sitter highlight queries

(comment) @comment

; Preprocessor: $define, $include, $once, $ifdef, $if, $elif, $else, $endif
(preprocessor_directive) @keyword.directive
(preprocessor
  directive: (preprocessor_directive) @_d
  argument: (preprocessor_argument) @constant.macro
  (#match? @_d "^\$(ifdef|if|elif|define)$"))
(preprocessor
  directive: (preprocessor_directive) @_d
  argument: (preprocessor_argument) @string.special.path
  (#eq? @_d "$include"))

; Command heads: control flow, the commands that take a [] block, and
; built-ins first; anything else is a user or external command.
((command_head) @keyword.conditional
  (#match? @keyword.conditional "^(if|elif|else)\($"))
((command_head) @keyword.repeat
  (#match? @keyword.repeat "^(while|do|each|map|filter)\($"))
((command_head) @keyword.return
  (#match? @keyword.return "^(break|continue|return)\($"))
((command_head) @keyword.function
  (#match? @keyword.function "^method\($"))
((command_head) @keyword.coroutine
  (#match? @keyword.coroutine "^(async|await|sleep|yield)\($"))
((command_head) @function.builtin
  (#match? @function.builtin "^(set|print|add|sub|mul|div|mod|concat|eval|get|random64|seed_random64|scan|time|cd|chain|file|template|struct|new|getf|setf|vec|push|at|len|encode|decode|global|load|gc)\($"))
(command_head) @function.call

(command result: (identifier) @variable.parameter)
((identifier) @variable.builtin (#eq? @variable.builtin "_"))
(identifier) @variable

(string) @string
(escape_sequence) @string.escape
(uno_literal) @string.special
(integer) @number
(real) @number.float
(negative_number) @number
(boolean) @boolean
(null) @constant.builtin

["[" "]" "{" "}" "(" ")"] @punctuation.bracket
":" @punctuation.delimiter
"@" @keyword

(binary_expression operator: _ @operator)
(unary_expression operator: _ @operator)

; Hash-variable sigils: NAME? reads, ARG! stores.
["?" "!"] @punctuation.special
