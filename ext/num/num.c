#include "cmdr.h"

#include <math.h>

#define COMMAND(name) \
    static int name(const cmdr *repl, void *user, size_t argc, const cmdr_value *argv, \
                    cmdr_value *result, char **error)

/* A list longer than this is more likely a typo than a loop. */
#define SEQ_LIMIT 10000000

COMMAND(seq_command)
{
    if (argc < 2 || argc > 3) return cmdr_error(error, "usage: num.seq FIRST LAST [STEP]");
    double first, last, step = 1;
    if (cmdr_number(argv[0].text, &first, error) || cmdr_number(argv[1].text, &last, error)
        || (argc == 3 && cmdr_number(argv[2].text, &step, error)))
        return 1;
    if (step == 0) return cmdr_error(error, "num.seq: STEP must be nonzero");
    /* The tolerance keeps LAST when rounding leaves it a hair past, as in 0 0.3 0.1. */
    double span = (last - first) / step;
    if (!isfinite(span)) return cmdr_error(error, "num.seq: too many items");
    double count = span < 0 ? 0 : floor(span + 1e-9) + 1;
    if (count > SEQ_LIMIT)
        return cmdr_error(error, "num.seq: %.0f items exceeds %d", count, SEQ_LIMIT);
    *result = (cmdr_value){.kind = CMDR_LIST};
    /* Multiply rather than add, so error does not accumulate. */
    for (size_t i = 0; i < (size_t)count; ++i)
        cmdr_push(result, cmdr_number_value(first + (double)i * step));
    return 0;
}

/* The remainder has B's sign, so num.mod -1 3 is 2. */
COMMAND(mod_command)
{
    if (argc != 2) return cmdr_error(error, "usage: num.mod A B");
    double a, b;
    if (cmdr_number(argv[0].text, &a, error) || cmdr_number(argv[1].text, &b, error)) return 1;
    if (b == 0) return cmdr_error(error, "num.mod: division by zero");
    double r = fmod(a, b);
    if (r != 0 && (r < 0) != (b < 0)) r += b;
    *result = cmdr_number_value(r);
    return 0;
}

static int unary(double (*f)(double), const char *name, size_t argc, const cmdr_value *argv,
                 cmdr_value *result, char **error)
{
    if (argc != 1) return cmdr_error(error, "usage: %s NUMBER", name);
    double x;
    if (cmdr_number(argv[0].text, &x, error)) return 1;
    /* Adding 0 turns -0, as from num.ceil -0.5, into 0. */
    *result = cmdr_number_value(f(x) + 0.0);
    return 0;
}
COMMAND(floor_command) { return unary(floor, "num.floor", argc, argv, result, error); }
COMMAND(ceil_command) { return unary(ceil, "num.ceil", argc, argv, result, error); }
COMMAND(round_command) { return unary(round, "num.round", argc, argv, result, error); }
COMMAND(abs_command) { return unary(fabs, "num.abs", argc, argv, result, error); }

#define TEXT(n, u, d, f) {.name = n, .usage = u, .docstring = d, .run = f, .text = 1}
static const cmdr_command commands[] = {
    TEXT("seq", "seq FIRST LAST [STEP]", "List numbers from FIRST to LAST", seq_command),
    TEXT("mod", "mod A B", "Return A modulo B, with B's sign", mod_command),
    TEXT("floor", "floor NUMBER", "Round down", floor_command),
    TEXT("ceil", "ceil NUMBER", "Round up", ceil_command),
    TEXT("round", "round NUMBER", "Round to nearest, halves away from zero", round_command),
    TEXT("abs", "abs NUMBER", "Return the absolute value", abs_command),
};
const cmdr cmdr_ext_num = {commands, sizeof(commands) / sizeof(commands[0]), NULL};
