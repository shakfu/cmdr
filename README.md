# cmdr

A small C REPL for commands with positional string arguments and explicit effects.
Requires a C11 compiler, POSIX `getline`, and Make (Linux/macOS).

```sh
make
./cmdr
```

```text
cmdr> text "hello world" | upper
HELLO WORLD
cmdr> text "hello world" | upper | write greeting.txt
HELLO WORLD
cmdr> read greeting.txt | upper
HELLO WORLD
```

Run `help` for the command list. Exit with Ctrl-D. For scripts, pass a pipeline
with `./cmdr -c 'text hello | upper'`, or feed one pipeline per line on stdin.
Errors go to stderr; a failed stage stops its pipeline. The REPL continues after
errors and exits with a nonzero status if any line failed.

## Composition

Each command receives positional arguments and returns an owned string or no
value. `a | b x` evaluates `a`, then calls `b` with the returned value followed
by `x`. The value stays a single argument, even when it contains spaces, newlines,
or pipes. The REPL prints only the final returned value, adding a newline when
needed. An empty string is a value; no value cannot feed another command.

Commands run in order in the same process. `read` and `write` perform filesystem
effects; `write TEXT PATH` replaces the destination and returns TEXT so that
composition can continue. Earlier effects remain if a later stage fails. Syntax
and command names are checked for the entire pipeline before any command runs;
individual commands validate their arguments when called.

Whitespace separates arguments. Single and double quotes preserve spaces and
literal pipes, including empty arguments (`""`). Backslash escapes the next
character outside single quotes. Adjacent quoted/unquoted fragments form one
argument. There is no variable expansion, shell execution, redirection, or
background execution. Values are NUL-terminated text, not binary buffers.

## Adding C commands

Define commands in [commands.c](commands.c): implement a `cmdr_fn` from
[cmdr.h](cmdr.h), then add it to the `commands` table in `cmdr_commands_init`.
[commands.h](commands.h) exposes that initializer, which `main.c` calls to load
the command registry. Arguments exclude the command name. Example:

```c
static int first(void *user, size_t argc, const char *const *argv,
                 char **result, char **error)
{
    (void)user;
    if (argc != 1) return cmdr_error(error, "usage: first TEXT");
    *result = malloc(2);
    if (!*result) return cmdr_error(error, "out of memory");
    (*result)[0] = argv[0][0];
    (*result)[1] = '\0';
    return 0;
}

/* In the commands table: */
/* {"first", "first TEXT", first, NULL}, */
```

Return zero on success. Set `*result` to a fresh malloc-owned string, or leave it
NULL for no value. Return nonzero and use `cmdr_error` on failure. The interpreter
frees both outputs; do not return borrowed arguments or static strings. The
opaque `user` pointer lets commands access application state. The interpreter
in `cmdr.c` can also be embedded independently of the example REPL in `main.c`.
