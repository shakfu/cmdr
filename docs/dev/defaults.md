# Default parameter values

Decision record, 2026-10-19, against `d191c3b` plus the working tree. Implemented as option 2, with the first-parameter rule. The code below is as merged, except where noted under Implementation notes.

## Background

Block arity is exact ([blocks.md](blocks.md)). `{ (a b) ... }` takes two arguments and `{ ... }` takes one, received by pipe. A call with any other count fails:

```text
cmdr> def w { (a b) text $a $b }
cmdr> w a
cmdr: command 'w' expects 2 arguments, got 1
```

The rule came from scenario E2 in [blocks.md](blocks.md#piping). Before it, a block that needed input but received none still ran, and `try` turned the resulting failure into its fallback:

```text
cmdr> list 1 x 3 | each { (n) try { add 1 } $n } | join ,
1,x,3
```

The result is wrong and nothing reports it. With exact arity this is `try: block takes 1 argument, got 0`.

The check runs at call time, not at compile time. `call_block` in `src/cmdr.c` checks a def's count when it is called. `check_body` in `src/primitives.c` checks a block passed to `each`, `keep`, `not`, `with`, `fold`, `try`, `or`, and `check` before its first item runs. Compilation checks syntax, references, and command names, but not counts.

### Evidence

A `def` cannot take an optional argument, so modules work around it:

| Where | Workaround |
|-|-|
| `examples/agent-workflows/aw` | Two defs for one default: `def chain { (t specs) chain-from $t $specs "" }` |
| `examples/sox-dsl/snd` | `reverb` requires AMOUNT, though sox defaults it to 50; `fade IN OUT` cannot take sox's optional curve type; other arguments go through `effect "..."` |
| Prelude and extension wrappers | A def that wraps `num-seq FIRST LAST [STEP]`, `str-pad-left TEXT WIDTH [FILL]`, or `files [DIR]` must fix the optional argument or add a second def |

Eight C commands take an optional argument: `num-seq`, `str-pad-left`, `str-pad-right`, `files`, and `fs-dirs` take a trailing one; `try`, `or`, and `check` take a leading INPUT. A `def` cannot have either signature. Defaults give it the trailing form.

## Analysis

The proposal: a trailing parameter may declare a default, `{ (chain amount=50) ... }`. A call may omit it, and it then holds the default.

### What exact arity catches, and whether defaults keep it

| Failure | Exact arity | With trailing defaults |
|-|-|-|
| A required argument is missing, as E2 | Error | Error: required parameters are unchanged |
| Too many arguments | Error | Error: the maximum is the parameter count |
| A block expects input and receives none | Error | Error: the first parameter cannot have a default |
| A defaulted argument is omitted | Not expressible | Accepted; the default appears in the signature and in `help` |

The new silent case is the last row, and it is the feature. It applies only to parameters whose author wrote a default.

The first parameter cannot have a default. A block whose first parameter had one would accept no input, so `try { (a=1) add $a 1 } x` would run with `a` set to 1 where E2 fails: a forgotten pipe would pass silently. Every case in the evidence defaults a parameter after a required one: `reverb CHAIN [AMOUNT]`, `chain T SPECS [INPUT]`, `num-seq FIRST LAST [STEP]`. The rule costs forms such as `def greet { (name=world) ... }`, callable with or without a piped name. Allowing them later breaks no script; forbidding them after modules use them would.

### Blocks passed to commands

An earlier answer suggested forbidding defaults in blocks passed to `each`, `keep`, and the other block commands, to avoid ambiguity. There is none. Each command builds the argument list itself: `each VALUE BLOCK [ARG ...]` calls the block with the item and then the ARGs. The block's arity does not affect how the command reads its own arguments, so a range check applies as an exact one does. The ambiguity in [compose-ng.md](compose-ng.md) concerned a command's own syntax: `when` with an optional ELSE followed by optional ARGs. Defaults do not touch any command's syntax.

```text
cmdr> list a b | each { (s mark=!) text $s $mark } | join ,
a !,b !
cmdr> list a b | each { (s mark=!) text $s $mark } ? | join ,
a ?,b ?
```

### Default values

| Choice | Over | Because |
|-|-|-|
| Literal text, fixed when the block compiles | An expression evaluated at each call | Python evaluates defaults once, at `def`. cmdr values are immutable, so Python's mutable-default trap does not arise |
| A bare word, or `""` or `''` for empty text | Any quoted string | Five tokenisers read parameter lists: `src/cmdr.c`, `src/editor.c`, and the Vim, Sublime, and VS Code grammars. The last three match up to the first `)`, and `editor.c` splits on whitespace. A bare word changes none of these rules. `aw`'s `chain-from` needs `""` |
| `$name` and `{ ... }` rejected as defaults | References to enclosing parameters | A default that captures would need the capture machinery at every call. No case needs it yet |

A bare default excludes whitespace, quotes, `\`, `|`, `$`, `{`, `}`, and `)`. Allowing quoted defaults later breaks nothing, because a quote in a default is an error now.

### Syntax

| Form | Example | For | Against |
|-|-|-|-|
| `name=value` | `(chain amount=50)` | Python's form; one token; `identifier` stops at `=` | `=` in a value needs quotes, which are deferred |
| `(name value)` | `(chain (amount 50))` | Tcl's `proc` form | Nested parentheses in a parameter list; every grammar needs a second level |
| `[name value]` | `(chain [amount 50])` | Reads as optional | `[` has no meaning elsewhere in cmdr |

`name=value`, following Python.

### Keyword arguments at the call site

`reverb $c amount=60` is not proposed. A word such as `user.name=demo` is an ordinary argument today: the `aw` demo passes `-c user.name=demo` to git. Reading `NAME=VALUE` words as keywords would change those calls.

### Help and the API

`help NAME` shows a defaulted parameter in brackets with its value: `snd.reverb CHAIN [REVERBERANCE=50] ...`. The `help` overview shows a definition's defaults as `...`, so long parameter names do not widen every row. Aliases from `import` and `from` keep the usage after the name, so they show the same.

`cmdr_block_arity` changes meaning from "takes exactly argc" to "accepts argc". `cmdr_run` fills omitted parameters with their defaults. `cmdr_code` is opaque, so the ABI is unchanged; no existing block declares a default, so no caller sees a difference.

### What stays out

- **Compile-time arity checks.** The count is still checked at call time. A def call's count is known when the line compiles: the stage's words, plus one for a piped value. Checking it then is a separate change, with or without defaults.

- **Rest parameters,** `(first ...rest)`. No module needs one; `run-with` already flattens lists.

## Options

1. **Exact arity, as now.** Write a second def for each short form, `effect "..."` for full sox syntax, or `""` with `or` in the body. No language change. Every module repeats the workaround.

2. **Trailing literal defaults,** below. Required parameters keep the E2 check. About 140 lines across `src/cmdr.c`, `src/session.c`, `src/editor.c`, three grammars, `include/cmdr.h`, tests, and docs.

3. **A sentinel argument.** Callers pass `""` and the body does `or $amount { nonempty } 50`. It works today, but every call site carries the empty argument, and a real empty value cannot be told apart from the sentinel.

4. **Keyword arguments.** Rejected above: they change existing calls.

Recommended: option 2.

## Implementation of option 2

### Grammar

```text
params    = "(" { space } [ param { space+ param } ] { space } ")"
param     = identifier [ "=" default ]
default   = bare | '""' | "''"
bare      = one or more characters other than whitespace, quotes, \ | $ { } )
```

The first parameter has no default, and a parameter without a default may not follow one with a default. So a block with defaults takes at least one argument.

### Errors

| Source | Error |
|-|-|
| `{ (a=1) ... }`, `{ (a=1 b=2) ... }` | `parameter $a: the first parameter cannot have a default` |
| `{ (a b=1 c) ... }` | `parameter $c has no default but follows one that does` |
| `{ (a=) ... }`, `{ (a="x y") ... }`, `{ (a=$x) ... }` | `invalid default for $a in '{...}': use a word without spaces, quotes, or ')', or "" for empty text` |
| `def r { (c a=50) ... }` then `r` | `command 'r' expects 1 to 2 arguments, got 0` |
| `each { (s m=!) ... } x y` | `each: block takes 1 to 2 arguments, got 3` |
| `{ (a b=2) text $a }` | `parameter $b is never used`, unchanged |

### `src/cmdr.c`

`struct cmdr_code` gains the required count and the defaults:

```c
struct cmdr_code {
    size_t refs;
    size_t params;   /* Declared parameters. */
    size_t required; /* Leading parameters without defaults. */
    char **defaults; /* Texts of the last params - required parameters. */
    int declared;    /* The source starts with "(...)", possibly empty. */
    const cmdr *scope;
    program body;
};

