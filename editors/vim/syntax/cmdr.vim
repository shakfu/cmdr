" Vim syntax for cmdr scripts. Token rules follow the lexer in src/cmdr.c.
if exists('b:current_syntax') | finish | endif

" The first word of a stage: at line start, or after an unescaped '|', a '{'
" that starts a lexeme, a '$(', or a parameter list. No command name holds '('
" or ')'.
syn match cmdrCommand /\%(^\|^|\|[^\\]|\|\%(^\|[ \t|]\){\|\$(\)\@3<=\s*\zs[^ \t|{}()'"$\\]\+/
syn match cmdrKeyword /\%(^\|^|\|[^\\]|\|\%(^\|[ \t|]\){\)\@3<=\s*\zsdef\ze\%(\s\|$\)/
      \ nextgroup=cmdrDefName skipwhite
syn match cmdrDefName /[^ \t|{}()'"$\\]\+/ contained

syn match cmdrPipe /|/
" Braces inside a word are text; in a block or sub-pipeline they balance, so
" a{b} does not end the block.
syn region cmdrWordBrace start=/\%(^\|[ \t|{]\|\$(\)\@2<!{/ end=/}/ contained transparent
      \ contains=TOP containedin=cmdrBlock,cmdrSub,cmdrParen,cmdrWordBrace
" A block where a lexeme starts, so a ')' inside it cannot end an enclosing $(.
syn region cmdrBlock matchgroup=cmdrBrace start=/\%(^\|[ \t|{]\|\$(\)\@2<={/ end=/}/
      \ contains=TOP transparent
syn region cmdrParams matchgroup=cmdrBrace start=/{\@1<=\s*(/ end=/)/ oneline
      \ contains=cmdrParam,cmdrDefault nextgroup=cmdrCommand skipwhite
syn match cmdrParam /\a[A-Za-z0-9_-]*\|\<_\>/ contained
" A parameter's default, from '=' to the next space or ')'.
syn match cmdrDefault /=[^ \t)]*/ contained
" After a parameter list a '#' starts a comment, not a command.
syn match cmdrCommand /#\@![^ \t|{}()'"$\\]\+/ contained

" A sub-pipeline: '$(' to the ')' that balances it, as lex_sub reads it.
syn region cmdrSub matchgroup=cmdrSubDelim start=/\$(/ end=/)/ contains=TOP
" contains=TOP,X would exclude X, so cmdrParen adds itself with containedin.
syn region cmdrParen start=/(/ end=/)/ contained transparent contains=TOP
      \ containedin=cmdrSub,cmdrParen

" A reference starts a lexeme and may name a module's binding: $NAME.DEF.
" Inside a word, as in a$x, '$' is text.
syn match cmdrReference /\%(^\|[ \t|{]\|\$(\)\@2<=\$\a[A-Za-z0-9_-]*\%(\.\a[A-Za-z0-9_-]*\)*/
syn match cmdrEscape /\\./
" A backslash before a newline is an error, not a continuation.
syn match cmdrEscapeError /\\$/
syn region cmdrString start=/'/ end=/'/ oneline
syn region cmdrString start=/"/ skip=/\\./ end=/"/ oneline contains=cmdrStringEscape,cmdrEscapeError
syn match cmdrStringEscape /\\./ contained
syn match cmdrNumber /\%(^\|[ \t]\)\@1<=-\=\d\+\%(\.\d\+\)\=\%([eE][-+]\=\d\+\)\=\ze\%([ \t|})]\|$\)/

" A comment is '#' where a line or a body starts: after '{', a block's
" parameters, or '$('. It runs to the line's end, brackets included; '#'
" elsewhere is text. Defined last, so it wins over a command at its position.
syn match cmdrComment /^\s*#.*$/ contains=@Spell
syn match cmdrComment /\%(\\\@1<!{\s*\%(([^)]*)\s*\)\=\|\$(\s*\)\@<=#.*$/ contains=@Spell

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
hi def link cmdrEscapeError Error
hi def link cmdrString String
hi def link cmdrStringEscape SpecialChar
hi def link cmdrNumber Number

" Blocks and sub-pipelines span lines, so highlight from the top: scripts are
" short, and syncing partway loses the region a line is in.
syn sync fromstart

let b:current_syntax = 'cmdr'
