; Helix copy of editors/tree-sitter-cmdr/queries/highlights.scm, in Helix
; scope names. tests/test_cmdr.py checks the two stay in step.

(comment) @comment

; The first word of a stage is a command; def names the command it defines.
((stage command: (word) @function)
 (#not-eq? @function "def"))

((stage
   command: (word) @keyword.function
   .
   (word) @function)
 (#eq? @keyword.function "def"))

(reference) @variable

(parameter name: (identifier) @variable.parameter)
(parameter default: (default) @constant)

[(string) (raw_string)] @string
(escape_sequence) @constant.character.escape

(number) @constant.numeric

"|" @operator

["{" "}"] @punctuation.bracket
(parameters ["(" ")"] @punctuation.bracket)
(sub_pipeline ["$(" ")"] @punctuation.special)
