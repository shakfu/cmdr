# C orchestration and a minimal scripting language

C orchestration is supported today. The scripting syntax below is a design
sketch, not an implemented feature.

## C orchestration today

A `.run_value` callback can call registered commands with `cmdr_call()` and use C
for parameters, branching, loops, and intermediate values. Calls use the active
registry, so application overrides apply. Arguments are typed values; they are
not assembled into source strings or parsed again.

For example, `clean-or TEXT FALLBACK` trims its input, returns the fallback when
that input is empty, and otherwise uppercases the trimmed text:

```c
#include "cmdr.h"

static int clean_or(const cmdr *repl, void *user, size_t argc,
                    const cmdr_value *argv, cmdr_value *result, char **error)
{
    (void)user;
    if (argc != 2 || argv[0].kind != CMDR_TEXT || argv[1].kind != CMDR_TEXT)
        return cmdr_error(error, "usage: clean-or TEXT FALLBACK");

    cmdr_value trimmed = {0};
    int status = cmdr_call(repl, "trim", 1, argv, &trimmed, error);
    if (status) goto done;

    /* Overrides can return typed values, including captured failures. */
    if (trimmed.kind != CMDR_TEXT) {
        status = cmdr_error(error, "clean-or: trim must return text");
        goto done;
    }
    if (trimmed.text[0] == '\0')
        status = cmdr_value_clone(&argv[1], result, error);
    else
        status = cmdr_call(repl, "upper", 1, &trimmed, result, error);

done:
    cmdr_value_free(&trimmed);
    return status;
}
```

Add this entry to the command table in `src/commands.c`:

```c
{.name = "clean-or", .usage = "clean-or TEXT FALLBACK",
 .docstring = "Trim and uppercase text, or return FALLBACK when empty",
 .run_value = clean_or},
```

```text
clean-or '  hello  ' '(empty)'       => HELLO
text '   ' | clean-or '(empty)'     => (empty)
```

The input arguments are borrowed. `trimmed` is owned and must be freed even if a
call fails; `result` is owned by the caller. Input and output values passed to
`cmdr_call()` must not alias. Clone a borrowed value when returning it, as in the
fallback branch. Propagate errors immediately and release intermediate values.
Effects from earlier calls are not rolled back on failure.

For a fixed pipeline with runtime input, a callback can also use
`cmdr_eval_value(repl, "trim | upper", &argv[0], result, error)`. Use
`cmdr_call()` when passing multiple runtime arguments or controlling execution
in C. Neither approach requires interpolating user data into pipeline source.

## Proposed scripting surface

Keep commands and typed pipelines as the execution model. Add only named
programs, positional parameters, immutable local bindings, conditional branches,
and explicit returns. Collection operations already provide `map`, `filter`,
and other useful iteration, so the first version need not add loops.

The same command could be expressed as:

```cmdr
program clean-or(text, fallback) {
    let cleaned = trim $text
    if $cleaned == "" {
        return $fallback
    }
    return upper $cleaned
}
```

Programs register as ordinary commands:

```text
clean-or '  hello  ' '(empty)'
text '   ' | clean-or '(empty)'
list '  hello  ' '   ' | map clean-or '(empty)'
```

A program with several intermediate results might look like this:

```cmdr
program unique-lines(path) {
    let contents = read $path
    let lines = split $contents "\n"
    let total = count $lines
    if $total == "0" {
        return "Empty file"
    } else {
        return $lines | sort | uniq | join "\n"
    }
}
```

### Values and command calls

- Retain `CMDR_TEXT`, `CMDR_LIST`, `CMDR_NONE`, and `CMDR_FAILURE`. No new number
  or boolean value types are needed initially; `count` still returns text.
- Bare words and quoted strings denote text, as in existing command arguments.
  `$name` refers to an entire typed value. A list reference passes one list
  argument; it never expands into separate arguments.
- A value reference must occupy a whole token. `"$name"` is literal text;
  interpolation, word splitting, and embedded substitutions are excluded.
- An expression is a literal, a value reference, or a command pipeline. A value
  may start a pipeline (`$lines | sort`); its value becomes the next command's
  first argument. Later stages follow the existing pipeline rules.
- Command names are literal names resolved through the active registry.
  Dynamic command names are outside the initial design.
- Preserve existing single quotes and backslash escapes. In script double
  quotes, additionally decode `\n`, `\r`, `\t`, `\"`, and `\\`. This is a proposed
  script syntax extension; today's command parser does not decode `\n` into a
  newline. Do not silently change existing REPL quoting behavior.

### Parameters and scope

- Programs have a fixed number of positional parameters. Pipeline input counts
  as the first argument, just as it does for C callbacks. Validate arity before
  running the body.
- `let name = expression` evaluates once and binds the result. Bindings are
  immutable; duplicate names and parameter shadowing are errors.
- Parameters belong to the program scope. Each branch has a child scope;
  bindings inside it are unavailable after the branch. There are no globals,
  closures, or access to the caller's local bindings.
- A standalone pipeline runs for its effects and discards its returned value.
  Intermediate results are silent unless a command explicitly prints them.
- `return expression` exits the program with that value. Falling off the end
  returns `CMDR_NONE`, not the result of the last statement.

### Conditions and errors

- Initially support only `if value == value` and `if value != value`, with an
  optional `else` block. Both operands must be text; comparisons use exact byte
  equality. There is no implicit truthiness or numeric comparison.
- A command error stops execution immediately. Add program name and source
  location to the diagnostic. Completed effects remain in place.
- Captured failures remain typed values. Existing `try` and `recover` keep their
  command behavior, including `try` taking a pipeline string. That string has
  no access to script locals; scoped error handling can be designed separately.
- Apply existing failure propagation to program calls. A captured failure in
  the first argument skips a program body by default. Program definitions do
  not initially expose `.handles_failure`.

### Parsing and loading

Newlines terminate statements outside quotes; `|` at the end of a line continues
an expression. Braces delimit program and branch bodies, and `else` follows the
closing brace of its `if` on the same line. Reserve `program`, `let`, `if`,
`else`, and `return` at statement positions. Braces, `$`, and `#` can be quoted
when literal; outside quotes, `#` starts a comment at a token boundary.

A script file contains program definitions only. The embedding host explicitly
loads the file, parses all definitions, checks parameter and binding references,
and registers the programs atomically. Duplicate program names in one file are
errors. How loaded definitions interact with host overrides must be an explicit
host choice, using the existing registry precedence model. Loading executes no
program bodies; invoking a registered name executes its body.

The first implementation can expose host loading without adding REPL syntax or
a CLI flag. File loading, multiline REPL entry, and program help metadata need
separate interface decisions before implementation.

## Implementation outline

1. Add a separate script lexer and parser producing an AST with source spans.
   Keep the existing single-pipeline parser available to current callers.
2. Implement a program frame holding owned local values and borrowed parameters.
   Evaluate references without rendering values to text; clone or move values
   when they escape a frame.
3. Execute command stages through `cmdr_call()`. This preserves typed arguments,
   registry overrides, and command error behavior.
4. Represent each registered program with a `.run_value` callback and an opaque
   `user` pointer to its parsed body. The host must keep the parsed program,
   command table, and metadata alive while registered.
5. Preserve the composition depth guard through nested program calls, and bound
   parser/AST nesting as well. Free every frame on return or failure.
6. Verify arity, scopes, typed list references, pipeline input, overrides,
   branch selection, error locations, recursion limits, and cleanup on failure.

Assignment, general loops, imports, arithmetic expressions, interpolation,
closures, dynamic command names, and new exception syntax can wait until actual
programs demonstrate a need for them.
