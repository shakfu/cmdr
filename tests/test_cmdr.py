"""Integration checks for value boundaries, effects, and error recovery."""
import json
import os
import re
import shutil
from pathlib import Path
import subprocess
import sys
import tempfile
import time
import tomllib
import unittest


ROOT = Path(__file__).resolve().parents[1]
BINARY = Path(os.environ.get("CMDR_BIN", ROOT / "cmdr")).resolve()
CLASH = os.environ.get("CMDR_CLASH_BIN")
PROBE = os.environ.get("CMDR_PROBE_BIN")


def quote(text):
    return '"' + str(text).replace('\\', '\\\\').replace('"', '\\"') + '"'


class Commands(unittest.TestCase):
    def run_line(self, source, expected=None, error=None):
        result = subprocess.run(
            [str(BINARY), "-c", source], capture_output=True, text=True, timeout=20
        )
        self.assertEqual(bool(result.returncode), bool(error), result.stderr)
        if expected is not None:
            self.assertEqual(result.stdout, expected)
        if error:
            self.assertIn(error, result.stderr)
        return result

    def test_text_and_arithmetic(self):
        cases = [
            ('text "  Hello World  " | trim | lower | replace world C', 'hello C\n'),
            ('add 2 3 | mul 4 | sub 1 | div 2', '9.5\n'),
            ('text "" | length', '0\n'),
            ('text "hello world" | take 5', 'hello\n'),
            ('text "hello world" | drop 6', 'world\n'),
            ('text "hello world" | drop 6 | take 3', 'wor\n'),
            ('take abc 99', 'abc\n'), ('drop abc 99', '\n'),
            ('take abc 0', '\n'), ('drop abc 0', 'abc\n'),
            ('text "a"b"c"', 'abc\n'),
            ('text a\\ b c', 'a b c\n'),
            ('text "a b" "c d" | length', '7\n'),
        ]
        for source, expected in cases:
            with self.subTest(source=source):
                self.run_line(source, expected)

    def test_large_pipeline_line(self):
        # One line with many tokens must stay fast and correct (regression:
        # the parser used to allocate line-sized buffers per token).
        source = 'text ' + ' '.join(['x'] * 20000) + ' | length'
        self.run_line(source, '39999\n')

    def test_lists(self):
        cases = [
            ('list "a\nb" "c" | count', '2\n'),
            ('list "" "" | count', '2\n'),
            ('list | count', '0\n'),
            ('list "hello world" "a|b" "" | map upper | join ,', 'HELLO WORLD,A|B,\n'),
            ('split "pear,apple,pear,banana" , | sort | uniq | join ", "', 'apple, banana, pear\n'),
            ('split "a,,b," , | join :', 'a::b:\n'),
            ('split "a\nb,c" , | count', '2\n'),
            ('split "" , | count', '0\n'),
            ('split , , | count', '2\n'),
            ('list "a\nb" "a\nc" "d" | filter a | count', '2\n'),
            ('list a b c | head 2 | tail 1 | join :', 'b\n'),
            ('list a b c | head 0 | count', '0\n'),
            ('list a b c | tail 0 | count', '0\n'),
            ('list a b c | tail 99 | count', '3\n'),
            ('list a b c | head 99 | count', '3\n'),
            ('list a a b a | uniq | join ,', 'a,b,a\n'),
            ('list a b | filter z | count', '0\n'),
            ('list a b | filter "" | count', '2\n'),
            ('list | map upper | count', '0\n'),
            ('list a b | map list | count', '2\n'),
        ]
        for source, expected in cases:
            with self.subTest(source=source):
                self.run_line(source, expected)

    def test_legacy_lines(self):
        cases = [
            ('text "b\na\na\n" | sort | uniq | upper', 'A\nB\n'),
            # Text in gives text out; lists stay lists.
            ('text "b\na" | sort | length', '4\n'),
            ('text "a\nb\n" | filter b | head 1 | tail 1 | length', '2\n'),
            ('list b a | sort | count', '2\n'),
            ('text "a\n\nb\n" | count', '3\n'),
            ('text "\n" | count', '1\n'),
            ('text "" | count', '0\n'),
            ('text "a\nb" | count', '2\n'),
            ('text "a\nb" | map upper | join ,', 'A,B\n'),
            ('text "a\n\nb\n" | lines | map length | join ,', '1,0,1\n'),
            ('text "" | lines | count', '0\n'),
            ('list "a\nb" c | lines | count', '2\n'),
        ]
        for source, expected in cases:
            with self.subTest(source=source):
                self.run_line(source, expected)
        self.run_line('list b a | sort | length', error='expects text')

    def test_paths(self):
        cases = [
            ('path-basename /a/b.txt', 'b.txt\n'), ('path-dirname /a/b.txt', '/a\n'),
            ('path-basename /a/b/', 'b\n'), ('path-dirname /a/b/', '/a\n'),
            ('path-basename /', '/\n'), ('path-dirname /', '/\n'),
            ('path-basename ////', '/\n'), ('path-dirname ////', '/\n'),
            ('path-basename ""', '.\n'), ('path-dirname ""', '.\n'),
            ('path-dirname name', '.\n'), ('path-dirname a//b', 'a\n'),
            ('list /a/b /c/d | map path-basename | join ,', 'b,d\n'),
        ]
        for source, expected in cases:
            with self.subTest(source=source):
                self.run_line(source, expected)

    def test_files_and_mapping(self):
        with tempfile.TemporaryDirectory() as tmp:
            directory = Path(tmp)
            contents = {'a.log': 'one\ntwo\n', 'b.log': 'three', 'c\nd.log': 'four\nfive'}
            for name, text in contents.items():
                (directory / name).write_text(text)
            (directory / 'subdir').mkdir()
            (directory / '.hidden').write_text('hidden')
            (directory / 'broken').symlink_to(directory / 'missing')
            self.run_line(f'files {quote(directory)} | count', '4\n')
            self.run_line(f'files {quote(directory)} | filter .log | map read | count', '3\n')
            self.run_line(f'files {quote(directory)} | filter .log | map read | map length | join ,', '8,5,9\n')
            self.run_line(f'files {quote(directory)} | filter .log | map path-basename | map length | join ,', '5,5,7\n')
            self.run_line(f'files {quote(directory / "subdir")} | count', '0\n')
            self.run_line(f'files {quote(directory / "missing")}', error='files ')
            self.run_line(f'files {quote(directory / "a.log")}', error='Not a directory')

    def test_effects_preserve_values(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / 'output'
            q = quote(path)
            self.run_line(f'text one | write {q} | upper', 'ONE\n')
            self.assertEqual(path.read_text(), 'one')
            self.run_line(f'text two | append {q} | length', '3\n')
            self.assertEqual(path.read_text(), 'onetwo')
            self.run_line(f'list "a\nb" c | write {q} | count', '2\n')
            self.assertEqual(path.read_text(), 'a\nb\nc\n')
            self.run_line(f'list x y | write {q} | map upper | join ,', 'X,Y\n')
            self.assertEqual(path.read_text(), 'x\ny\n')
            self.run_line(f'list a b | map append {q} | count', '2\n')
            self.assertEqual(path.read_text(), 'x\ny\nab')
            self.run_line(f'text "" | write {q}', '\n')
            self.assertEqual(path.read_bytes(), b'')
            self.run_line(f'text x | append {quote(path / "bad")}', error='append ')

    def test_recovery(self):
        cases = [
            ("try { () div 1 0 } fallback | upper", 'FALLBACK\n'),
            ("try { () text success } fallback", 'success\n'),
            ("text hello | try { upper } fallback", 'HELLO\n'),
            ("text x | try { equals y } no", 'no\n'),
            ("list a b | try { map upper } fallback | join ,", 'A,B\n'),
            ("list hello world | each { try { upper } fallback } | join ,", 'HELLO,WORLD\n'),
            ("try { () list a } '' | count", '1\n'),
            # A fallback block is returned as a value, not called.
            ("text x | try { equals y } { upper }", '{ upper }\n'),
        ]
        for source, expected in cases:
            with self.subTest(source=source):
                self.run_line(source, expected)
        # Syntax, names, and arity are checked, not recovered.
        errors = [
            ("try { () unknown } x", 'unknown command: unknown'),
            ("try { () text x | } y", "expected a command after '|'"),
            ("text x | try { () text x } ok", 'try: block takes 0 arguments, got 1'),
            ("try { text x } ok", 'try: block takes 1 argument, got 0'),
            # Naming the item as the fallback stops it being piped; arity
            # reports the missing input instead of returning the fallback.
            ("list 1 x | each { (n) try { add 1 } $n }", 'each item 1: try: block takes 1 argument, got 0'),
            ("list a | each { try { unknown } x }", 'unknown command: unknown'),
            ("text x | try upper y", 'try: expected a block'),
        ]
        for source, error in errors:
            with self.subTest(source=source):
                self.run_line(source, error=error)
        self.run_line("list 1 x 3 | each { (n) try $n { add 1 } $n } | join ,", '2,x,4\n')
        with tempfile.TemporaryDirectory() as tmp:
            # A failed stage stops the pipeline before later effects.
            path = Path(tmp) / 'must-not-exist'
            self.run_line(f"run false | write {quote(path)}", error='status 1')
            self.assertFalse(path.exists())
            # Block errors are found before the first stage runs.
            self.run_line(f"text x | write {quote(path)} | each {{ (s) text $t }}", error="'$t'")
            self.assertFalse(path.exists())
            existing = Path(tmp) / 'exists'
            existing.write_text('a\nb')
            missing = Path(tmp) / 'missing'
            self.run_line(f'list {quote(existing)} {quote(missing)} | each {{ try {{ read }} "" }} '
                          '| map length | join ,', '3,0\n')

    def test_failures(self):
        cases = [
            ('take abc -1', 'nonnegative integer'),
            ('drop abc 1.5', 'nonnegative integer'),
            ('take abc ""', 'nonnegative integer'),
            ('take abc 9999999999999999999999999', 'nonnegative integer'),
            ('list a b | upper', 'use map or join'),
            ('list | map missing', 'unknown command'),
            ('list a b | map upper extra', 'map item 1'),
            ('list a | map list | join ,', 'expected text'),
            ('run false', 'status 1'),
            ('split x ""', 'nonempty'),
            ('div 1 0', 'division by zero'),
            ('text x |', 'expected a command'),
            ('text x | | upper', 'expected a command before'),
            ('text "a', 'unclosed quote'),
            ("text a\\", 'unfinished escape'),
        ]
        for source, error in cases:
            with self.subTest(source=source):
                self.run_line(source, error=error)
        for source in ['map', 'files a b', 'path-basename', 'path-dirname', 'take a', 'drop a',
                       'append a', 'write a', 'try', 'try a', 'text a | try a b c']:
            with self.subTest(source=source):
                self.run_line(source, error='usage:')

    def test_references(self):
        cases = [
            ("text x | try { (s) text pre $s } no", 'pre x\n'),
            ("text x | try { (s) text $s $s } no", 'x x\n'),
            ("list a b | each { (s) text [ $s ] } | join ,", '[ a ],[ b ]\n'),
            ("list a | each { (item-2 x_y) text $item-2 $x_y } b", 'a b\n'),
            ("text a$1 \\$1 '$1' \"$2\" _", 'a$1 $1 $1 $2 _\n'),
        ]
        for source, expected in cases:
            with self.subTest(source=source):
                self.run_line(source, expected)
        errors = [
            ('text $x', "invalid reference '$x': no enclosing block declares it"),
            ('text $1', "invalid reference '$1'"),
            ('text $_', "invalid reference '$_'"),
            ('text $1x', "invalid reference '$1x'"),
            ('text $', "invalid reference '$'"),
            # A name is visible only inside the block that declares it.
            ('list a | each { (s) text $s } | text $s', "invalid reference '$s'"),
            ('list a | each { (s) $s x }', 'command name cannot be a reference'),
            ('{ upper } x', 'command name cannot be a block'),
            ('list a | each { (1x) upper }', 'invalid parameter list'),
            ('list a | each { (s }', 'invalid parameter list'),
        ]
        for source, error in errors:
            with self.subTest(source=source):
                self.run_line(source, error=error)

    def test_combinators_and_predicates(self):
        cases = [
            ("list a bb '' | keep { nonempty } | join ,", 'a,bb\n'),
            ("list a bb c | keep { contains b } | join ,", 'bb\n'),
            ("list a b | keep { not { equals a } } | join ,", 'b\n'),
            ("list a b c | keep { (s v) not $s { equals $v } } b | join ,", 'a,c\n'),
            ("list a b c | keep { (s v) equals $s $v } b | join ,", 'b\n'),
            ("list | keep { nonempty } | count", '0\n'),
            # keep preserves the input representation, like filter.
            ('text "x\ny\n" | keep { equals y }', 'y\n'),
            ('text "x\ny\n" | keep { equals y } | length', '2\n'),
            ("list 'a b' c | each { (s p) text $p $s } pre | join ,", 'pre a b,pre c\n'),
            ("list a b | each { (s) upper $s | text [ $s ] } | join ,", 'A [ a ],B [ b ]\n'),
            ("list a b | each { (s) list $s } | count", '2\n'),
            ("list a b | each { try { equals a } no } | join ,", 'a,no\n'),
            ("text '' | not { nonempty } | length", '0\n'),
            ("text abc | contains b | upper", 'ABC\n'),
            ("text '   ' | trim | try { nonempty | upper } none", 'none\n'),
            ("text ' hi ' | trim | try { nonempty | upper } none", 'HI\n'),
            # Inner blocks see enclosing names; an inner name shadows.
            ("list a,b c | each { (r) split $r , | each { (f) text $f in $r } | join ';' } | join ' '",
             'a in a,b;b in a,b c in c\n'),
            ("list a | each { (x y) split $y , | each { (x) text $x $y } | join + | text $x } b,c",
             'b b,c+c b,c a\n'),
            ("list { upper } { (a) lower $a } | count", '2\n'),
            # Declared parameters appear only where named, so a block can
            # start from something other than its input.
            ("text ERROR | try { (w) text 'an ERROR' | contains $w | count } no", '1\n'),
            ("list a | each { (s) $s | text $s }", 'a a\n'),
            ("text '{' \"}\" \\{", '{ } {\n'),
            ("list a | each { text \"a }\" '{' }", 'a a } {\n'),
        ]
        for source, expected in cases:
            with self.subTest(source=source):
                self.run_line(source, expected)
        errors = [
            # Invalid bodies fail instead of acting as false (or true for not).
            ('list a | keep { nonemtpy }', 'unknown command: nonemtpy'),
            ('list | keep { bogus }', 'unknown command: bogus'),
            ("list a | keep { (s v) equals $s $v }", 'keep: block takes 2 arguments, got 1'),
            ("list a | each { (s) upper }", 'parameter $s is never used'),
            # An unused parameter would never receive its argument.
            ("text a | try { (s) upper } no", 'parameter $s is never used'),
            ("list a | each { (s) each { (t) text $s } }", 'parameter $t is never used'),
            ("list a | keep { equals b } b", 'keep: block takes 1 argument, got 2'),
            ('list a | keep nonempty', 'keep: expected a block'),
            ('text a | not { bogus }', 'unknown command: bogus'),
            # Errors below the top-level body are errors too, not false.
            ("list a | keep { not equals a }", 'keep item 1: not: expected a block'),
            ("list a | keep { read }", 'keep item 1: read a'),
            ("text a | not { try { bogus } x }", 'unknown command: bogus'),
            ("list a | each { text a | }", "expected a command after '|'"),
            ('text x | not { nonempty }', "not: x satisfies { nonempty }"),
            ("list a b | each { equals a }", 'each item 2: equals'),
            ("list a | each { text a", "unclosed '{'"),
            ("text {a}b", "expected a space after '}'"),
            ("text { upper }", "expects text"),
            ("list a | each " + "{ " * 200 + "}" * 200, 'blocks nested too deeply'),
            ('text "" | nonempty', 'nonempty: empty text'),
            ('list | nonempty', 'nonempty: empty list'),
            ('text a | equals b', "equals: a differs from b"),
            # Descriptions quote text that is not one word, and stay short.
            ("text 'a b' | equals ''", "equals: 'a b' differs from ''"),
            ('text "x\ny" | equals ' + 'z' * 40, "differs from " + 'z' * 32 + "..."),
            ('text "x\ny" | equals x', r"equals: 'x\ny' differs from x"),
            ('list a b c d e f g | not { nonempty }', 'not: [a, b, c, d, e, ... 7 items] satisfies'),
            ('text acd | contains b', "contains: b not found in acd"),
            ('list a | equals a', "command 'equals' expects text"),
        ]
        for source, error in errors:
            with self.subTest(source=source):
                self.run_line(source, error=error)

    def test_with_fold_or(self):
        cases = [
            # with binds a computed value, so it can be any argument.
            ("text foobar | with { (s) length bar | with { (n) drop $s $n } }", 'bar\n'),
            ("list a b | with { (l k) count $l | text $k } items", '2 items\n'),
            # A mismatch passes through with, so keep still treats it as false.
            ("list a b | keep { with { equals a } } | join ,", 'a\n'),
            ("list 1 2 3 | fold 0 { (a x) add $a $x }", '6\n'),
            ("list a b c | fold '' { (a x) list $x $a | join '' }", 'cba\n'),
            ("list 1 2 | fold 0 { (a x k) add $a $x $k } 10", '23\n'),
            ("list | fold init { (a x) text $a $x }", 'init\n'),
            ('text "1\n2\n" | fold 0 { (a x) add $a $x }', '3\n'),
            # or falls back only on a mismatch.
            ("text a | or { equals b } other", 'other\n'),
            ("text a | or { equals a } other", 'a\n'),
            ("or { () equals a b } other", 'other\n'),
            ("text a | check { equals a } 'not a'", 'a\n'),
            # check keeps the mismatch status, so keep still treats it as false.
            ("list a b | keep { check { equals a } 'not a' } | join ,", 'a\n'),
        ]
        for source, expected in cases:
            with self.subTest(source=source):
                self.run_line(source, expected)
        errors = [
            ("text a | with { (x y) text $x $y }", 'with: block takes 2 arguments, got 1'),
            ("text a | with upper", 'with: expected a block'),
            ("list 1 2 | fold 0 { (a) add $a 1 }", 'fold: block takes 1 argument, got 2'),
            ("list 1 x | fold 0 { (a x) add $a $x }", 'fold item 2: invalid finite number: x'),
            ("list nonexistent | fold 0 { (a x) read $x | text $a }", 'fold item 1: read nonexistent'),
            # An error is not a mismatch, so or does not hide it.
            ("text a | or { add 1 } other", 'invalid finite number: a'),
            ("text a | or { bogus } other", 'unknown command: bogus'),
            ("or { equals a } other", 'or: block takes 1 argument, got 0'),
            # check replaces a mismatch message, not an error.
            ("text b | check { equals a } 'b is not a'", 'cmdr: b is not a\n'),
            ("text a | check { add 1 } 'never shown'", 'invalid finite number: a'),
            ("text a | check { equals a } { upper }", 'expected text'),
        ]
        for source, error in errors:
            with self.subTest(source=source):
                self.run_line(source, error=error)

        # A def's call still prefixes the message.
        result = self.run_input("def is-a { (s) text $s 'is not a' | with { (m) check $s { equals a } $m } }\n"
                                "text b | is-a\n")
        self.assertIn('is-a b: b is not a', result.stderr)

    def test_matches_and_sort_n(self):
        cases = [
            ("text abc123 | matches '^[a-z]+[0-9]+$'", 'abc123\n'),
            ("list a1 b c2 | keep { matches '[0-9]' } | join ,", 'a1,c2\n'),
            ("list 10 9 100 -1 2.5 | sort-n | join ,", '-1,2.5,9,10,100\n'),
            ('text "10\n9\n" | sort-n', '9\n10\n'),
            ("list | sort-n | count", '0\n'),
            ("list | sort | count", '0\n'),
        ]
        for source, expected in cases:
            with self.subTest(source=source):
                self.run_line(source, expected)
        self.run_line("text abc | matches '^[0-9]'", error="matches: abc does not match '^[0-9]'")
        self.run_line("text abc | not { matches '^[0-9]' }", 'abc\n')
        self.run_line("text abc | matches '('", error="matches: invalid regex '('")
        self.run_line("list 1 x | sort-n", error='invalid finite number: x')

    def run_input(self, source, *args):
        return subprocess.run([str(BINARY), *args], input=source,
                              capture_output=True, text=True, timeout=20)

    def test_definitions(self):
        result = self.run_input(
            "def clean-or { (s fallback) trim $s | try { nonempty | upper } $fallback }\n"
            "def wrap { (x d) text $d $x $d }\n"
            "clean-or '  hi  ' none\n"
            "text '   ' | clean-or none\n"
            "list a b | map wrap - | join ,\n"
            "help | filter def: | count\n"
            # Redefinition shadows prelude commands and earlier definitions.
            "def wrap { (x) text [ $x ] }\n"
            "wrap x\n"
            "def upper { lower }\n"
            "text ABC | upper\n")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout, 'HI\nnone\n- a -,- b -\n2\n[ x ]\nabc\n')
        errors = [
            ('def', 'usage: def NAME BLOCK'),
            ('def x upper', 'usage: def NAME BLOCK'),
            ("def 'a b' { upper }", "def: invalid name 'a b'"),
            ("def 'a|b' { upper }", "def: invalid name 'a|b'"),
            ("def '$x' { upper }", "def: invalid name '$x'"),
            ('def def { upper }', 'def: cannot redefine def'),
            ('def x { bogus }', 'unknown command: bogus'),
            ("def x {}", 'def x: empty block'),
            ("def x { upper | }", "expected a command after '|'"),
            ("list a | each { (s) def $s { upper } }", 'def a: block uses enclosing parameters'),
        ]
        for source, error in errors:
            with self.subTest(source=source):
                self.run_line(source, error=error)
        # Arity is checked at call time.
        for source, error in [
            ("def w { (a b) text $a $b }\nw a\n", "command 'w' expects 2 arguments, got 1"),
            ("def shout { upper }\nshout\n", "command 'shout' expects 1 argument, got 0"),
        ]:
            result = self.run_input(source)
            self.assertEqual(result.returncode, 1)
            self.assertIn(error, result.stderr)

    def test_lexical_definitions(self):
        result = self.run_input(
            # Naming itself calls the shadowed command, so a def can wrap it.
            "def upper { upper | text ! }\n"
            "text a | upper\n"
            # Bodies keep the commands they were defined with.
            "def shout { upper }\n"
            "def upper { lower }\n"
            "text a | shout\n"
            "text A | upper\n")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout, 'A !\nA !\na\n')
        # Errors from a body name the command, giving a call chain.
        result = self.run_input("def inner { equals x }\ndef outer { trim | inner }\ntext y | outer\n")
        self.assertIn("outer y: inner: equals: y differs from x", result.stderr)
        # Only a mismatch names the call's arguments, and only on the line
        # itself: an error keeps the plain chain.
        result = self.run_input("def num { (s) add $s 1 }\nnum x\n"
                                "def inner { equals x }\nlist y | each { inner }\n")
        self.assertIn("num: invalid finite number: x", result.stderr)
        self.assertIn("each item 1: inner: equals: y differs from x", result.stderr)
        # A block keeps the names it captured after its def returns.
        result = self.run_input(
            "def adder { (n) try $n { equals never } { (x) add $x $n } }\n"
            "def call { (b x) try $x $b none }\n"
            "adder 10 | call 5\n"
            "def without { (xs v) keep $xs { not { equals $v } } }\n"
            "list a b c | without b | join ,\n")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout, '15\na,c\n')
        # A block passed into a def resolves names where it was written.
        result = self.run_input(
            "def apply { (b) list x | each $b | join , }\n"
            "def later { upper }\n"
            "apply { later }\n"
            "def upper { text shadowed }\n"
            "apply { upper }\n")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout, 'X\nx shadowed\n')

    def test_script_files(self):
        with tempfile.TemporaryDirectory() as directory:
            script = Path(directory) / 'script.cmdr'
            script.write_text(
                "#!/usr/bin/env cmdr\n"
                "# Helpers\n"
                "def shout { (s) upper $s | text $s ! }\n"
                "shout hi\n"
                "bogus\n"
                "text unreachable\n")
            result = subprocess.run([str(BINARY), str(script)], capture_output=True,
                                    text=True, timeout=20)
            self.assertEqual(result.returncode, 1)
            self.assertEqual(result.stdout, 'HI hi !\n')
            self.assertIn(f'{script}:5: unknown command: bogus', result.stderr)
            missing = subprocess.run([str(BINARY), str(Path(directory) / 'missing')],
                                     capture_output=True, text=True, timeout=20)
            self.assertEqual(missing.returncode, 1)
            self.assertIn('missing', missing.stderr)
        usage = subprocess.run([str(BINARY), '-x'], capture_output=True, text=True, timeout=20)
        self.assertEqual(usage.returncode, 2)
        self.assertIn('usage: cmdr [--user] [-c PIPELINE | FILE [ARG ...]]', usage.stderr)
        usage = subprocess.run([str(BINARY), '-c', 'text x', 'extra'], capture_output=True,
                               text=True, timeout=20)
        self.assertEqual(usage.returncode, 2)

    def test_prelude(self):
        cases = [
            ('path-join a b', 'a/b\n'), ('path-join a/ b', 'a/b\n'),
            ('path-join "" b', 'b\n'), ('path-join a /b', '/b\n'),
            ('path-join a ""', 'a/\n'), ('path-join / b', '/b\n'),
            ('path-join a b | path-join c', 'a/b/c\n'),
            ('path-split a/b/c | join " "', 'a/b c\n'), ('path-split /c | join " "', '/ c\n'),
            ('path-split c | join " "', '. c\n'), ('path-split a/b/ | join " "', 'a b\n'),
            ('list a.b.c d.e | map path-ext | join ,', 'c,e\n'),
            ('list a.txt Makefile .gitignore | path-has-ext | join ,', 'a.txt\n'),
            ('list a b | last', 'b\n'),
            ('text "a\nb\n" | last', 'b\n'),
            ('help | filter "Return the last item" | count', '1\n'),
            ('list a b c | first', 'a\n'),
            ('list a b c | nth 2', 'b\n'),
            ('list a b c | init | join ,', 'a,b\n'),
            ('list a b c | rest | join ,', 'b,c\n'),
            ('list a b | concat', 'ab\n'),
            ('list a b | unwords', 'a b\n'),
            ('list b a b | distinct | join ,', 'a,b\n'),
            ('list ab bc | reject c | join ,', 'ab\n'),
            ('list a b | without a | join ,', 'b\n'),
            ("list a ' ' '' b | compact | join ,", 'a,b\n'),
            ('list a b | prefix x- | suffix .c | join ,', 'x-a.c,x-b.c\n'),
            ('list "" a | keep { empty } | count', '1\n'),
            ('list 1 2 3 | any { gt 2 } | count', '3\n'),
            ('list 1 2 3 | all { gt 0 } | count', '3\n'),
            ('list | all { gt 0 } | count', '0\n'),
            ('list 1 2 3.5 | sum', '6.5\n'),
            ('list | sum', '0\n'),
            ('list 2 3 4 | product', '24\n'),
            ('list 3 10 2 | max', '10\n'),
            ('list 3 10 2 | min', '2\n'),
            ('list 1 2 | mean', '1.5\n'),
            ('list 1 5 11 | keep { between 2 10 } | join ,', '5\n'),
            ('text "a  b\tc\nd" | words | join ,', 'a,b,c,d\n'),
            ('text foobar | starts-with foo', 'foobar\n'),
            ('text foo | starts-with ""', 'foo\n'),
            ('text foobar | ends-with bar', 'foobar\n'),
            ('text foobar | ends-with ""', 'foobar\n'),
            ('list a.c b.h | keep { ends-with .c } | join ,', 'a.c\n'),
            ('text foobar | strip-prefix foo', 'bar\n'),
            ('text foobar | strip-suffix bar', 'foo\n'),
            ('list dir/a.tar.gz .gitignore Makefile a. | map path-stem | join ,',
             'a.tar,.gitignore,Makefile,a\n'),
            ('text hi | sh-in "tr a-z A-Z"', 'HI\n'),
            ('sh "echo hi"', 'hi\n'),
        ]
        for source, expected in cases:
            with self.subTest(source=source):
                self.run_line(source, expected)
        errors = [
            ('list a b c | nth 4', 'nth [a, b, c] 4: le: 4 is not at most 3'),
            ('list a b c | nth 0', 'nth [a, b, c] 0: gt: 0 is not greater than 0'),
            ('list | init', 'init []: nonempty: empty list'),
            ('list | first', 'first []: nonempty: empty list'),
            ('list | last', 'last []: nonempty: empty list'),
            ('text "" | last', "last '': nonempty: empty text"),
            ('list 1 2 | any { gt 5 }', 'any [1, 2] { gt 5 }: nonempty: empty list'),
            ('list 1 2 | all { gt 1 }', 'all [1, 2] { gt 1 }: empty: not: [1] satisfies { nonempty }'),
            ('list | max', 'max []: nonempty: empty list'),
            ('list 3 x | max', 'max: fold item 2: invalid finite number: x'),
            ('text fo | starts-with foo', "starts-with fo foo: equals: fo differs from foo"),
            ('text ar | ends-with bar', 'ends-with ar bar: ge: 2 is not at least 3'),
            ('text foo | strip-prefix bar', 'strip-prefix foo bar: starts-with: equals: foo differs from bar'),
        ]
        for source, error in errors:
            with self.subTest(source=source):
                self.run_line(source, error=error)
        with tempfile.TemporaryDirectory() as directory:
            a, b = Path(directory) / 'a', Path(directory) / 'b'
            a.write_text('one\ntwo\n')
            b.write_text('three\n')
            self.run_line(f'list {quote(a)} {quote(b)} | cat | count', '3\n')
            self.run_line(f"grep {quote(a)} '^t'", 'two\n')
        # Prelude defs are lexical: a later def does not change them.
        result = self.run_input("def tail { lower }\nlist a b | last\n")
        self.assertEqual((result.returncode, result.stdout), (0, 'b\n'), result.stderr)

    def test_definition_help(self):
        result = self.run_input(
            "def exts { (dir) files $dir | count } 'Count files in DIR'\n"
            "def pair { (a b) text $a $b }\n"
            "def none { () text x }\n"
            "def shout { upper }\n"
            "help\n")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertRegex(result.stdout, r'(?m)^exts DIR +Count files in DIR$')
        self.assertRegex(result.stdout, r'(?m)^pair A B +def: \(a b\) text \$a \$b$')
        self.assertRegex(result.stdout, r'(?m)^none +def: \(\) text x$')
        self.assertRegex(result.stdout, r'(?m)^shout VALUE +def: upper$')
        # Tables first, then definitions oldest first: the prelude, then the
        # session. A shadowed command is listed once, at its definition.
        result = self.run_input("def zz { upper }\ndef upper { lower }\nhelp | lines | each { (row) split $row ' ' | head 1 | join '' }\n")
        names = result.stdout.split()
        self.assertEqual(names[:8], ['help', 'def', 'script-file', 'script-dir', 'script-args',
                                     'load', 'import', 'from'])
        self.assertEqual(names.count('upper'), 1)
        self.assertLess(names.index('run-in'), names.index('first'))
        self.assertEqual(names[names.index('first'):names.index('first') + 2], ['first', 'last'])
        self.assertEqual(names[names.index('sh-in'):names.index('sh-in') + 3], ['sh-in', 'zz', 'upper'])
        self.run_line('def f { upper } { lower }', error='usage: def NAME BLOCK [DOC]')
        self.run_line('def f { upper } a b', error='usage: def NAME BLOCK [DOC]')

    @unittest.skipUnless(PROBE, 'CMDR_PROBE_BIN is not set')
    def test_extensions(self):
        # tests/ext/probe: a C command, and a definition built on it.
        def run(*args, env=None):
            return subprocess.run([PROBE, *args], capture_output=True, text=True,
                                  timeout=20, env=env)
        result = run('-c', 'probe-twice')
        self.assertEqual((result.returncode, result.stdout), (0, 'probed probed\n'), result.stderr)
        # help tags C commands with their extension; definitions keep their docs.
        result = run('-c', 'help')
        self.assertRegex(result.stdout, r'(?m)^probe +ext/probe: Return probed$')
        self.assertRegex(result.stdout, r'(?m)^probe-twice +Return probed twice$')
        self.assertRegex(result.stdout, r'(?m)^split TEXT SEPARATOR +Split into a list$')
        # Extension commands follow the built-ins, ending with from, and precede
        # primitives; their definitions precede the prelude's.
        names = [line.split()[0] for line in result.stdout.splitlines()[:-3]]
        self.assertEqual(names[names.index('from') + 1], 'probe')
        self.assertLess(names.index('run-in'), names.index('probe-twice'))
        self.assertLess(names.index('probe-twice'), names.index('first'))
        # Extension scripts do not depend on the prelude, which --user replaces.
        with tempfile.TemporaryDirectory() as home:
            prelude = Path(home) / '.cmdr' / 'prelude.cmdr'
            prelude.parent.mkdir()
            prelude.write_text('')
            result = run('--user', '-c', 'probe-twice', env={**os.environ, 'HOME': home})
            self.assertEqual((result.returncode, result.stdout), (0, 'probed probed\n'), result.stderr)

    def test_mk_ext(self):
        # CMake also builds a fresh skeleton with --c-test and runs its tests:
        # ext_mk-ext-skeleton_test and ext_mk-ext-skeleton_test_mk-ext-skeleton.
        script = ROOT / 'scripts' / 'mk_ext.py'
        def run(*args):
            return subprocess.run([sys.executable, str(script), *args],
                                  capture_output=True, text=True, timeout=20)
        with tempfile.TemporaryDirectory() as root:
            result = run('my-ext', '--root', root)
            self.assertEqual(result.returncode, 0, result.stderr)
            target = Path(root) / 'my-ext'
            self.assertEqual(sorted(str(p.relative_to(target)) for p in target.rglob('*')),
                             ['README.md', 'my-ext.c', 'my-ext.cmdr', 'tests', 'tests/test.cmdr'])
            self.assertIn('const cmdr cmdr_ext_my_ext =', (target / 'my-ext.c').read_text())
            # --c-test adds a C test program.
            result = run('c-ext', '--root', root, '--c-test')
            self.assertEqual(result.returncode, 0, result.stderr)
            test = Path(root) / 'c-ext' / 'tests' / 'test_c-ext.c'
            self.assertIn('extern const cmdr cmdr_ext_c_ext;', test.read_text())
            # An existing directory is left as it is.
            (target / 'README.md').write_text('mine\n')
            result = run('my-ext', '--root', root)
            self.assertEqual(result.returncode, 1)
            self.assertIn('already exists', result.stderr)
            self.assertEqual((target / 'README.md').read_text(), 'mine\n')
            for name in ['My', '1x', 'a_b', 'a--b', '-a', 'a-', 'a b']:
                with self.subTest(name=name):
                    result = run('--root', root, '--', name)
                    self.assertEqual(result.returncode, 2)
                    self.assertIn('invalid name', result.stderr)
                    self.assertFalse((Path(root) / name).exists())

    @unittest.skipUnless(CLASH, 'CMDR_CLASH_BIN is not set')
    def test_extension_collisions(self):
        result = subprocess.run([CLASH, '-c', 'text ran'], capture_output=True,
                                text=True, timeout=20)
        self.assertEqual((result.returncode, result.stdout), (1, ''))
        self.assertEqual(result.stderr.splitlines(), [
            "cmdr: ext/clash: 'split' is already a command",
            "cmdr: ext/clash: 'help' is already a command",
            "cmdr: ext/clash: 'last' is already a command",
            "cmdr: ext/clash: 'fresh' is already a command",
            "cmdr: ext/clash/clash.cmdr: 'take' is already a command",
            "cmdr: ext/clash/clash.cmdr: 'first' is already a command",
        ])

    def test_load(self):
        with tempfile.TemporaryDirectory() as directory:
            defs = Path(directory) / 'defs.cmdr'
            defs.write_text("# Helpers\ndef shout { upper | text ! }\ntext loaded\n")
            result = self.run_input(f"load {quote(defs)}\ntext a | shout\n")
            self.assertEqual((result.returncode, result.stdout), (0, 'loaded\nA !\n'), result.stderr)
            # A loaded script stops at its first failure and names the line.
            bad = Path(directory) / 'bad.cmdr'
            bad.write_text("def ok { upper }\nbogus\ntext unreachable\n")
            result = self.run_input(f"load {quote(bad)}\ntext a | ok\n")
            self.assertEqual(result.stdout, 'A\n')
            self.assertIn(f'{bad}:2: unknown command: bogus', result.stderr)
            self.assertIn(f'load {bad} failed', result.stderr)
            self.run_line(f'load {quote(Path(directory) / "missing")}', error='No such file')
            loop = Path(directory) / 'loop.cmdr'
            loop.write_text(f"load {quote(loop)}\n")
            self.run_line(f'load {quote(loop)}', error='nested too deeply')

    def version(self):
        result = subprocess.run([str(BINARY), '--version'], capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertRegex(result.stdout, r'^cmdr \d+\.\d+\.\d+\n$')
        return result.stdout.strip()

    def test_user_prelude(self):
        self.version()
        with tempfile.TemporaryDirectory() as home:
            env = dict(os.environ, HOME=home)
            def run(*args):
                return subprocess.run([str(BINARY), *args], capture_output=True,
                                      text=True, timeout=20, env=env)
            missing = run('--user', '-c', 'text x')
            self.assertEqual(missing.returncode, 1)
            self.assertIn(f'{home}/.cmdr/prelude.cmdr: No such file', missing.stderr)
            prelude = Path(home) / '.cmdr' / 'prelude.cmdr'
            prelude.parent.mkdir()
            prelude.write_text("def greet { () text hi }\n")
            self.assertEqual(run('--user', '-c', 'greet').stdout, 'hi\n')
            # The user prelude replaces the standard one.
            self.assertIn('unknown command: last', run('--user', '-c', 'list a | last').stderr)
            self.assertIn('unknown command: greet', run('-c', 'greet').stderr)
            script = Path(home) / 'script.cmdr'
            script.write_text("greet\n")
            self.assertEqual(run('--user', str(script)).stdout, 'hi\n')
            prelude.write_text("text ok\nbogus\n")
            broken = run('--user', '-c', 'text x')
            self.assertEqual((broken.returncode, broken.stdout), (1, 'ok\n'))
            self.assertIn(f'{prelude}:2: unknown command: bogus', broken.stderr)
        for args in [['--user', '--user'], ['--version', 'x'], ['--user', '-c'], ['-c', 'a', 'b']]:
            with self.subTest(args=args):
                result = subprocess.run([str(BINARY), *args], capture_output=True, text=True)
                self.assertEqual(result.returncode, 2, result.stderr)
                self.assertIn('usage:', result.stderr)

    def test_banner(self):
        version = self.version().encode()
        with tempfile.TemporaryDirectory() as home:
            code, out = self.pty_session([b"quit\r"], home)
            self.assertEqual(code, 0, out)
            # Bold cyan where the prompt has color, which needs the editor.
            cyan = os.environ.get('CMDR_EDITOR') == '1'
            line = version + b" (prelude: standard)"
            self.assertIn(b"\x1b[1;36m" + line + b"\x1b[0m\r\n" if cyan else line + b"\r\n", out)
            prelude = Path(home) / '.cmdr' / 'prelude.cmdr'
            prelude.parent.mkdir()
            prelude.write_text("def greet { () text hi }\n")
            code, out = self.pty_session([b"quit\r"], home, args=['--user'])
            self.assertEqual(code, 0, out)
            self.assertIn(version + f" (prelude: {prelude})".encode(), out)
        # Output that is not interactive carries no banner.
        self.run_line('text x', 'x\n')
        self.assertEqual(self.run_input('text y\n').stdout, 'y\n')

    def test_escapes(self):
        cases = [
            ('list a b | join "\\n"', 'a\nb\n'),
            ('text "x\\ty" | length', '3\n'),
            ("text 'a\\nb'", 'a\\nb\n'),   # single quotes stay literal
            ('text a\\nb', 'anb\n'),       # unquoted backslash escapes the next byte
            ('text "a\\"b\\\\c"', 'a"b\\c\n'),
        ]
        for source, expected in cases:
            with self.subTest(source=source):
                self.run_line(source, expected)

    def test_value_stage(self):
        result = self.run_input(
            "def stats2 { (n xs) $xs | sort | uniq | count | text of $n }\n"
            "def stats { (xs) count $xs | stats2 $xs }\n"
            "list b a b | stats\n"
            "list a | each { (s) upper $s | $s }\n")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout, '2 of 3\na\n')
        errors = [
            ('$x', "invalid reference '$x'"),
            ('list a | each { (s) $s y }', 'command name cannot be a reference'),
        ]
        for source, error in errors:
            with self.subTest(source=source):
                self.run_line(source, error=error)

    def test_no_bindings(self):
        # `as` is an ordinary name.
        self.run_line('text a | as x', error='unknown command: as')
        result = self.run_input("def as { upper }\ntext a | as\n")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout, 'A\n')

    def test_limits(self):
        # Values and composed commands nest boundedly instead of overflowing.
        self.run_line('list a' + ' | list' * 1100, error='value is nested too deeply')
        defs = "def d0 { upper }\n" + "".join(f"def d{i} {{ d{i - 1} }}\n" for i in range(1, 130))
        result = self.run_input(defs + "d129 x\n")
        self.assertEqual(result.returncode, 1)
        self.assertIn('nested too deeply', result.stderr)

    def test_numeric_predicates(self):
        cases = [
            ('text 3 | lt 5', '3\n'),
            ('text 7 | gt 5.5', '7\n'),
            ("list 1 5 9 | keep { (n lo) gt $n $lo } 4 | join ,", '5,9\n'),
            ('text 5 | le 5', '5\n'),
            ('text 5 | ge 5', '5\n'),
        ]
        for source, expected in cases:
            with self.subTest(source=source):
                self.run_line(source, expected)
        self.run_line('text 7 | lt 5', error='lt: 7 is not less than 5')
        self.run_line('text 6 | le 5', error='le: 6 is not at most 5')
        self.run_line('text 4 | ge 5', error='ge: 4 is not at least 5')
        self.run_line('text 3 | gt x', error='invalid finite number: x')
        self.run_line("list 1 x | keep { gt 0 }", error='keep item 2: invalid finite number: x')

    def test_continuation(self):
        result = self.run_input(
            "text a |\n  # note\n\n  upper |\n  text b\n"
            "text c\\|\n"
            "# trailing pipe in a comment does not continue |\n"
            "text d\n")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout, 'A b\nc|\nd\n')
        result = self.run_input("def report { upper |\n  # note\n  lower }\n"
                                "report X\nhelp | filter report\n")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout.split('\n')[0], 'x')
        self.assertRegex(result.stdout, r'\nreport VALUE +def: upper \| lower\n$')
        result = self.run_input('text a |\n')
        self.assertEqual(result.returncode, 1)
        self.assertIn("expected a command after '|'", result.stderr)
        with tempfile.TemporaryDirectory() as directory:
            script = Path(directory) / 'script.cmdr'
            script.write_text('text a\ntext b |\n  bogus\ntext unreachable\n')
            result = subprocess.run([str(BINARY), str(script)], capture_output=True,
                                    text=True, timeout=20)
            self.assertEqual(result.stdout, 'a\n')
            self.assertIn(f'{script}:2: unknown command: bogus', result.stderr)

    def test_os_composition(self):
        self.run_line("run printf '%s' 'hello world' | upper", 'HELLO WORLD\n')
        self.run_line('text abc | run-in tr a-z A-Z', 'ABC\n')
        self.run_line('list abc def | map run-in tr a-z A-Z | join ,', 'ABC,DEF\n')
        self.run_line("try { () run false } fallback", 'fallback\n')
        self.run_line("run printf '%s' '$HOME;*.c'", '$HOME;*.c\n')

    def test_push(self):
        self.run_line('list a b | push c "d e" | join ,', 'a,b,c,d e\n')
        self.run_line('text "a\nb\n" | push c | join ,', 'a,b,c\n')
        self.run_line('list | push | count', '0\n')
        # A list item stays one item, so fold can collect lists.
        self.run_line('list a | with { (xs) push $xs $xs } | count', '2\n')
        self.run_line('list | with { (none) list a b | fold $none { (acc x) list $x $x | '
                      'with { (p) push $acc $p } } } | each { join , } | join ";"', 'a,a;b,b\n')
        self.run_line('push', error='usage: push LIST_OR_LINES [VALUE ...]')

    def test_run_with(self):
        def run(options, program):
            return f'list {options} | with {{ (o) run-with $o {program} }}'
        with tempfile.TemporaryDirectory() as directory:
            here = Path(directory).resolve()
            self.run_line(run(f'dir {quote(here)}', 'pwd') + ' | nth 2', f'{here}\n')
            (here / 'file').write_text('')
            self.run_line(run(f'dir {quote(here / "file")}', 'pwd'), error='Not a directory')
            self.run_line(run(f'dir {quote(here / "missing")}', 'pwd'), error='No such file')
        self.run_line(run('stdin hello', 'tr a-z A-Z') + ' | nth 2', 'HELLO\n')
        # A nonzero status is a result, with stderr captured.
        self.run_line(run('', 'sh -c "echo out; echo err >&2; exit 3"') + ' | join "|"',
                      '3|out\n|err\n')
        self.run_line(run('', "sh -c 'kill -9 $$'") + ' | first', '137\n')
        start = time.monotonic()
        self.run_line(run('timeout 0.2', 'sleep 5') + ' | first', 'timeout\n')
        self.assertLess(time.monotonic() - start, 3)
        # A child that ignores SIGTERM is killed a second later.
        self.run_line(run('timeout 0.2', "sh -c 'trap \"\" TERM; sleep 5'") + ' | first', 'timeout\n')
        self.assertLess(time.monotonic() - start, 6)
        # List arguments are spliced at any depth; an empty list adds none.
        self.run_line('list --model m | with { (f) list | with { (e) list a $f $e | with { (args) '
                      'list | with { (o) run-with $o printf "[%s]" $args $e x } } } } | nth 2',
                      '[a][--model][m][x]\n')
        self.run_line(run('', 'no-such-program'), error='run-with no-such-program: No such file')
        for options, message in [('bogus 1', 'unknown option bogus'),
                                 ('dir', 'NAME VALUE pairs'),
                                 ('dir . dir .', 'option dir given twice'),
                                 ('timeout 0', 'timeout must be positive'),
                                 ('timeout x', 'invalid finite number: x')]:
            with self.subTest(options=options):
                self.run_line(run(options, 'pwd'), error=message)
        self.run_line('run-with a pwd', error='usage: run-with OPTIONS PROGRAM')
        self.run_line('list | with { (o) run-with $o }', error='usage: run-with')
        self.run_line('list | with { (o) list | with { (e) run-with $o $e } }', error='usage: run-with')

    def test_script_file(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory).resolve()
            (root / 'sub').mkdir()
            inner = root / 'sub' / 'inner.cmdr'
            inner.write_text('def where { () script-dir }\ndef which { () script-file }\nscript-dir\n')
            outer = root / 'outer.cmdr'
            outer.write_text(f'script-file\nload {quote(inner)}\nscript-dir\nwhere\nwhich\n')
            # Each resolves where it is written: where and which name inner.cmdr.
            result = subprocess.run([str(BINARY), str(outer)], capture_output=True,
                                    text=True, timeout=20, cwd='/')
            self.assertEqual(result.stdout.splitlines(),
                             [str(outer), str(root / 'sub'), str(root), str(root / 'sub'), str(inner)],
                             result.stderr)
            result = self.run_input(f'load {quote(inner)}\nwhere\nscript-dir\n')
            self.assertEqual(result.stdout.splitlines(), [str(root / 'sub')] * 2)
            self.assertIn('script-dir: not in a script file', result.stderr)
        for name in ['script-file', 'script-dir', 'script-args']:
            with self.subTest(name=name):
                self.run_line(name, error=f'{name}: not in a script file')
                self.run_line(f'{name} x', error=f'usage: {name}')

    def test_script_args(self):
        with tempfile.TemporaryDirectory() as directory:
            lib = Path(directory) / 'lib.cmdr'
            lib.write_text('def lib-args { () script-args }\nscript-args | join ,\n')
            tool = Path(directory) / 'tool.cmdr'
            tool.write_text(f'script-args | count\nscript-args | join ,\n'
                            f'load {quote(lib)} x "y z"\nlib-args | join ,\nscript-args | join ,\n')
            # Each script sees its own arguments; an empty argument is kept.
            result = subprocess.run([str(BINARY), str(tool), 'a', 'b c', ''],
                                    capture_output=True, text=True, timeout=20)
            self.assertEqual(result.stdout.splitlines(), ['3', 'a,b c,', 'x,y z', 'x,y z', 'a,b c,'],
                             result.stderr)
            result = subprocess.run([str(BINARY), str(tool)], capture_output=True,
                                    text=True, timeout=20)
            self.assertEqual(result.stdout.splitlines()[0], '0', result.stderr)
            # A def keeps its script's arguments after the script ends.
            result = self.run_input(f'load {quote(lib)} q\nlib-args\n')
            self.assertEqual(result.stdout, 'q\nq\n', result.stderr)

    def test_import(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory).resolve()
            app, lib, home = root / 'app', root / 'lib', root / 'home'
            for d in [app, lib / 'pkg', home / '.cmdr' / 'lib']:
                d.mkdir(parents=True)
            log = root / 'ran.log'
            (app / 'util.cmdr').write_text(
                f'def helper {{ upper }}\ndef shout {{ helper | text ! }}\n'
                f'def where {{ () script-dir }}\nappend x {quote(log)}\n')
            (lib / 'pkg' / '__init__.cmdr').write_text(
                'import inner\ndef twice { (x) inner.up $x | with { (y) text $y $y } }\n')
            (lib / 'pkg' / 'inner.cmdr').write_text('def up { upper }\n')
            (lib / 'pkg.cmdr').write_text('def twice { (x) text file }\n')
            (home / '.cmdr' / 'lib' / 'homemod.cmdr').write_text('def hi { () text home }\n')
            env = {**os.environ, 'CMDR_PATH': f'/nonexistent::{lib}', 'HOME': str(home)}
            def run(source, name='main.cmdr', cwd='/'):
                (app / name).write_text(source)
                return subprocess.run([str(BINARY), str(app / name)], capture_output=True,
                                      text=True, timeout=20, cwd=cwd, env=env)
            # The importer's directory first; the module runs once and prints nothing.
            result = run('import util\nimport util as u\nutil.shout hi\nu.shout hey\nutil.where\n')
            self.assertEqual(result.stdout.splitlines(), ['HI !', 'HEY !', str(app)], result.stderr)
            self.assertEqual(log.read_text(), 'x')
            # A package directory wins over NAME.cmdr; CMDR_PATH and ~/.cmdr/lib are searched.
            result = run('import pkg\npkg.twice ab\nimport homemod\nhomemod.hi\n')
            self.assertEqual(result.stdout.splitlines(), ['AB AB', 'home'], result.stderr)
            # The module's own names and imports stay inside it.
            for line, name in [('helper x', 'helper'), ('inner.up x', 'inner.up')]:
                with self.subTest(line=line):
                    result = run(f'import util\nimport pkg\n{line}\n')
                    self.assertIn(f'unknown command: {name}', result.stderr)
            result = run('import util\nhelp | filter util.\n')
            self.assertRegex(result.stdout, r'(?m)^util\.shout VALUE +def: helper \| text !$')
            # Errors; a failed import is not recorded, so it runs again.
            (app / 'loop.cmdr').write_text('import loop\n')
            (app / 'bad.cmdr').write_text('def ok { () text ok }\nbogus\n')
            result = subprocess.run([str(BINARY)], input='import loop\nimport bad\nimport bad\nimport nosuch\n',
                                    capture_output=True, text=True, timeout=20, cwd=app, env=env)
            self.assertIn('import loop: circular import', result.stderr)
            self.assertEqual(result.stderr.count('bad.cmdr:2: unknown command: bogus'), 2)
            self.assertIn(f'import nosuch: not found in .:/nonexistent:{lib}:{home}/.cmdr/lib', result.stderr)
        for source, message in [('import a.b', "invalid module name 'a.b'"),
                                ('import a/b', "invalid module name 'a/b'"),
                                ('import a as b.c', "invalid alias 'b.c'"),
                                ('import a b', 'usage: import NAME [as ALIAS]')]:
            with self.subTest(source=source):
                self.run_line(source, error=message)

    def test_defaults(self):
        cases = [
            # Omitted trailing arguments take their defaults; given ones replace them.
            ('def greet { (name greeting=hello) text $greeting $name }\ngreet bob\ngreet bob hi\n',
             'hello bob\nhi bob\n'),
            ('def greet { (name greeting=hello) text $greeting $name }\ntext bob | greet\n',
             'hello bob\n'),
            ('def three { (a b=1 c=2) text $a $b $c }\nthree x\nthree x y\nthree x y z\n',
             'x 1 2\nx y 2\nx y z\n'),
            ('def e { (a b="") list $a $b | join , }\ne x\n', 'x,\n'),
            ("def e { (a b='') list $a $b | join , }\ne x\n", 'x,\n'),
            ('def neg { (a b=-0.5) add $a $b }\nneg 1\n', '0.5\n'),
            # Blocks passed to commands take defaults too.
            ('list a b | each { (s mark=!) text $s $mark } | join ,\n', 'a !,b !\n'),
            ('list a b | each { (s mark=!) text $s $mark } ? | join ,\n', 'a ?,b ?\n'),
            ('text 3 | with { (n step=2) add $n $step }\n', '5\n'),
            ('list 1 2 3 | fold 0 { (acc x scale=10) mul $x $scale | add $acc }\n', '60\n'),
            # A nested block captures a default as it captures an argument.
            ('def mk { (y x=7) list a | each { (s) text $s $x $y } }\nmk q\n', 'a 7 q\n'),
        ]
        for source, expected in cases:
            with self.subTest(source=source):
                result = self.run_input(source)
                self.assertEqual(result.stdout, expected, result.stderr)
        errors = [
            ('def g { (a b=1) text $a $b }\ng\n', "command 'g' expects 1 to 2 arguments, got 0"),
            ('def g { (a b=1) text $a $b }\ng x y z\n', "command 'g' expects 1 to 2 arguments, got 3"),
            ('list a | each { (s m=!) text $s $m } x y\n', 'each: block takes 1 to 2 arguments, got 3'),
            # E2 still fails: a required parameter without input is an error, not a fallback.
            ('list 1 x | each { (n) try { (a b=1) add $a $b } $n }\n',
             'each item 1: try: block takes 1 to 2 arguments, got 0'),
            ('list a | each { (a b=1 c) text $a $b $c }\n', 'parameter $c has no default but follows one that does'),
            ('list a | each { (a=1) text $a }\n', 'parameter $a: the first parameter cannot have a default'),
            ('text 1 | try { (a=1 b=2) text $a $b } no\n', 'parameter $a: the first parameter cannot have a default'),
            ('list a | each { (a b=) text $a $b }\n', 'invalid default for $b'),
            ('list a | each { (a b="x y") text $a $b }\n', 'invalid default for $b'),
            ('list a | each { (a b=$a) text $a $b }\n', 'invalid default for $b'),
            ('list a | each { (a b={x}) text $a $b }\n', 'invalid default for $b'),
            ('list a | each { (a b=x"y") text $a $b }\n', 'invalid default for $b'),
            ('list a | each { (a b=1) text $a }\n', 'parameter $b is never used'),
        ]
        for source, message in errors:
            with self.subTest(source=source):
                result = self.run_input(source)
                self.assertIn(message, result.stderr)
                self.assertEqual(result.stdout, '')
        # The overview shows defaults as ...; help NAME shows them, also under a from alias.
        result = self.run_input('def r { (chain amount=50 tag="") text $chain $amount $tag }\nhelp | filter def:\nhelp r\n')
        self.assertRegex(result.stdout, r'(?m)^r CHAIN \.\.\. +def:')
        self.assertIn('r CHAIN [AMOUNT=50] [TAG=""]\ndef: (chain amount=50 tag="") text $chain $amount $tag\n', result.stdout)
        with tempfile.TemporaryDirectory() as directory:
            (Path(directory) / 'm.cmdr').write_text('def r { (c a=50) text $c $a } "R"\n')
            result = subprocess.run([str(BINARY)], input='from m import r as rr\nrr x\nhelp | filter R\nhelp rr\n',
                                    capture_output=True, text=True, timeout=20, cwd=directory)
            self.assertEqual(result.stdout.splitlines()[0], 'x 50', result.stderr)
            self.assertRegex(result.stdout, r'(?m)^rr C \.\.\. +R$')
            self.assertIn('rr C [A=50]\nR\n', result.stdout)

    def test_from_import(self):
        with tempfile.TemporaryDirectory() as directory:
            app = Path(directory).resolve()
            log = app / 'ran.log'
            (app / 'util.cmdr').write_text(
                f'def helper {{ upper }}\ndef shout {{ helper | text ! }}\n'
                f'def pair {{ (a b) text $a $b }}\ndef trim {{ (s) text trimmed $s }}\n'
                f'append x {quote(log)}\n')
            def run(source):
                (app / 'main.cmdr').write_text(source)
                return subprocess.run([str(BINARY), str(app / 'main.cmdr')], capture_output=True,
                                      text=True, timeout=20, cwd='/')
            # Names bind unqualified, or as ALIAS; the module runs once, shared with import.
            result = run('import util\nfrom util import shout pair as p\nfrom util import shout\n'
                         'shout hi\np a b\nutil.shout yo\n')
            self.assertEqual(result.stdout.splitlines(), ['HI !', 'a b', 'YO !'], result.stderr)
            self.assertEqual(log.read_text(), 'x')
            # Only the names listed bind; help shows each under its bound name.
            result = run('from util import pair as p\nshout x\n')
            self.assertIn('unknown command: shout', result.stderr)
            result = run('from util import pair as p\nhelp | filter def:\n')
            self.assertRegex(result.stdout, r'(?m)^p A B +def: \(a b\) text \$a \$b$')
            # An existing name is an error, and nothing in the statement binds;
            # as resolves it, and a later def may still shadow.
            # Piped stdin continues after an error, so shout x runs.
            result = subprocess.run([str(BINARY)], input='from util import shout trim\nshout x\n',
                                    capture_output=True, text=True, timeout=20, cwd=app)
            self.assertIn('from util import trim: trim already exists; bind it with as', result.stderr)
            self.assertIn('unknown command: shout', result.stderr)
            result = run('from util import trim as tr\ntr x\ntrim "  y  "\n')
            self.assertEqual(result.stdout.splitlines(), ['trimmed x', 'y'], result.stderr)
            result = run('def shout { lower }\nfrom util import shout\n')
            self.assertIn('shout already exists', result.stderr)
            result = run('from util import shout\ndef shout { lower }\nshout X\n')
            self.assertEqual(result.stdout, 'x\n', result.stderr)
            for source, message in [('from util import nope', 'from util import nope: util has no definition nope'),
                                    ('from util import shout shout', 'shout is bound twice'),
                                    ('from util import shout as s pair as s', 's is bound twice'),
                                    ('from nosuch import x', 'from nosuch: not found in'),
                                    ('from util import shout as a.b', "from util: invalid name 'a.b'")]:
                with self.subTest(source=source):
                    self.assertIn(message, run(source).stderr)
        for source in ['from a', 'from a b c', 'from a import', 'from a import x as',
                       'from a import as']:
            with self.subTest(source=source):
                self.run_line(source, error='usage: from NAME import DEF [as ALIAS] ...')
        self.run_line('from a.b import c', error="from: invalid module name 'a.b'")

    def test_syntax_files(self):
        editors = ROOT / 'editors'
        package = json.loads((editors / 'vscode' / 'package.json').read_text())
        grammar_path = editors / 'vscode' / package['contributes']['grammars'][0]['path']
        self.assertEqual(json.loads(grammar_path.read_text())['scopeName'], 'source.cmdr')
        json.loads((editors / 'vscode' / 'language-configuration.json').read_text())
        helix = tomllib.loads((editors / 'helix' / 'languages.toml').read_text())
        self.assertEqual(helix['language'][0]['file-types'], ['cmdr'])
        try:
            import yaml
        except ImportError:
            yaml = None
        if yaml:
            sublime = yaml.safe_load((editors / 'sublime' / 'cmdr.sublime-syntax').read_text())
            self.assertEqual(sublime['scope'], 'source.cmdr')
        if not shutil.which('vim'):
            self.skipTest('vim not found')
        source = ("# note | x\n"
                  "def shout { (s) upper $s | text \"a\\tb\" 'x y' }\n"
                  "list 1 -2.5 a1 | each { gt 0 }\n"
                  "text a#b \\| c\n"
                  "num-seq 1 3 | re-replace x '(a)' '\\1' | str-pad-left 3 0\n")
        expected = [
            [('# note | x', 'cmdrComment')],
            [('def', 'cmdrKeyword'), ('shout', 'cmdrDefName'), ('s', 'cmdrParam'),
             ('upper', 'cmdrCommand'), ('$s', 'cmdrReference'), ('|', 'cmdrPipe'),
             ('text', 'cmdrCommand'), ('\\t', 'cmdrStringEscape'), ("'x y'", 'cmdrString'),
             ('}', 'cmdrBrace')],
            [('list', 'cmdrCommand'), ('1', 'cmdrNumber'), ('-2.5', 'cmdrNumber'),
             ('a1', ''), ('each', 'cmdrCommand'), ('gt', 'cmdrCommand'), ('0', 'cmdrNumber')],
            [('text', 'cmdrCommand'), ('a#b', ''), ('\\|', 'cmdrEscape'), ('c', '')],
            # Extension commands are hyphenated; single quotes keep backslashes.
            [('num-seq', 'cmdrCommand'), ('1', 'cmdrNumber'), ('re-replace', 'cmdrCommand'),
             ("'(a)'", 'cmdrString'), ("'\\1'", 'cmdrString'), ('str-pad-left', 'cmdrCommand'),
             ('3', 'cmdrNumber')],
        ]
        with tempfile.TemporaryDirectory() as directory:
            sample, out = Path(directory) / 'sample.cmdr', Path(directory) / 'groups'
            sample.write_text(source)
            # One line per source line: each byte's syntax group, or '-'.
            dump = ('for l in range(1, line("$")) | call writefile([join(map(range(1, col([l, "$"]) - 1),'
                    ' {_, c -> synIDattr(synID(g:l, c, 1), "name")}), " ")], "' + str(out) + '", "a") | endfor')
            subprocess.run(['vim', '-Nu', 'NONE', '-es', '-i', 'NONE',
                            '--cmd', f'set rtp^={editors / "vim"}', '-c', 'filetype on',
                            '-c', 'syntax on', '-c', f'e {sample}', '-c', dump, '-c', 'qa!'],
                           timeout=20, check=True, stdin=subprocess.DEVNULL)
            groups = [line.split(' ') for line in out.read_text().splitlines()]
        self.assertEqual(len(groups), len(expected))
        for text, row, want in zip(source.splitlines(), groups, expected):
            start = 0  # Tokens are listed in order.
            for token, group in want:
                with self.subTest(line=text, token=token):
                    start = text.index(token, start)
                    self.assertEqual(set(row[start:start + len(token)]), {group})
                    start += len(token)

    def test_help_and_repl(self):
        result = self.run_line('help')
        lines = result.stdout.splitlines()[:-3]
        rows = [re.fullmatch(r'(\S(?:.*?\S)?) {2,}(\S.*)', line) for line in lines]
        self.assertTrue(all(rows), result.stdout)
        self.assertEqual(len({row.start(2) for row in rows}), 1, result.stdout)
        self.assertEqual(rows[0].groups(), ('help [NAME]', "Show commands, or NAME's full usage and documentation"))
        # help NAME gives one command; an unknown name is an error.
        self.run_line('help map', expected='map LIST_OR_LINES COMMAND [ARG ...]\nApply a command to each item\n')
        self.run_line('help nope', error='help: unknown command: nope')
        self.run_line('help a b', error='usage: help [NAME]')
        self.assertIn(('map LIST_OR_LINES COMMAND [ARG ...]',
                       'Apply a command to each item'), [row.groups() for row in rows])
        names = [line.split()[0] for line in lines]
        self.assertEqual(len(names), len(set(names)))
        self.assertTrue({'map', 'files', 'list', 'take', 'append', 'write', 'try', 'def'} <= set(names))
        self.run_line('help | filter "map LIST" | count', '1\n')
        self.run_line('help | head 1 | take 4', 'help\n')
        result = subprocess.run([str(BINARY)], input='run cat\ntext next\n',
                                capture_output=True, text=True, timeout=20)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout, '\nnext\n')
        # Empty and whitespace-only lines are silent successes.
        result = subprocess.run([str(BINARY)], input='run cat\n\n   \ntext next\n',
                                capture_output=True, text=True, timeout=20)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout, '\nnext\n')
        # Lines starting with '#' are comments; '#' elsewhere is literal.
        result = subprocess.run([str(BINARY)], input='# note\n  # indented\ntext a#b "#"\n',
                                capture_output=True, text=True, timeout=20)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout, 'a#b #\n')
        self.run_line("try { () # skipped } x", '')
        # quit and exit end only the interactive REPL.
        result = self.run_input('exit\ntext after\n')
        self.assertEqual(result.returncode, 1)
        self.assertIn('unknown command: exit', result.stderr)
        self.assertEqual(result.stdout, 'after\n')

    def pty_session(self, chunks, home, term='xterm', args=()):
        """Type each chunk into an interactive cmdr once it waits for input, as
        a person would; return its exit status and output."""
        import fcntl
        import pty
        import select
        import struct
        import termios
        import time
        editor = os.environ.get('CMDR_EDITOR') == '1'
        pid, fd = pty.fork()
        if pid == 0:
            os.environ.update(HOME=str(home), TERM=term)
            os.execv(str(BINARY), [str(BINARY), *args])
        # A real size, so linenoise need not query the cursor position.
        fcntl.ioctl(fd, termios.TIOCSWINSZ, struct.pack('HHHH', 24, 80, 0, 0))
        # linenoise enables bracketed paste once raw mode is on, which drops
        # earlier input; plain reads accept typing ahead.
        ready = b"\x1b[?2004h" if editor and term != 'dumb' else b"> "
        out, sent, deadline = b"", 0, time.time() + 10
        while True:
            done, status = os.waitpid(pid, os.WNOHANG)
            if done:
                break
            if time.time() > deadline:
                os.kill(pid, 9)
                os.waitpid(pid, 0)
                self.fail(f"REPL did not exit; output: {out!r}")
            if sent < len(chunks) and out.count(ready) > sent:
                os.write(fd, chunks[sent])
                sent += 1
            if select.select([fd], [], [], 0.02)[0]:
                try:
                    out += os.read(fd, 65536)
                except OSError:
                    pass
        os.close(fd)
        return os.waitstatus_to_exitcode(status), out

    def test_quit_and_exit(self):
        # Terminals send \r for Enter; this also checks that Enter does not
        # crash the editor (see the C_EXTENSIONS note in CMakeLists.txt).
        for word in [b"quit", b"  exit "]:
            with self.subTest(word=word), tempfile.TemporaryDirectory() as home:
                code, out = self.pty_session([b"text hi\r", word + b"\r"], home)
                self.assertEqual(code, 0, out)
                self.assertIn(b"hi\r\n", out)

    @unittest.skipUnless(os.environ.get('CMDR_EDITOR') == '1', 'built without the editor')
    def test_editor_plain_terminal(self):
        # linenoise prints the prompt as is here, so it must carry no color.
        with tempfile.TemporaryDirectory() as home:
            code, out = self.pty_session([b"text hi\r", b"quit\r"], home, term='dumb')
            self.assertEqual(code, 0, out)
            self.assertIn(b"cmdr> ", out)
            self.assertNotIn(b"\x1b[", out)

    @unittest.skipUnless(os.environ.get('CMDR_EDITOR') == '1', 'built without the editor')
    def test_editor_paste(self):
        with tempfile.TemporaryDirectory() as home:
            keys = [b"\x1b[200~text a\rtext b\x1b[201~\r",   # Bracketed paste, two lines.
                    b"\x1b[200~text c |\rupper\r\x1b[201~\r",  # Ends in a newline.
                    # Typed ahead, unbracketed. quit is in the same chunk: the
                    # harness sends a chunk per read, and these lines are three.
                    b"text d\rtext e\rquit\r"]
            code, out = self.pty_session(keys, home)
            self.assertEqual(code, 0, out)
            self.assertIn(b"\x1b[92mcmdr>\x1b[0m ", out)  # The light green prompt.
            out = re.sub(rb'\x1b\[[0-9;]*m', b'', out)
            # Each pasted line is echoed after its prompt and runs on its own.
            self.assertIn(b"cmdr> text a\r\na\r\ncmdr> text b\r\nb\r\n", out)
            self.assertIn(b"cmdr> text c |\r\n...> upper\r\nC\r\n", out)
            self.assertIn(b"\nd\r\n", out)
            self.assertIn(b"\ne\r\n", out)
            self.assertNotIn(b"\x1b[31m[...", out)  # The paste placeholder is not an error.
            history = (Path(home) / '.cmdr_history').read_text().splitlines()
            self.assertEqual(history, ['text a', 'text b', 'text c | upper', 'text d', 'text e'])

    @unittest.skipUnless(os.environ.get('CMDR_EDITOR') == '1', 'built without the editor')
    def test_editor(self):
        with tempfile.TemporaryDirectory() as home:
            keys = [b"tex\t a\r",               # Tab completes a command name.
                    b"text b |\r", b"  upper\r",   # A continued pipeline.
                    b"text c |\r", b"\x03",        # Ctrl-C drops the unfinished pipeline.
                    b"text d\r",
                    b"\x1b[A\r",                  # Up recalls the previous line.
                    b"bogus x\x03",               # Unknown command, drawn in red.
                    b"quit\r"]
            code, out = self.pty_session(keys, home)
            self.assertEqual(code, 0, out)
            self.assertIn(b"\na\r\n", out)
            self.assertIn(b"\nB\r\n", out)
            self.assertEqual(out.count(b"\nd\r\n"), 2, out)
            self.assertNotIn(b"c d", out)
            self.assertIn(b"\x1b[34mtext", out)
            self.assertIn(b"\x1b[31mbogus", out)
            history = (Path(home) / '.cmdr_history').read_text().splitlines()
            # A continued pipeline is one entry. A repeat of the previous entry
            # and quit are not stored.
            self.assertEqual(history, ['text a', 'text b |   upper', 'text d'])
            self.assertFalse((Path(home) / '.cmdr_history.tmp').exists())

    @unittest.skipUnless(os.environ.get('CMDR_EDITOR') == '1', 'built without the editor')
    def test_editor_history_save_failure(self):
        with tempfile.TemporaryDirectory() as home:
            history = Path(home) / '.cmdr_history'
            history.write_text('text old\n')
            saving = Path(home) / '.cmdr_history.tmp'
            # The save cannot open the copy; then it opens it, but writes fail.
            setups = [saving.mkdir]
            if os.path.exists('/dev/full'):
                setups.append(lambda: saving.symlink_to('/dev/full'))
            for setup in setups:
                if saving.is_dir():
                    saving.rmdir()
                setup()
                code, out = self.pty_session([b"text new\r", b"quit\r"], home)
                self.assertEqual(code, 0, out)
                self.assertIn(b"cannot save history", out)
                self.assertEqual(history.read_text(), 'text old\n')


if __name__ == '__main__':
    unittest.main()