static void free_defaults(cmdr_code *code)
{
    for (size_t i = 0; i < code->params - code->required; ++i) free(code->defaults[i]);
    free(code->defaults);
}

static void release_code(cmdr_code *code)
{
    if (--code->refs) return;
    dispose(&code->body);
    free_defaults(code);
    free(code);
}
```

`compile_block` reads `name=default`. `identifier` already stops at `=`:

```c
/* Length of a default at p: "" or '' for empty text, else a bare word. */
static size_t default_length(const char *p, int *empty)
{
    *empty = (p[0] == '"' || p[0] == '\'') && p[1] == p[0];
    if (*empty) return 2;
    size_t n = 0;
    while (p[n] && !isspace((unsigned char)p[n]) && !strchr("'\"\\|${})", p[n])) ++n;
    return n;
}

/* Whether p ends a parameter: whitespace, ')', or the end of the text. */
static int param_end(const char *p)
{
    return !*p || isspace((unsigned char)*p) || *p == ')';
}

    if (*p == '(') {
        code->declared = 1;
        for (++p;;) {
            while (isspace((unsigned char)*p)) ++p;
            if (*p == ')') { ++p; break; }
            size_t n = identifier(p), vn = 0;
            int has_default = n && p[n] == '=', empty = 0;
            if (has_default) vn = default_length(p + n + 1, &empty);
            const char *end = has_default ? p + n + 1 + vn : p + n;
            if (!n || (!has_default && !param_end(end))) {
                status = cmdr_error(error, "invalid parameter list in '{%s}'", text);
                break;
            }
            if (has_default && (!vn || !param_end(end))) {
                status = cmdr_error(error, "invalid default for $%.*s in '{%s}': use a word without "
                                    "spaces, quotes, or ')', or \"\" for empty text", (int)n, p, text);
                break;
            }
            /* A first parameter with a default would let a block run without
             * its input, the silent failure exact arity prevents. */
            if (has_default && !code->params) {
                status = cmdr_error(error, "parameter $%.*s: the first parameter cannot have a default",
                                    (int)n, p);
                break;
            }
            if (!has_default && code->required < code->params) {
                status = cmdr_error(error, "parameter $%.*s has no default but follows one that does",
                                    (int)n, p);
                break;
            }
            sc->names = cmdr_realloc(sc->names, sc->count + 1, sizeof(*sc->names));
            sc->used = cmdr_realloc(sc->used, sc->count + 1, sizeof(*sc->used));
            sc->used[sc->count] = 0;
            sc->names[sc->count++] = cmdr_strndup(p, n);
            if (has_default) {
                size_t k = code->params - code->required;
                code->defaults = cmdr_realloc(code->defaults, k + 1, sizeof(*code->defaults));
                code->defaults[k] = empty ? cmdr_strndup("", 0) : cmdr_strndup(p + n + 1, vn);
            } else {
                ++code->required;
            }
            ++code->params;
            p = end;
        }
    }
