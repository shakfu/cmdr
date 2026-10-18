" Vim syntax for cmdr scripts. Token rules follow the lexer in src/cmdr.c.
if exists('b:current_syntax') | finish | endif

" The first word of a stage: at line start, or after an unescaped '|', a '{',
" or a parameter list.
syn match cmdrCommand /\%(^\|^[|{]\|[^\\][|{]\)\@2<=\s*\zs[^ \t|{}'"$\\]\+/
syn match cmdrKeyword /\%(^\|^[|{]\|[^\\][|{]\)\@2<=\s*\zsdef\ze\%(\s\|$\)/
      \ nextgroup=cmdrDefName skipwhite
syn match cmdrDefName /[^ \t|{}'"$\\]\+/ contained

syn match cmdrPipe /|/
syn match cmdrBrace /[{}]/
syn region cmdrParams matchgroup=cmdrBrace start=/{\@1<=\s*(/ end=/)/ oneline
      \ contains=cmdrParam nextgroup=cmdrCommand skipwhite
syn match cmdrParam /\a[A-Za-z0-9_-]*/ contained
syn match cmdrCommand /[^ \t|{}'"$\\]\+/ contained

syn match cmdrReference /\$\a[A-Za-z0-9_-]*/
syn match cmdrEscape /\\./
syn region cmdrString start=/'/ end=/'/ oneline
syn region cmdrString start=/"/ skip=/\\./ end=/"/ oneline contains=cmdrStringEscape
syn match cmdrStringEscape /\\./ contained
syn match cmdrNumber /\%(^\|[ \t]\)\@1<=-\=\%(0[xX]\x\+\|\d\+\%(\.\d\+\)\=\%([eE][-+]\=\d\+\)\=\)\ze\%([ \t|}]\|$\)/

" A comment is a whole line; '#' later in a line is text. Defined last, so it
" wins over a command at the same position.
syn match cmdrComment /^\s*#.*$/ contains=@Spell

hi def link cmdrComment Comment
hi def link cmdrCommand Function
hi def link cmdrKeyword Keyword
hi def link cmdrDefName Identifier
hi def link cmdrPipe Operator
hi def link cmdrBrace Delimiter
hi def link cmdrParam Identifier
hi def link cmdrReference Identifier
hi def link cmdrEscape SpecialChar
hi def link cmdrString String
hi def link cmdrStringEscape SpecialChar
hi def link cmdrNumber Number

let b:current_syntax = 'cmdr'
