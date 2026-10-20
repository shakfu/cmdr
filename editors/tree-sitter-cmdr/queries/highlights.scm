; Highlights for cmdr, in the capture names nvim-treesitter uses. Each node
; matches one pattern, so editors that let the first or the last pattern win
; agree.

(comment) @comment

; The first word of a stage is a command; def names the command it defines.
((stage command: (word) @function.call)
 (#not-eq? @function.call "def"))

((stage
   command: (word) @keyword.function
   .
   (word) @function)
 (#eq? @keyword.function "def"))

(reference) @variable

(parameter name: (identifier) @variable.parameter)
(parameter default: (default) @constant)

[(string) (raw_string)] @string
(escape_sequence) @string.escape

(number) @number

"|" @operator

["{" "}"] @punctuation.bracket
(parameters ["(" ")"] @punctuation.bracket)
(sub_pipeline ["$(" ")"] @punctuation.special)
