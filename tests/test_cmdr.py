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
            ('text "hello world" | slice 6 3', 'wor\n'),
            ('take abc 99', 'abc\n'), ('drop abc 99', '\n'),
            ('slice abc 1 99', 'bc\n'), ('slice abc 99 2', '\n'),
            ('take abc 0', '\n'), ('drop abc 0', 'abc\n'),
            ('slice abc 1 0', '\n'),
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
            self.run_line(f'text one | tee {q} | upper', 'ONE\n')
            self.assertEqual(path.read_text(), 'one')
            self.run_line(f'text two | append {q} | length', '3\n')
            self.assertEqual(path.read_text(), 'onetwo')
            self.run_line(f'list "a\nb" c | tee {q} | count', '2\n')
            self.assertEqual(path.read_text(), 'a\nb\nc\n')
            self.run_line(f'list x y | write {q} | map upper | join ,', 'X,Y\n')
            self.assertEqual(path.read_text(), 'x\ny\n')
            self.run_line(f'list a b | map append {q} | count', '2\n')
            self.assertEqual(path.read_text(), 'x\ny\nab')
            self.run_line(f'text "" | tee {q}', '\n')
            self.assertEqual(path.read_bytes(), b'')
            self.run_line(f'text x | append {quote(path / "bad")}', error='append ')

    def test_recovery(self):
        cases = [
            ("try 'div 1 0' | recover fallback | upper", 'FALLBACK\n'),
            ("try 'div 1 0' | upper | recover fallback", 'fallback\n'),
            ("try 'text success' | recover fallback", 'success\n'),
            ("try 'unknown' | recover fallback", 'fallback\n'),
            ("try 'text x |' | recover fallback", 'fallback\n'),
            ("text hello | try upper | recover fallback", 'HELLO\n'),
            ("list a b | try 'map upper' | recover fallback | join ,", 'A,B\n'),
            ("list a b | map try unknown | map recover fallback | join ,", 'fallback,fallback\n'),
            ("list hello world | map try upper | map recover fallback | join ,", 'HELLO,WORLD\n'),
        ]
        for source, expected in cases:
            with self.subTest(source=source):
                self.run_line(source, expected)
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / 'must-not-exist'
            self.run_line(f"try 'run false' | tee {quote(path)} | recover fallback", 'fallback\n')
            self.assertFalse(path.exists())
            existing = Path(tmp) / 'exists'
            existing.write_text('a\nb')
            missing = Path(tmp) / 'missing'
            self.run_line(f'list {quote(existing)} {quote(missing)} | map try read | map recover "" | map length | join ,', '3,0\n')

    def test_failures(self):
        cases = [
            ('take abc -1', 'nonnegative integer'),
            ('drop abc 1.5', 'nonnegative integer'),
            ('slice abc 0 ""', 'nonnegative integer'),
            ('slice abc 9999999999999999999999999 1', 'nonnegative integer'),
            ('list a b | upper', 'use map or join'),
            ('list | map missing', 'unknown command'),
            ('list a b | map upper extra', 'map item 1'),
            ('list a | map list | join ,', 'expected text'),
            ("try 'run false'", 'status 1'),
            ('list a | map try unknown', 'unknown command'),
            ('split x ""', 'nonempty'),
            ('div 1 0', 'division by zero'),
            ('text x |', 'expected a command'),
            ('text x | | upper', 'expected a command before'),
            ('text "a', 'unclosed quote'),
            ("text a\\", 'unfinished escape'),
            # Captured failures must fail the pipeline instead of being
            # silently discarded or reported as "expected text".
            ('list a b | map try "div 1 0" | count', 'arithmetic expects exactly two numbers'),
            ('list a b | map try "div 1 0" | sort', 'arithmetic expects exactly two numbers'),
            ('list a b | map try "div 1 0" | join ,', 'arithmetic expects exactly two numbers'),
            ('list a b | map try "div 1 0" | basename', 'arithmetic expects exactly two numbers'),
        ]
        for source, error in cases:
            with self.subTest(source=source):
                self.run_line(source, error=error)
        for source in ['map', 'files a b', 'basename', 'dirname', 'take a', 'drop a',
                       'slice a 1', 'append a', 'tee a', 'try', 'recover a']:
            with self.subTest(source=source):
                self.run_line(source, error='usage:')

    def test_references(self):
        cases = [
            ("text x | try 'text pre $1'", 'pre x\n'),
            ("text x | try 'text $1 $1'", 'x x\n'),
            ("list a b | map try 'text [ $1 ]' | join ,", '[ a ],[ b ]\n'),
            ("text a$1 \\$1 '$1' \"$2\" _", 'a$1 $1 $1 $2 _\n'),
        ]
        for source, expected in cases:
            with self.subTest(source=source):
                self.run_line(source, expected)
        errors = [
            ('text $1', 'reference $1 has no argument'),
            ("text x | try 'text $2' | recover ok", None),
            ('text $name', "invalid reference '$name'"),
            ('text $_', "invalid reference '$_'"),
            ('text $1x', "invalid reference '$1x'"),
            ('text $0', "invalid reference '$0'"),
            ('text $', "invalid reference '$'"),
            ('$1 x', 'command name cannot be a reference'),
        ]
        for source, error in errors:
            with self.subTest(source=source):
                if error:
                    self.run_line(source, error=error)
                else:
                    self.run_line(source, 'ok\n')

    def test_combinators_and_predicates(self):
        cases = [
            ("list a bb '' | keep nonempty | join ,", 'a,bb\n'),
            ("list a bb c | keep 'contains b' | join ,", 'bb\n'),
            ("list a b | keep 'not \"equals a\"' | join ,", 'b\n'),
            ("list a b c | keep 'not \"equals $2\" $2' b | join ,", 'a,c\n'),
            ("text x | try 'equals y' | recover no", 'no\n'),
            ("list a b c | keep 'equals $2' b | join ,", 'b\n'),
            ("list | keep nonempty | count", '0\n'),
            # keep preserves the input representation, like filter.
            ('text "x\ny\n" | keep "equals y"', 'y\n'),
            ("list 'a b' c | each 'text $2 $1' pre | join ,", 'pre a b,pre c\n'),
            ("list a b | each 'upper | text [ $1 ]' | join ,", 'A [ a ],B [ b ]\n'),
            ("list a b | each 'list $1' | count", '2\n'),
            ("list a b | each 'try \"equals a\" | recover no' | join ,", 'a,no\n'),
            ("text '' | not nonempty | length", '0\n'),
            ("text abc | contains b | upper", 'ABC\n'),
            ("text '   ' | trim | try 'nonempty | upper' | recover none", 'none\n'),
            ("text ' hi ' | trim | try 'nonempty | upper' | recover none", 'HI\n'),
        ]
        for source, expected in cases:
            with self.subTest(source=source):
                self.run_line(source, expected)
        errors = [
            # Invalid bodies fail instead of acting as false (or true for not).
            ('list a | keep nonemtpy', 'keep: unknown command: nonemtpy'),
            ('list | keep bogus', 'keep: unknown command: bogus'),
            ("list a | keep 'equals $2'", 'keep: reference $2 has no argument'),
            # Nested strings do not capture the enclosing body's parameters.
            ("list a | keep 'not \"equals $2\"' b", 'keep: pipeline without references'),
            ('text a | not bogus', 'not: unknown command: bogus'),
            # Errors below the top-level body are errors too, not false.
            ("list a | keep 'not equals a'", 'keep item 1: not: pipeline without references'),
            ("list a | keep 'read'", 'keep item 1: read a'),
            ("text a | not 'try bogus'", 'not: unknown command: bogus'),
            ("list a | each 'text $1 | '", "each: expected a command after '|'"),
            ('text x | not nonempty', "not: 'nonempty' succeeded"),
            ("list a b | each 'equals a'", 'each item 2: equals'),
            ("list a | each 'try \"equals b\"'", 'each item 1: equals'),
            ('list a | map try \'equals b\' | keep nonempty', 'keep item 1: equals'),
            ('text "" | nonempty', 'nonempty: empty text'),
            ('list | nonempty', 'nonempty: empty list'),
            ('text a | equals b', "equals: text differs from 'b'"),
            ('text a | contains b', "contains: 'b' not found"),
            ('list a | equals a', 'expected text'),
        ]
        for source, error in errors:
            with self.subTest(source=source):
                self.run_line(source, error=error)

    def run_input(self, source, *args):
        return subprocess.run([str(BINARY), *args], input=source,
                              capture_output=True, text=True, timeout=20)

    def test_definitions(self):
        result = self.run_input(
            "def clean-or 'trim | try \"nonempty | upper\" | recover $2'\n"
            "def wrap 'text $2 $1 $2'\n"
            "clean-or '  hi  ' none\n"
            "text '   ' | clean-or none\n"
            "list a b | map wrap - | join ,\n"
            "help | filter def: | count\n"
            # Redefinition shadows prelude commands and earlier definitions.
            "def wrap 'text [ $1 ]'\n"
            "wrap x\n"
            "def upper 'lower'\n"
            "text ABC | upper\n")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout, 'HI\nnone\n- a -,- b -\n2\n[ x ]\nabc\n')
        errors = [
            ('def', 'usage: def NAME PIPELINE'),
            ("def 'a b' upper", "def: invalid name 'a b'"),
            ("def 'a|b' upper", "def: invalid name 'a|b'"),
            ("def '$x' upper", "def: invalid name '$x'"),
            ('def def upper', 'def: cannot redefine def'),
            ('def x bogus', 'def x: unknown command: bogus'),
            ("def x ''", 'def x: empty pipeline'),
            ("def x '# note'", 'def x: empty pipeline'),
            ("def x 'upper |'", "def x: expected a command after '|'"),
        ]
        for source, error in errors:
            with self.subTest(source=source):
                self.run_line(source, error=error)
        # Arity is checked at call time.
        result = self.run_input("def w 'text $2'\nw a\n")
        self.assertEqual(result.returncode, 1)
        self.assertIn("command 'w' expects 2 arguments, got 1", result.stderr)

    def test_lexical_definitions(self):
        result = self.run_input(
            # Naming itself calls the shadowed command, so a def can wrap it.
            "def upper 'upper | text !'\n"
            "text a | upper\n"
            # Bodies keep the commands they were defined with.
            "def shout 'upper'\n"
            "def upper 'lower'\n"
            "text a | shout\n"
            "text A | upper\n")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout, 'A !\nA !\na\n')
        # Errors from a body name the command, giving a call chain.
        result = self.run_input("def inner 'equals x'\ndef outer 'trim | inner'\ntext y | outer\n")
        self.assertIn("outer: inner: equals: text differs from 'x'", result.stderr)

    def test_script_files(self):
        with tempfile.TemporaryDirectory() as directory:
            script = Path(directory) / 'script.cmdr'
            script.write_text(
                "#!/usr/bin/env cmdr\n"
                "# Helpers\n"
                "def shout 'upper | text $1 !'\n"
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

    def test_value_stage_and_when(self):
        result = self.run_input(
            "def stats2 '$2 | sort | uniq | count | text of $1'\n"
            "def stats 'count | stats2 $1'\n"
            "list b a b | stats\n"
            "def clean-or 'trim | when nonempty upper \"$2\" $2'\n"
            "clean-or '  hi  ' none\n"
            "text '   ' | clean-or none\n")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout, '2 of 3\nHI\nnone\n')
        cases = [
            ("text a | when 'equals $2' 'text yes $1' 'text no $1' a", 'yes a\n'),
            ("text b | when 'equals $2' 'text yes $1' 'text no $1' a", 'no b\n'),
            # A mismatch from the chosen branch propagates, so when nests in keep.
            ("list a b c | keep 'when \"equals a\" nonempty \"equals c\"' | join ,", 'a,c\n'),
            ("list x | each 'when nonempty \"$1\" \"$1\"' | count", '1\n'),
        ]
        for source, expected in cases:
            with self.subTest(source=source):
                self.run_line(source, expected)
        errors = [
            ("text a | when 'head nope' upper lower", 'when COND: invalid nonnegative integer'),
            ('text a | when nonempty bogus lower', 'when THEN: unknown command: bogus'),
            ("text a | when nonempty upper 'lower |'", "when ELSE: expected a command after '|'"),
            ('text a | when nonempty upper lower extra', 'when: 1 argument unused'),
            ("text a | when nonempty upper '$2' x y", 'when: 1 argument unused'),
            ("text a | when nonempty upper '$3' x", 'when: reference $3 has no argument'),
            ('text a | when nonempty upper', 'usage: when VALUE COND THEN ELSE'),
            ('text a | upper | $1', 'reference $1 has no argument'),
            ('$1 x', 'command name cannot be a reference'),
            ('$1', 'reference $1 has no argument'),
        ]
        for source, error in errors:
            with self.subTest(source=source):
                self.run_line(source, error=error)

    def test_no_bindings(self):
        # `as` and $name references are not part of the language.
        self.run_line('text x | as y', error='unknown command: as')
        self.run_line('text $y', error="invalid reference '$y'")
        result = self.run_input("def as 'upper'\ntext a | as\n")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout, 'A\n')
        # A captured failure is not replaced by a value stage.
        result = self.run_input("def f 'try bogus | $1 | recover ok'\nf a\n")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout, 'ok\n')

    def test_numeric_predicates(self):
        cases = [
            ('text 3 | lt 5', '3\n'),
            ('text 7 | gt 5.5', '7\n'),
            ("list 1 5 9 | keep 'gt $2' 4 | join ,", '5,9\n'),
            ("text 5 | when 'lt 5' 'text low' 'text high'", '5 high\n'),
        ]
        for source, expected in cases:
            with self.subTest(source=source):
                self.run_line(source, expected)
        self.run_line('text 7 | lt 5', error='lt: 7 is not less than 5')
        self.run_line('text 3 | gt x', error='invalid finite number: x')
        self.run_line("list 1 x | keep 'gt 0'", error='keep item 2: invalid finite number: x')

    def test_continuation(self):
        result = self.run_input(
            "text a |\n  # note\n\n  upper |\n  text b\n"
            "text c\\|\n"
            "# trailing pipe in a comment does not continue |\n"
            "text d\n")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout, 'A b\nc|\nd\n')
        result = self.run_input("def report 'upper |\n  # note\n  lower'\n"
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
        self.run_line("try 'run false' | recover fallback", 'fallback\n')
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
        self.assertTrue({'map', 'files', 'list', 'take', 'append', 'tee', 'try', 'recover'} <= set(names))
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
        self.run_line("try '# skipped'", '')
        # quit and exit end only the interactive REPL.
        result = subprocess.run([str(BINARY)], input='exit\ntext after\n',
                                capture_output=True, text=True, timeout=20)
        self.assertEqual(result.returncode, 1)
        self.assertIn('unknown command: exit', result.stderr)
        self.assertEqual(result.stdout, 'after\n')


@unittest.skipUnless(os.environ.get("CMDR_LINENOISE") == "1", "built without linenoise")
class Editor(unittest.TestCase):
    """Drive the interactive REPL through a pseudo-terminal."""

    def session(self, home, keys, end=b"\x04"):
        import pty
        import select
        import time
        pid, fd = pty.fork()
        if pid == 0:
            os.environ.update(TERM="xterm", HOME=home)
            os.execv(str(BINARY), [str(BINARY)])
        out = b""

        def drain(seconds):
            nonlocal out
            end = time.time() + seconds
            while time.time() < end:
                if select.select([fd], [], [], 0.05)[0]:
                    try:
                        chunk = os.read(fd, 65536)
                    except OSError:
                        return
                    out += chunk
                    if b"\x1b[6n" in chunk:  # Answer cursor position queries.
                        os.write(fd, b"\x1b[1;200R")

        drain(0.3)
        for key in keys + ([end] if end else []):
            os.write(fd, key)
            drain(0.3)
        deadline = time.time() + 5
        while not os.waitpid(pid, os.WNOHANG)[0]:
            if time.time() > deadline:
                os.kill(pid, 9)
                os.waitpid(pid, 0)
                self.fail("REPL did not exit")
            drain(0.1)
        os.close(fd)
        return out

    def test_quit_and_exit(self):
        for word in [b"quit", b"  exit "]:
            with tempfile.TemporaryDirectory() as home:
                out = self.session(home, [b"text hi\r", word + b"\r"], end=None)
                self.assertIn(b"hi\r\n", out)

    def test_completion_history_and_highlighting(self):
        with tempfile.TemporaryDirectory() as home:
            out = self.session(home, [b"tex\t hi | upp\t\r", b"list b a |\r",
                                      b"sort | join ,\r", b"text 'q' $x\r"])
            self.assertIn(b"HI\r\n", out)
            self.assertIn(b"a,b\r\n", out)
            self.assertIn(b"\x1b[34mupper\x1b[0m", out)   # Known command.
            self.assertIn(b"\x1b[32m'q'\x1b[0m", out)     # Quoted text.
            self.assertIn(b"\x1b[35m$x\x1b[0m", out)      # Reference.
            history = Path(home, ".cmdr_history")
            self.assertEqual(history.read_text(), "text hi | upper\n"
                             "list b a | sort | join ,\ntext 'q' $x\n")
            self.assertEqual(history.stat().st_mode & 0o777, 0o600)
            # A new session recalls the previous one.
            out = self.session(home, [b"\x1b[A\x1b[A\x1b[A\r"])
            self.assertIn(b"HI\r\n", out)


if __name__ == '__main__':
    unittest.main()
