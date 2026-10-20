#!/usr/bin/env python3
"""Rewrite `A | with { (v) BODY }` without the with, where that is safe.

BODY becomes the pipeline, with A in v's place:

- `$v` alone as BODY's first stage: A's stages lead into the rest of BODY.
- `$v` as the first argument of BODY's first command: `A | cmd ARGS`.
- `$v` anywhere else: `$(A)` in its place.

In the first two forms A stays the pipeline's head and runs when it did. The
third moves A into an argument, so it applies only when A is short, has no
effects or random draws, and does not take the pipeline's input, and nothing
in BODY before `$v` has effects or random draws. Every form needs `$v` exactly
once, at BODY's own level: in a nested block, `$(A)` would run per call.

Prints each change unless --write. Review the result: a guard such as `check`
or `starts-with` moved into an argument still runs, but reads as a value.
See docs/dev/missing-syntax.md.
"""
import argparse
import re
import sys

# Commands with effects or random draws, by name or by the part after a dot.
EFFECT = re.compile(
    r'(^|\.)(write|append|run|run-in|run-with|load|import|from|def|as|mkdir|remove|move|'
    r'out|out-as|play|record|report|render|cut|split-at|split-silence|split-channels|'
    r'write-to|save|random|seed|int|uniform|gauss|choice|choices|shuffle|sample|'
    r'walk|pick|chance|humanize|jitter|jitter-int|reset|commit|launch|ask|edit|fix|'
    r'workflow|chain|audit|build|run-step|run-in-demo|git-in|demo-reset|demo-commit|'
    r'sandbox-probe|check-sandbox|stages|requests|request|stats|stats-field|info|info-field|'
    r'segments|repeat)$|agent|llama|step|ports')
# The longest A that moves into an argument.
MAX_MOVED = 48


def word_end(text, i):
    """The end of the word at text[i], and its kind: block, sub, ref, or word."""
    def balanced(i, open_, close):
        depth, quote = 0, None
        while i < len(text):
            c = text[i]
            if c == '\\' and quote != "'":
                i += 2
                continue
            if quote:
                if c == quote:
                    quote = None
            elif c in "'\"":
                quote = c
            elif c == open_:
                depth += 1
            elif c == close:
                depth -= 1
                if depth == 0:
                    return i + 1
            i += 1
        raise ValueError('unbalanced')

    if text[i] == '{':
        return balanced(i, '{', '}'), 'block'
    if text.startswith('$(', i):
        return balanced(i + 1, '(', ')'), 'sub'
    kind = 'ref' if text[i] == '$' else 'word'
    quote = None
    while i < len(text):
        c = text[i]
        if c == '\\' and quote != "'":
            i += 2
            continue
        if quote:
            if c == quote:
                quote = None
        elif c in "'\"":
            quote = c
        elif c.isspace() or c == '|':
            break
        i += 1
    return i, kind


def tokenize(text):
    """The pipeline's stages, each a list of (kind, source) words."""
    stages, stage, i = [], [], 0
    while i < len(text):
        c = text[i]
        if c.isspace():
            i += 1
        elif c == '|':
            stages.append(stage)
            stage = []
            i += 1
        else:
            end, kind = word_end(text, i)
            stage.append((kind, text[i:end]))
            i = end
    stages.append(stage)
    return stages


def join(stages):
    return ' | '.join(' '.join(source for _, source in stage) for stage in stages)


def split_params(inner):
    """A block's parameter names, or None, and its body."""
    m = re.match(r'\s*\(([^)]*)\)', inner)
    return (m[1].split(), inner[m.end():]) if m else (None, inner)


def effectful(stages):
    names = [stage[0][1] for stage in stages if stage and stage[0][0] == 'word']
    names += re.findall(r'\$\(\s*([^\s|)]+)', join(stages))
    return any(EFFECT.search(name) for name in names)


def rewrite(text, has_input):
    """Rewrite text, a pipeline, and its nested blocks and sub-pipelines.

    has_input: the pipeline's first stage receives a value, as in a block
    without parameters.
    """
    stages = []
    for stage in tokenize(text):
        words = []
        for kind, source in stage:
            if kind == 'block':
                params, body = split_params(source[1:-1])
                head = source[1:len(source) - 1 - len(body)]
                source = '{' + head + ' ' + rewrite(body, params is None).strip() + ' }'
                source = re.sub(r'\{\s+\(', '{ (', source)
            elif kind == 'sub':
                source = '$(' + rewrite(source[2:-1], False) + ')'
            words.append((kind, source))
        stages.append(words)
    changed = True
    while changed:  # Inner with blocks first expose outer ones.
        changed = False
        for k in range(1, len(stages)):
            stage = stages[k]
            if len(stage) != 2 or stage[0] != ('word', 'with') or stage[1][0] != 'block':
                continue
            params, body = split_params(stage[1][1][1:-1])
            if not params or len(params) != 1 or not re.fullmatch(r'[a-z][a-z0-9_-]*', params[0]):
                continue
            ref = '$' + params[0]
            a = stages[:k]
            # $x | f ARGS is f $x ARGS; the second reads as a call.
            if len(a) >= 2 and len(a[0]) == 1 and a[0][0][0] == 'ref' and a[1][0][0] == 'word':
                a = [[a[1][0], a[0][0]] + a[1][1:]] + a[2:]
            body_stages = tokenize(body)
            uses = [(i, j) for i, s in enumerate(body_stages) for j, w in enumerate(s)
                    if w == ('ref', ref)]
            if len(uses) != 1 or len(re.findall(re.escape(ref) + r'(?![\w.-])', body)) != 1:
                continue
            i, j = uses[0]
            moves = not (i == 0 and j <= 1)
            if moves and (has_input or '$(' in join(a) or len(join(a)) > MAX_MOVED
                          or effectful(a) or effectful(body_stages[:i])):
                continue
            if j == 0:
                if i != 0 or len(body_stages[0]) != 1:
                    continue
                new = a + body_stages[1:]
            elif not moves:
                new = a + [[body_stages[0][0]] + body_stages[0][2:]] + body_stages[1:]
            else:
                s = list(body_stages[i])
                s[j] = ('sub', '$(' + join(a) + ')')
                new = body_stages[:i] + [s] + body_stages[i + 1:]
            stages = new + stages[k + 1:]
            changed = True
            break
    return join(stages)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument('files', nargs='+', help='.cmdr files')
    parser.add_argument('--write', action='store_true', help='rewrite the files in place')
    parser.add_argument('--skip', action='append', default=[], metavar='FILE:LINE',
                        help='leave this line as it is; may repeat')
    args = parser.parse_args(argv)
    skip = set(args.skip)
    for path in args.files:
        with open(path) as f:
            lines = f.read().split('\n')
        changed = 0
        for n, line in enumerate(lines):
            if not line.strip() or line.lstrip().startswith('#') or f'{path}:{n + 1}' in skip:
                continue
            try:
                new = rewrite(line, False)
            except ValueError:
                continue
            if tokenize(new) != tokenize(line):
                if not args.write:
                    print(f'{path}:{n + 1}\n  - {line}\n  + {new}')
                lines[n] = new
                changed += 1
        if args.write and changed:
            with open(path, 'w') as f:
                f.write('\n'.join(lines))
        print(f'{path}: {changed} lines', file=sys.stderr)
    return 0


if __name__ == '__main__':
    sys.exit(main())
