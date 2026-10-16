"""Integration checks for value boundaries, effects, and error recovery."""
import os
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
        ]
        for source, expected in cases:
            with self.subTest(source=source):
                self.run_line(source, expected)

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
        ]
        for source, error in cases:
            with self.subTest(source=source):
                self.run_line(source, error=error)
        for source in ['map', 'files a b', 'basename', 'dirname', 'take a', 'drop a',
                       'slice a 1', 'append a', 'tee a', 'try', 'recover a']:
            with self.subTest(source=source):
                self.run_line(source, error='usage:')

    def test_os_composition(self):
        self.run_line("run printf '%s' 'hello world' | upper", 'HELLO WORLD\n')
        self.run_line('text abc | run-in tr a-z A-Z', 'ABC\n')
        self.run_line('list abc def | map run-in tr a-z A-Z | join ,', 'ABC,DEF\n')
        self.run_line("try 'run false' | recover fallback", 'fallback\n')
        self.run_line("run printf '%s' '$HOME;*.c'", '$HOME;*.c\n')

    def test_help_and_repl(self):
        result = self.run_line('help')
        names = [line.split()[0] for line in result.stdout.splitlines()[:-2]]
        self.assertEqual(len(names), len(set(names)))
        self.assertTrue({'map', 'files', 'list', 'take', 'append', 'tee', 'try', 'recover'} <= set(names))
        result = subprocess.run([str(BINARY)], input='run cat\ntext next\n',
                                capture_output=True, text=True, timeout=20)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout, '\nnext\n')


if __name__ == '__main__':
    unittest.main()
