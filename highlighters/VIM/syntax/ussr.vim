" Vim syntax file
" Language:    USSR (Unified Shell Script)
" Filenames:   *.su

if exists("b:current_syntax")
  finish
endif

let s:cpo_save = &cpo
set cpo&vim

" ---------------------------------------------------------------
" Comments
" ---------------------------------------------------------------
syn keyword ussrTodo contained TODO FIXME XXX NOTE
syn match   ussrComment "#.*$" contains=ussrTodo,@Spell

" ---------------------------------------------------------------
" Preprocessor: $define / $include / $once / $ifdef NAME /
" $if defined(NAME) / $elif defined NAME / $else / $endif
" ---------------------------------------------------------------
syn match ussrPreCondit "^\s*\$\(define\|include\|once\|ifdef\|if\|elif\|else\|endif\)\>"
syn match ussrDefined   "\<defined\>"
syn match ussrPreParen  "\(\<defined\s*(\=\s*\)\@<=\h\w*"
syn match ussrPreParen  "\(^\s*\$ifdef\s\+\)\@<=\h\w*"

" ---------------------------------------------------------------
" Strings: "..." (ordinary), '...' (UNO literal), `...` (UNO-internal
" string, only meaningful inside a UNO literal -- see the USSR manual
" section 8.4 on the three non-colliding quote levels)
" ---------------------------------------------------------------
syn region ussrString     start=/"/ skip=/\\./ end=/"/ contains=ussrEscape,@Spell
syn match  ussrEscape     contained /\\./

syn match  ussrUnoString  contained "`[^`]*`"
syn region ussrUnoLiteral start=/'/ end=/'/ contains=ussrUnoString,ussrNumber,ussrBoolean,ussrNull

" ---------------------------------------------------------------
" Numbers, booleans, null
" ---------------------------------------------------------------
syn match   ussrNumber  "\<\d\+\(\.\d\+\)\=\>"
syn keyword ussrBoolean true false
syn keyword ussrNull    null

" ---------------------------------------------------------------
" Command call syntax: name(return_var): args...
" Every command -- builtin, user-defined, or external -- uses this
" same shape. The head is the lexer's COMMAND_HEAD: any run of
" [A-Za-z0-9_./\:-] directly followed by "(", so a path such as
" ./tools/run( is one name.
" ---------------------------------------------------------------
syn match ussrCommandName "[A-Za-z0-9_./\:-]\@1<![A-Za-z0-9_./\:-]\+\ze("
syn match ussrReturnVar   "\((\s*\)\@<=[A-Za-z_][A-Za-z0-9_]*\ze\s*)"

" ---------------------------------------------------------------
" Reserved command names, only as command heads (a variable named
" map stays a variable). Defined after ussrCommandName so they win.
" Control flow is compiled to jumps/CALL-RET -- see the VM spec.
" ---------------------------------------------------------------
syn match ussrConditional "\<\(if\|elif\|else\)\ze("
syn match ussrRepeat      "\<\(while\|do\|each\|map\|filter\)\ze("
syn match ussrStatement   "\<\(break\|continue\|return\|method\)\ze("
syn match ussrScheduler   "\<\(async\|await\|sleep\|yield\|gc\|load\)\ze("
syn match ussrBuiltin     "\<\(set\|print\|concat\|add\|sub\|mul\|div\|mod\|eval\)\ze("
syn match ussrBuiltin     "\<\(get\|scan\|time\|random64\|seed_random64\|cd\|chain\|file\|template\|global\)\ze("
syn match ussrOOP         "\<\(struct\|new\|getf\|setf\|vec\|push\|at\|len\|encode\|decode\)\ze("

" @[ ... ] [ ... ] advanced control, and the hash sigils: NAME? reads
" a hash variable, ARG! stores into one.
syn match ussrAdvanced    "@\ze\s*\["
syn match ussrHashSigil   "[?!]"

" ---------------------------------------------------------------
" Operators / delimiters
" ---------------------------------------------------------------
" Exactly the lexer's operators: "=" is equality, there is no "==".
syn match ussrOperator  "&&\|||\|!=\|>=\|<=\|<<\|>>"
syn match ussrOperator  "[-+*/%<>=&|^]"
syn match ussrDelimiter "[(){}\[\]:]"

" A leading "-" is part of the number in argument position (after
" whitespace or the ':'); defined after the operators so it wins there.
syn match ussrNumber    "\([[:space:]:]\)\@1<=-\d\+\(\.\d\+\)\=\>"

" ---------------------------------------------------------------
" Folding: [ ... ] blocks (loop/function bodies, and optionally the
" whole program) -- enable with :setlocal foldmethod=syntax
" ---------------------------------------------------------------
syn region ussrBlock start="\[" end="\]" transparent fold

" ---------------------------------------------------------------
" Highlight group links
" ---------------------------------------------------------------
hi def link ussrComment     Comment
hi def link ussrTodo        Todo
hi def link ussrPreCondit   PreCondit
hi def link ussrDefined     PreProc
hi def link ussrPreParen    Identifier
hi def link ussrString      String
hi def link ussrEscape      SpecialChar
hi def link ussrUnoLiteral  Special
hi def link ussrUnoString   String
hi def link ussrNumber      Number
hi def link ussrBoolean     Boolean
hi def link ussrNull        Constant
hi def link ussrConditional Conditional
hi def link ussrRepeat      Repeat
hi def link ussrStatement   Statement
hi def link ussrHashSigil   Special
hi def link ussrAdvanced    Keyword
hi def link ussrScheduler   Keyword
hi def link ussrBuiltin     Function
hi def link ussrOOP         Function
hi def link ussrCommandName Function
hi def link ussrReturnVar   Identifier
hi def link ussrOperator    Operator
hi def link ussrDelimiter   Delimiter

let b:current_syntax = "ussr"

let &cpo = s:cpo_save
unlet s:cpo_save