```

The failure path at the end of `compile_block` frees the defaults too:

```c
    if (status) {
        free_defaults(code);
        free(code);
    } else {
        *out = code;
    }
```

Arity becomes a range:

```c
/* A block without a parameter list takes exactly its piped input. */
static size_t min_arity(const cmdr_code *code) { return code->declared ? code->required : 1; }
static size_t max_arity(const cmdr_code *code) { return code->declared ? code->params : 1; }

static int arity_error(const char *name, const cmdr_code *code, size_t argc, char **error)
{
    size_t lo = min_arity(code), hi = max_arity(code);
    char want[64];
    if (lo == hi) snprintf(want, sizeof(want), "%zu argument%s", lo, lo == 1 ? "" : "s");
    else snprintf(want, sizeof(want), "%zu to %zu arguments", lo, hi);
    if (name) return cmdr_error(error, "command '%s' expects %s, got %zu", name, want, argc);
    return cmdr_error(error, "block takes %s, got %zu", want, argc);
}

int cmdr_block_arity(const cmdr_value *block, size_t argc, char **error)
{
    const cmdr_code *code = block->code;
    return argc >= min_arity(code) && argc <= max_arity(code) ? 0
        : arity_error(NULL, code, argc, error);
}
```

`call_block` binds omitted parameters to their defaults. The defaults are borrowed from `code`, which outlives the call, as arguments are borrowed from the caller:

```c
static int call_block(const cmdr_value *block, const char *name,
                      size_t argc, const cmdr_value *argv, cmdr_value *result, char **error)
{
    *result = (cmdr_value){0};
    const cmdr_code *code = block->code;
    if (argc < min_arity(code) || argc > max_arity(code))
        return arity_error(name, code, argc, error);
    const cmdr_value *env = block->items, *input = code->declared ? NULL : argv;
    cmdr_value *bound = NULL;
    if (code->params) {
        bound = cmdr_realloc(NULL, block->count + code->params, sizeof(*bound));
        if (block->count) memcpy(bound, block->items, block->count * sizeof(*bound));
        memcpy(bound + block->count, argv, argc * sizeof(*bound));
        /* argc >= required, so each omitted parameter has a default. */
        for (size_t i = argc; i < code->params; ++i)
            bound[block->count + i] = (cmdr_value){.kind = CMDR_TEXT,
                                                   .text = code->defaults[i - code->required]};
        env = bound;
    }
    int status = evaluate(code->scope, &code->body, name, 0, input, env, block->count + code->params,
                          result, error);
    free(bound);
    return status;
}
```

A block made inside the body captures `env`, including defaults, through `.items`. `cmdr_clone` copies captured values when a block is kept, so a closure does not point into `code->defaults`.

### `include/cmdr.h`

```c
/* Call a block with argv bound to its parameters; omitted trailing
 * parameters take their defaults. Its commands resolve where it was

 * written, not in the caller. */
