"""Integration checks for value boundaries, effects, and error recovery."""
import os
import re
from pathlib import Path
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
BINARY = Path(os.environ.get("CMDR_BIN", ROOT / "cmdr")).resolve()


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
            ('basename /a/b.txt', 'b.txt\n'), ('dirname /a/b.txt', '/a\n'),
            ('basename /a/b/', 'b\n'), ('dirname /a/b/', '/a\n'),
            ('basename /', '/\n'), ('dirname /', '/\n'),
            ('basename ////', '/\n'), ('dirname ////', '/\n'),
            ('basename ""', '.\n'), ('dirname ""', '.\n'),
            ('dirname name', '.\n'), ('dirname a//b', 'a\n'),
            ('list /a/b /c/d | map basename | join ,', 'b,d\n'),
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
            self.run_line(f'files {quote(directory)} | filter .log | map basename | map length | join ,', '5,5,7\n')
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
        for source in ['map', 'files a b', 'basename', 'dirname', 'take a', 'drop a',
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
            ("list a b c | keep { (s v) not { equals $v } } b | join ,", 'a,c\n'),
            ("list a b c | keep { (s v) equals $v } b | join ,", 'b\n'),
            ("list | keep { nonempty } | count", '0\n'),
            # keep preserves the input representation, like filter.
            ('text "x\ny\n" | keep { equals y }', 'y\n'),
            ('text "x\ny\n" | keep { equals y } | length', '2\n'),
            ("list 'a b' c | each { (s p) text $p $s } pre | join ,", 'pre a b,pre c\n'),
            ("list a b | each { (s) upper | text [ $s ] } | join ,", 'A [ a ],B [ b ]\n'),
            ("list a b | each { (s) list $s } | count", '2\n'),
            ("list a b | each { try { equals a } no } | join ,", 'a,no\n'),
            ("text '' | not { nonempty } | length", '0\n'),
            ("text abc | contains b | upper", 'ABC\n'),
            ("text '   ' | trim | try { nonempty | upper } none", 'none\n'),
            ("text ' hi ' | trim | try { nonempty | upper } none", 'HI\n'),
            # Inner blocks see enclosing names; an inner name shadows.
            ("list a,b c | each { (r) split , | each { (f) text $f in $r } | join ';' } | join ' '",
             'a in a,b;b in a,b c in c\n'),
            ("list a | each { (x y) $y | split , | each { (x) text $x $y } | join + } b,c | join ,",
             'b b,c+c b,c\n'),
            ("list { upper } { (a) lower } | count", '2\n'),
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
            ("list a | keep { (s v) equals $v }", 'keep: block takes 2 arguments, got 1'),
            ("list a | keep { equals b } b", 'keep: block takes 1 argument, got 2'),
            ('list a | keep nonempty', 'keep: expected a block'),
            ('text a | not { bogus }', 'unknown command: bogus'),
            # Errors below the top-level body are errors too, not false.
            ("list a | keep { not equals a }", 'keep item 1: not: expected a block'),
            ("list a | keep { read }", 'keep item 1: read a'),
            ("text a | not { try { bogus } x }", 'unknown command: bogus'),
            ("list a | each { text a | }", "expected a command after '|'"),
            ('text x | not { nonempty }', "not: { nonempty } succeeded"),
            ("list a b | each { equals a }", 'each item 2: equals'),
            ("list a | each { text a", "unclosed '{'"),
            ("text {a}b", "expected a space after '}'"),
            ("text { upper }", "expects text"),
            ("list a | each " + "{ " * 200 + "}" * 200, 'blocks nested too deeply'),
            ('text "" | nonempty', 'nonempty: empty text'),
            ('list | nonempty', 'nonempty: empty list'),
            ('text a | equals b', "equals: text differs from 'b'"),
            ('text a | contains b', "contains: 'b' not found"),
            ('list a | equals a', "command 'equals' expects text"),
        ]
        for source, error in errors:
            with self.subTest(source=source):
                self.run_line(source, error=error)

    def run_input(self, source, *args):
        return subprocess.run([str(BINARY), *args], input=source,
                              capture_output=True, text=True, timeout=20)

    def test_definitions(self):
        result = self.run_input(
            "def clean-or { (s fallback) trim | try { nonempty | upper } $fallback }\n"
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
            ("def w { (a b) text $b }\nw a\n", "command 'w' expects 2 arguments, got 1"),
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
        self.assertIn("outer: inner: equals: text differs from 'x'", result.stderr)
        # A block keeps the names it captured after its def returns.
        result = self.run_input(
            "def adder { (n) try $n { equals never } { (x) add $x $n } }\n"
            "def call { (b x) try $x $b none }\n"
            "adder 10 | call 5\n"
            "def without { (xs v) keep { not { equals $v } } }\n"
            "list a b c | without b | join ,\n")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout, '15\na,c\n')

    def test_script_files(self):
        with tempfile.TemporaryDirectory() as directory:
            script = Path(directory) / 'script.cmdr'
            script.write_text(
                "#!/usr/bin/env cmdr\n"
                "# Helpers\n"
                "def shout { (s) upper | text $s ! }\n"
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
        self.assertIn('usage: cmdr [-c PIPELINE | FILE]', usage.stderr)

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
            "def stats { (xs) count | stats2 $xs }\n"
            "list b a b | stats\n"
            "list a | each { (s) upper | $s }\n")
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
            ("list 1 5 9 | keep { (n lo) gt $lo } 4 | join ,", '5,9\n'),
        ]
        for source, expected in cases:
            with self.subTest(source=source):
                self.run_line(source, expected)
        self.run_line('text 7 | lt 5', error='lt: 7 is not less than 5')
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
        self.assertRegex(result.stdout, r'\nreport +def: upper \| lower\n$')
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

    def test_help_and_repl(self):
        result = self.run_line('help')
        lines = result.stdout.splitlines()[:-2]
        rows = [re.fullmatch(r'(\S(?:.*?\S)?) {2,}(\S.*)', line) for line in lines]
        self.assertTrue(all(rows), result.stdout)
        self.assertEqual(len({row.start(2) for row in rows}), 1, result.stdout)
        self.assertEqual(rows[0].groups(), ('help', 'Show commands'))
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

    def test_quit_and_exit(self):
        import pty
        import select
        import time
        for word in [b"quit", b"  exit "]:
            with self.subTest(word=word):
                pid, fd = pty.fork()
                if pid == 0:
                    os.execv(str(BINARY), [str(BINARY)])
                os.write(fd, b"text hi\n" + word + b"\n")
                out, deadline = b"", time.time() + 5
                while not os.waitpid(pid, os.WNOHANG)[0]:
                    if time.time() > deadline:
                        os.kill(pid, 9)
                        os.waitpid(pid, 0)
                        self.fail("REPL did not exit")
                    if select.select([fd], [], [], 0.05)[0]:
                        try:
                            out += os.read(fd, 65536)
                        except OSError:
                            pass
                os.close(fd)
                self.assertIn(b"hi\r\n", out)


if __name__ == '__main__':
    unittest.main()
