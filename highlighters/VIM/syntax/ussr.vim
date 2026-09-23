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
" Preprocessor: $define / $include / $ifdef / $if defined(...) / $else / $endif
" ---------------------------------------------------------------
syn match ussrPreCondit "^\s*\$\(define\|include\|ifdef\|if\|else\|endif\)\>"
syn match ussrDefined   "\<defined\>"
syn match ussrPreParen  "defined(\zs[A-Za-z_][A-Za-z0-9_]*\ze)"

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
syn match   ussrNumber  "\<-\?\d\+\(\.\d\+\)\?\>"
syn keyword ussrBoolean true false
syn keyword ussrNull    null

" ---------------------------------------------------------------
" Control flow (compiled to jumps/CALL-RET -- see the VM spec) and
" the "hash" sigils
" ---------------------------------------------------------------
syn keyword ussrConditional if else
syn keyword ussrRepeat      while
syn keyword ussrStatement   break continue return
syn match   ussrHashSigil   "[?!]"

" ---------------------------------------------------------------
" Builtin commands
" ---------------------------------------------------------------
syn keyword ussrBuiltin set print concat add sub mul div mod eval
syn keyword ussrBuiltin get scan time random64 seed_random64 cd
syn keyword ussrOOP     struct new getf setf vec push at len encode decode

" ---------------------------------------------------------------
" Command call syntax: name(return_var): args...
" Every command -- builtin, user-defined, or external -- uses this
" same shape, so this highlights the name and return slot uniformly;
" the keyword groups above take priority for the reserved names.
" ---------------------------------------------------------------
syn match ussrCommandName "\<\h\w*\>\ze\s*("
syn match ussrReturnVar   "(\zs\s*[A-Za-z_][A-Za-z0-9_]*\s*\ze)"

" ---------------------------------------------------------------
" Operators / delimiters
" ---------------------------------------------------------------
syn match ussrOperator  "&&\|||\|==\|!=\|>=\|<=\|<<\|>>"
syn match ussrOperator  "[-+*/%<>&|^~]"
syn match ussrDelimiter "[(){}\[\]:;,]"

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
hi def link ussrBuiltin     Function
hi def link ussrOOP         Function
hi def link ussrCommandName Function
hi def link ussrReturnVar   Identifier
hi def link ussrOperator    Operator
hi def link ussrDelimiter   Delimiter

let b:current_syntax = "ussr"

let &cpo = s:cpo_save
unlet s:cpo_save