int cmdr_run(const cmdr_value *block, size_t argc, const cmdr_value *argv,
             cmdr_value *result, char **error);
/* Fail unless the block accepts argc arguments: at least its parameters
 * without defaults, at most all of them. */
int cmdr_block_arity(const cmdr_value *block, size_t argc, char **error);
```

### `src/session.c`

`usage_of` writes `[NAME=VALUE]` for a defaulted parameter. The block's line compiled it, so the list is valid here:

```c
/* Help syntax from the block: "NAME A [B=1]" for { (a b=1) ... }, "NAME VALUE"
 * for { ... }, which takes one piped input. */
static char *usage_of(const char *name, const char *block)
{
    const char *p = block;
    while (isspace((unsigned char)*p)) ++p;
    size_t n = strlen(name);
    /* Each parameter gains at most a space and two brackets. */
    char *usage = cmdr_realloc(NULL, n + 2 * strlen(p) + sizeof(" VALUE"), 1), *u = usage + n;
    memcpy(usage, name, n);
    if (*p != '(') {
        u += sprintf(u, " VALUE");
    } else {
        for (++p; *p && *p != ')';) {
            if (isspace((unsigned char)*p)) { ++p; continue; }
            const char *start = p;
            while (*p && !isspace((unsigned char)*p) && *p != ')' && *p != '=') ++p;
            int has_default = *p == '=';
            *u++ = ' ';
            if (has_default) *u++ = '[';
            for (const char *c = start; c < p; ++c) *u++ = (char)toupper((unsigned char)*c);
            if (has_default) {
                while (*p && !isspace((unsigned char)*p) && *p != ')') *u++ = *p++;
                *u++ = ']';
            }
        }
    }
    *u = '\0';
    return usage;
}
```

`""` and `''` contain no space or `)`, so the copy loop keeps them whole: `CHAIN-FROM T SPECS [INPUT=""]`.

### `src/editor.c`

`scan` splits a parameter token at `=`. The name keeps the parameter colour and the default is an ordinary argument:

```c
            for (++i; i < len && buf[i] != ')'; ) {
                if (isspace((unsigned char)buf[i])) { ++i; continue; }
                token p = {PARAM, i, i, 0, 1};
                while (i < len && !isspace((unsigned char)buf[i]) && buf[i] != ')' && buf[i] != '=')
                    ++i;
                p.end = i;
                visit(&p, data);
                if (i < len && buf[i] == '=') {
                    token v = {WORD, i, i, 0, 1};
                    while (i < len && !isspace((unsigned char)buf[i]) && buf[i] != ')') ++i;
                    v.end = i;
                    visit(&v, data);
                }
            }
