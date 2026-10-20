" Vim syntax for cmdr scripts. Token rules follow the lexer in src/cmdr.c.
if exists('b:current_syntax') | finish | endif

" The first word of a stage: at line start, or after an unescaped '|', a '{',
" a '$(', or a parameter list. No command name holds '(' or ')'.
syn match cmdrCommand /\%(^\|^[|{]\|[^\\][|{]\|\$(\)\@2<=\s*\zs[^ \t|{}()'"$\\]\+/
syn match cmdrKeyword /\%(^\|^[|{]\|[^\\][|{]\)\@2<=\s*\zsdef\ze\%(\s\|$\)/
      \ nextgroup=cmdrDefName skipwhite
syn match cmdrDefName /[^ \t|{}()'"$\\]\+/ contained

syn match cmdrPipe /|/
syn match cmdrBrace /[{}]/
syn region cmdrParams matchgroup=cmdrBrace start=/{\@1<=\s*(/ end=/)/ oneline
      \ contains=cmdrParam,cmdrDefault nextgroup=cmdrCommand skipwhite
syn match cmdrParam /\a[A-Za-z0-9_-]*\|\<_\>/ contained
" A parameter's default, from '=' to the next space or ')'.
syn match cmdrDefault /=[^ \t)]*/ contained
syn match cmdrCommand /[^ \t|{}()'"$\\]\+/ contained

" A sub-pipeline: '$(' to the ')' that balances it, as lex_sub reads it.
syn region cmdrSub matchgroup=cmdrSubDelim start=/\$(/ end=/)/ contains=TOP
" contains=TOP,X would exclude X, so cmdrParen adds itself with containedin.
syn region cmdrParen start=/(/ end=/)/ contained transparent contains=TOP
      \ containedin=cmdrSub,cmdrParen

" A reference may name a module's binding: $NAME.DEF.
syn match cmdrReference /\$\a[A-Za-z0-9_-]*\%(\.\a[A-Za-z0-9_-]*\)*/
syn match cmdrEscape /\\./
syn region cmdrString start=/'/ end=/'/ oneline
syn region cmdrString start=/"/ skip=/\\./ end=/"/ oneline contains=cmdrStringEscape
syn match cmdrStringEscape /\\./ contained
syn match cmdrNumber /\%(^\|[ \t]\)\@1<=-\=\d\+\%(\.\d\+\)\=\%([eE][-+]\=\d\+\)\=\ze\%([ \t|})]\|$\)/

" A comment is a whole line; '#' later in a line is text. Defined last, so it
" wins over a command at the same position.
syn match cmdrComment /^\s*#.*$/ contains=@Spell

hi def link cmdrComment Comment
hi def link cmdrCommand Function
hi def link cmdrKeyword Keyword
hi def link cmdrDefName Identifier
hi def link cmdrPipe Operator
hi def link cmdrBrace Delimiter
hi def link cmdrSubDelim Delimiter
hi def link cmdrParam Identifier
hi def link cmdrDefault Constant
hi def link cmdrReference Identifier
hi def link cmdrEscape SpecialChar
hi def link cmdrString String
hi def link cmdrStringEscape SpecialChar
hi def link cmdrNumber Number

let b:current_syntax = 'cmdr'
