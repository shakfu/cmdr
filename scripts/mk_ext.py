#!/usr/bin/env python3
"""Create a skeleton extension, ext/NAME/, that builds and passes its tests.

See README.md, "Extensions", for the layout.
"""
import argparse
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]

# One unquoted cmdr word that is also a C identifier once '-' becomes '_'.
NAME = re.compile(r"[a-z][a-z0-9]*(-[a-z0-9]+)*")

C_SOURCE = """\
#include "cmdr.h"

#include <string.h>

/* {name} TEXT: replace with the command's behaviour. */
static int {id}_command(const cmdr *repl, void *user, size_t argc, const cmdr_value *argv,
{pad}cmdr_value *result, char **error)
{{
    if (argc != 1) return cmdr_error(error, "usage: {name} TEXT");
    *result = cmdr_text(argv[0].text, strlen(argv[0].text));
    return 0;
}}

static const cmdr_command commands[] = {{
    {{.name = "{name}", .usage = "{name} TEXT", .docstring = "Return TEXT unchanged",
     .run = {id}_command, .text = 1}},
}};
const cmdr cmdr_ext_{id} = {{commands, sizeof(commands) / sizeof(commands[0]), NULL}};
"""

CMDR_SOURCE = """\
# Definitions for ext/{name}. Extension scripts run before the prelude, so
# they may use only primitives and extension commands.
"""

README = """\
# {name}

TODO: what the extension does.

| Command | Result |
|-|-|
| `{name} TEXT` | `TEXT`, unchanged. |

TODO: why this needs C, not a `def`.
"""

TEST = """\
# Each line must succeed; cmdr stops at the first that fails.
{name} hello | equals hello
"""

C_TEST = """\
/* What test.cmdr cannot check, such as error messages. */
#define _POSIX_C_SOURCE 200809L
#include "cmdr.h"
#include "ext_check.h"

extern const cmdr cmdr_ext_{id};

int main(void)
{{
    const cmdr table = {{cmdr_ext_{id}.commands, cmdr_ext_{id}.count, &cmdr_primitives}};
    expect(&table, "{name} hello", "hello");
    expect_error(&table, "{name}", 1, "usage: {name} TEXT");
    puts("test_{name}: ok");
    return 0;
}}
"""


def files(name, c_test=False):
    """Map each skeleton path, relative to ext/NAME/, to its contents."""
    id = name.replace("-", "_")
    pad = " " * len(f"static int {id}_command(")
    fields = {"name": name, "id": id, "pad": pad}
    paths = {
        f"{name}.c": C_SOURCE.format(**fields),
        f"{name}.cmdr": CMDR_SOURCE.format(**fields),
        "README.md": README.format(**fields),
        "tests/test.cmdr": TEST.format(**fields),
    }
    if c_test:
        paths[f"tests/test_{name}.c"] = C_TEST.format(**fields)
    return paths


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("name", help="extension and command name, such as my-ext")
    parser.add_argument("--root", type=Path, default=ROOT / "ext",
                        help="directory to create NAME/ in (default: %(default)s)")
    parser.add_argument("--c-test", action="store_true",
                        help="also create tests/test_NAME.c, a C test program")
    args = parser.parse_args(argv)
    if not NAME.fullmatch(args.name):
        parser.error(f"invalid name '{args.name}': use lowercase letters, digits, "
                     "and single hyphens, starting with a letter")
    target = args.root / args.name
    try:
        target.mkdir(parents=True)  # Fails if it exists, so nothing is overwritten.
    except FileExistsError:
        print(f"mk_ext.py: {target} already exists", file=sys.stderr)
        return 1
    (target / "tests").mkdir()
    for path, text in files(args.name, args.c_test).items():
        out = target / path
        out.write_text(text)
        print(out)
    return 0


if __name__ == "__main__":
    sys.exit(main())