```

### Editor grammars

Vim, `editors/vim/syntax/cmdr.vim`:

```vim
syn region cmdrParams matchgroup=cmdrBrace start=/{\@1<=\s*(/ end=/)/ oneline
      \ contains=cmdrParam,cmdrDefault nextgroup=cmdrCommand skipwhite
syn match cmdrDefault /=[^ \t)]*/ contained
hi def link cmdrDefault Constant
```

Sublime, `editors/sublime/Cmdr.sublime-syntax`, first in `params`:

```yaml
    - match: '(=)([^\s)]*)'
      captures:
        1: keyword.operator.assignment.cmdr
        2: constant.other.cmdr
```

VS Code, `editors/vscode/syntaxes/cmdr.tmLanguage.json`, first in the parameter capture's patterns, so a default such as `=fast` is not read as a parameter:

```json
{
  "match": "(=)([^\\s)]*)",
  "captures": {
    "1": {"name": "keyword.operator.assignment.cmdr"},
    "2": {"name": "constant.other.cmdr"}
  }
}
```

### Tests

`tests/test_cmdr.py` gains `test_defaults`. CTest registers it from the method name.

```python
    def test_defaults(self):
        cases = [
            # Omitted trailing arguments take their defaults; given ones replace them.
            ('def greet { (name greeting=hello) text $greeting $name }\ngreet bob\ngreet bob hi\n',
             'hello bob\nhi bob\n'),
            ('def greet { (name greeting=hello) text $greeting $name }\ntext bob | greet\n',
             'hello bob\n'),
            ('def three { (a b=1 c=2) text $a $b $c }\nthree x\nthree x y\nthree x y z\n', 'x 1 2\nx y 2\nx y z\n'),
            ('def e { (a b="") list $a $b | join , }\ne x\n', 'x,\n'),
            ("def e { (a b='') list $a $b | join , }\ne x\n", 'x,\n'),
            # Blocks passed to commands take defaults too.
            ('list a b | each { (s mark=!) text $s $mark } | join ,\n', 'a !,b !\n'),
            ('list a b | each { (s mark=!) text $s $mark } ? | join ,\n', 'a ?,b ?\n'),
            ('text 3 | with { (n step=2) add $n $step }\n', '5\n'),
            # A nested block captures a default as it captures an argument.
            ('def mk { (x=7) list a | each { (s) text $s $x } }\nmk\n', 'a 7\n'),
        ]
        for source, expected in cases:
            with self.subTest(source=source):
                result = self.run_input(source)
                self.assertEqual(result.stdout, expected, result.stderr)
        errors = [
            ('def g { (a b=1) text $a $b }\ng\n', "command 'g' expects 1 to 2 arguments, got 0"),
            ('def g { (a b=1) text $a $b }\ng x y z\n', "command 'g' expects 1 to 2 arguments, got 3"),
            ('list a | each { (s m=!) text $s $m } x y\n', 'each: block takes 1 to 2 arguments, got 3'),
            ('list a | each { (a b=1 c) text $a $b $c }\n', 'parameter $c has no default but follows one that does'),
            ('list a | each { (a=1) text $a }\n', 'parameter $a: the first parameter cannot have a default'),
            ('text 1 | try { (a=1 b=2) text $a $b } no\n', 'parameter $a: the first parameter cannot have a default'),
            ('list a | each { (a b=) text $a $b }\n', 'invalid default for $b'),
            ('list a | each { (a b="x y") text $a $b }\n', 'invalid default for $b'),
            ('list a | each { (a b=$a) text $a $b }\n', 'invalid default for $b'),
            ('list a | each { (a b=1) text $a }\n', 'parameter $b is never used'),
        ]
        for source, message in errors:
            with self.subTest(source=source):
                self.assertIn(message, self.run_input(source).stderr)
        # help shows defaults in brackets; from keeps them under the bound name.
        result = self.run_input('def r { (chain amount=50 tag="") text $chain $amount $tag }\nhelp | filter def:\n')
        self.assertRegex(result.stdout, r'(?m)^r CHAIN \[AMOUNT=50\] \[TAG=""\] +def:')
```

`tests/test_api.c` checks the API through a host command that returns its block:

```c
static int keep_block(const cmdr *repl, void *user, size_t argc, const cmdr_value *argv,
                      cmdr_value *result, char **error)
{
    if (argc != 1 || argv[0].kind != CMDR_BLOCK) return cmdr_error(error, "usage: keep-block BLOCK");
    return cmdr_clone(&argv[0], result, error);
}

    /* A block with a default accepts 1 or 2 arguments; cmdr_run fills the second. */
    cmdr_value block;
    CHECK(!cmdr_session_eval(session, "keep-block { (a b=2) text $a $b }", &block, &error));
    CHECK(!cmdr_block_arity(&block, 1, &error) && !cmdr_block_arity(&block, 2, &error));
    CHECK(cmdr_block_arity(&block, 0, &error) && strstr(error, "1 to 2 arguments"));
    free(error);
    cmdr_value one = cmdr_text("x", 1), out;
    CHECK(!cmdr_run(&block, 1, &one, &out, &error) && !strcmp(out.text, "x 2"));
```

The existing exact-arity tests stay as they are. A block without defaults still reports `takes 2 arguments`.

### Documentation

- `README.md`, "Blocks and definitions": "A block takes exactly the arguments it declares" becomes "A block takes the arguments it declares. Trailing parameters with defaults, `(chain amount=50)`, may be omitted." Add the `help` form.

- `CHANGELOG.md`: one paragraph: the feature, trailing literal defaults over keyword arguments because `NAME=VALUE` words are ordinary arguments today, and a pointer here.

- `blocks.md`: one line under Decision pointing here.

- Editors README: no change; the grammars change in place.

### Migration candidates

These are optional; none is required.

| Module | Now | With defaults |
|-|-|-|
| `snd` | `def reverb { (chain amount) ... }` | `def reverb { (chain amount=50) ... }`, so `reverb` alone is sox's default |
| `snd` | `def norm { (chain db) ... }` | `(chain db=0)`, sox's default level |
| `snd` | `def fade { (chain in out) ... }` | `(chain in out=0)`: fade in only |
| `aw` | `chain` and `chain-from` | `def chain { (t specs input="") ... }`; keep `chain-from` as an alias for importers |

### Implementation notes

- The merged `compile_block` factors the end-of-parameter test into `param_end`, shown above.

- `test_defaults` also covers a negative default, `fold`, and an alias bound with `from`. Each error case also checks that nothing printed.

- The editor tests check only the prompt's colour, so `scan`'s split at `=` has no test of its own.

- Running the suite under ASan and UBSan found undefined behaviour outside this change: `examples/sox-dsl/ext/snd` used libsox's `SOX_ROUND_CLIP_COUNT` and `SOX_SAMPLE_MIN`, which shift 1 by 31 bits in an `int`. It now clips with its own function.

- `help` showed defaults in the overview at first. `snd.reverb`'s six sox parameter names made its row about 115 characters wide, and every row was padded to it. The overview now shows `...`, and `help NAME` gives the full signature. The help test above is as first written; the merged one checks both forms.

### Order of work

1. `src/cmdr.c` and `include/cmdr.h`, with `test_defaults` and the API test, under ASan and UBSan.

2. `src/session.c` `usage_of`, with the `help` test.

3. `src/editor.c` and the three grammars; `test_syntax_files` and the editor tests already parse them.

4. README, CHANGELOG, and the `blocks.md` pointer.

5. Migrations, each with its module's tests.
