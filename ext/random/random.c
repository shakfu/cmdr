#define _POSIX_C_SOURCE 200809L
#include "cmdr.h"

#include <fcntl.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define COMMAND(name) \
    static int name(const cmdr *repl, void *user, size_t argc, const cmdr_value *argv, \
                    cmdr_value *result, char **error)

/* MT19937 with Python's seeding and derivations, so a seed gives the values
 * Python's random module gives. One generator per thread. */
#define N 624
#define M 397

static _Thread_local struct {
    uint32_t mt[N];
    int index, seeded, has_gauss;
    double gauss;
} rng;

static void init_genrand(uint32_t s)
{
    rng.mt[0] = s;
    for (uint32_t i = 1; i < N; ++i)
        rng.mt[i] = 1812433253U * (rng.mt[i - 1] ^ (rng.mt[i - 1] >> 30)) + i;
    rng.index = N;
}

static void init_by_array(const uint32_t *key, size_t length)
{
    init_genrand(19650218U);
    size_t i = 1, j = 0;
    for (size_t k = N > length ? N : length; k; --k) {
        rng.mt[i] = (rng.mt[i] ^ ((rng.mt[i - 1] ^ (rng.mt[i - 1] >> 30)) * 1664525U)) + key[j]
                    + (uint32_t)j;
        if (++i >= N) { rng.mt[0] = rng.mt[N - 1]; i = 1; }
        if (++j >= length) j = 0;
    }
    for (size_t k = N - 1; k; --k) {
        rng.mt[i] = (rng.mt[i] ^ ((rng.mt[i - 1] ^ (rng.mt[i - 1] >> 30)) * 1566083941U))
                    - (uint32_t)i;
        if (++i >= N) { rng.mt[0] = rng.mt[N - 1]; i = 1; }
    }
    rng.mt[0] = 0x80000000U;
    rng.index = N;
    rng.has_gauss = 0;
    rng.seeded = 1;
}

/* Seed from the system's entropy, as Python does without a seed. */
static void seed_from_system(void)
{
    uint32_t key[N];
    int fd = open("/dev/urandom", O_RDONLY);
    ssize_t got = fd < 0 ? -1 : read(fd, key, sizeof(key));
    if (fd >= 0) close(fd);
    if (got != (ssize_t)sizeof(key)) {
        struct timespec t;
        clock_gettime(CLOCK_REALTIME, &t);
        key[0] = (uint32_t)t.tv_sec, key[1] = (uint32_t)t.tv_nsec, key[2] = (uint32_t)getpid();
        got = 3 * sizeof(uint32_t);
    }
    init_by_array(key, (size_t)got / sizeof(uint32_t));
}

static uint32_t genrand(void)
{
    static const uint32_t mag01[2] = {0, 0x9908b0dfU};
    if (!rng.seeded) seed_from_system();
    if (rng.index >= N) {
        uint32_t y;
        int k;
        for (k = 0; k < N - M; ++k) {
            y = (rng.mt[k] & 0x80000000U) | (rng.mt[k + 1] & 0x7fffffffU);
            rng.mt[k] = rng.mt[k + M] ^ (y >> 1) ^ mag01[y & 1];
        }
        for (; k < N - 1; ++k) {
            y = (rng.mt[k] & 0x80000000U) | (rng.mt[k + 1] & 0x7fffffffU);
            rng.mt[k] = rng.mt[k + (M - N)] ^ (y >> 1) ^ mag01[y & 1];
        }
        y = (rng.mt[N - 1] & 0x80000000U) | (rng.mt[0] & 0x7fffffffU);
        rng.mt[N - 1] = rng.mt[M - 1] ^ (y >> 1) ^ mag01[y & 1];
        rng.index = 0;
    }
    uint32_t y = rng.mt[rng.index++];
    y ^= y >> 11;
    y ^= (y << 7) & 0x9d2c5680U;
    y ^= (y << 15) & 0xefc60000U;
    return y ^ (y >> 18);
}

/* random.random(): 53 bits in [0, 1). */
static double random_double(void)
{
    uint32_t a = genrand() >> 5, b = genrand() >> 6;
    return (a * 67108864.0 + b) * (1.0 / 9007199254740992.0);
}

/* random.getrandbits(k), for k from 1 to 64. */
static uint64_t random_bits(int k)
{
    if (k <= 32) return genrand() >> (32 - k);
    uint64_t low = genrand();
    return low | (uint64_t)(genrand() >> (64 - k)) << 32;
}

/* random._randbelow(n), for n of at least 1. */
static uint64_t random_below(uint64_t n)
{
    int k = 0;
    for (uint64_t m = n; m; m >>= 1) ++k;
    uint64_t r;
    do r = random_bits(k);
    while (r >= n);
    return r;
}

/* Integers are exact up to 2^53 as numbers. */
#define MAX_INTEGER 9007199254740992.0

static int parse_integer(const char *name, const char *what, const char *text, int64_t *out,
                         char **error)
{
    double x;
    if (cmdr_number(text, &x, error)) return 1;
    if (x != floor(x) || fabs(x) > MAX_INTEGER)
        return cmdr_error(error, "%s: %s %s is not an integer within 2^53", name, what, text);
    *out = (int64_t)x;
    return 0;
}

static cmdr_value integer_value(int64_t n)
{
    char text[24];
    return cmdr_text(text, (size_t)snprintf(text, sizeof(text), "%lld", (long long)n));
}

/* A list as is, or text split into lines as list commands split it. */
static int items(const cmdr *repl, const cmdr_value *value, cmdr_value *list, char **error)
{
    if (value->kind == CMDR_LIST) return cmdr_clone(value, list, error);
    return cmdr_call(&cmdr_primitives, "lines", 1, value, list, error);
}

COMMAND(seed_command)
{
    if (argc != 1 || argv[0].kind != CMDR_TEXT) return cmdr_error(error, "usage: random.seed N");
    int64_t n;
    if (parse_integer("random.seed", "N", argv[0].text, &n, error)) return 1;
    uint64_t a = n < 0 ? (uint64_t)-n : (uint64_t)n;
    /* Python's key: the 32-bit words of |N|, least significant first. */
    uint32_t key[2] = {(uint32_t)a, (uint32_t)(a >> 32)};
    init_by_array(key, key[1] ? 2 : 1);
    *result = (cmdr_value){0};
    return 0;
}

COMMAND(random_command)
{
    if (argc) return cmdr_error(error, "usage: random.random");
    *result = cmdr_number_value(random_double());
    return 0;
}

COMMAND(int_command)
{
    if (argc != 2) return cmdr_error(error, "usage: random.int A B");
    int64_t a, b;
    if (parse_integer("random.int", "A", argv[0].text, &a, error)
        || parse_integer("random.int", "B", argv[1].text, &b, error))
        return 1;
    if (b < a) return cmdr_error(error, "random.int: empty range %s to %s", argv[0].text,
                                 argv[1].text);
    *result = integer_value(a + (int64_t)random_below((uint64_t)(b - a) + 1));
    return 0;
}

COMMAND(uniform_command)
{
    if (argc != 2) return cmdr_error(error, "usage: random.uniform A B");
    double a, b;
    if (cmdr_number(argv[0].text, &a, error) || cmdr_number(argv[1].text, &b, error)) return 1;
    *result = cmdr_number_value(a + (b - a) * random_double());
    return 0;
}

/* random.gauss: Box-Muller, keeping the second value for the next call. */
COMMAND(gauss_command)
{
    if (argc != 2) return cmdr_error(error, "usage: random.gauss MU SIGMA");
    double mu, sigma, z;
    if (cmdr_number(argv[0].text, &mu, error) || cmdr_number(argv[1].text, &sigma, error))
        return 1;
    if (rng.has_gauss) {
        z = rng.gauss;
        rng.has_gauss = 0;
    } else {
        double x2pi = random_double() * 6.283185307179586; /* Python's TWOPI */
        double g2rad = sqrt(-2.0 * log(1.0 - random_double()));
        z = cos(x2pi) * g2rad;
        rng.gauss = sin(x2pi) * g2rad;
        rng.has_gauss = 1;
    }
    *result = cmdr_number_value(mu + z * sigma);
    return 0;
}

COMMAND(choice_command)
{
    if (argc != 1) return cmdr_error(error, "usage: random.choice LIST_OR_LINES");
    cmdr_value list;
    if (items(repl, &argv[0], &list, error)) return 1;
    int status = 0;
    if (!list.count)
        status = cmdr_error(error, "random.choice: no items");
    else
        status = cmdr_clone(&list.items[random_below(list.count)], result, error);
    cmdr_free(&list);
    return status;
}

static int parse_count(const char *name, const cmdr_value *value, size_t *n, char **error)
{
    char *ignored = NULL;
    int bad = value->kind != CMDR_TEXT || cmdr_natural(value, n, &ignored);
    free(ignored);
    if (!bad) return 0;
    char *text = cmdr_describe(value);
    cmdr_error(error, "%s: K %s is not a count", name, text);
    free(text);
    return 1;
}

/* random.choices: K items with replacement; with WEIGHTS, by bisecting
 * their running sums. */
COMMAND(choices_command)
{
    if (argc < 2 || argc > 3)
        return cmdr_error(error, "usage: random.choices LIST_OR_LINES K [WEIGHTS]");
    size_t k;
    if (parse_count("random.choices", &argv[1], &k, error)) return 1;
    cmdr_value list, weights = {0};
    if (items(repl, &argv[0], &list, error)) return 1;
    if (argc == 3 && items(repl, &argv[2], &weights, error)) {
        cmdr_free(&list);
        return 1;
    }
    double *sums = NULL;
    int status = 0;
    size_t n = list.count;
    if (!n && k) {
        status = cmdr_error(error, "random.choices: no items");
        goto done;
    }
    if (argc == 3) {
        if (weights.count != n) {
            status = cmdr_error(error, "random.choices: %zu weights for %zu items",
                                weights.count, n);
            goto done;
        }
        sums = cmdr_realloc(NULL, n ? n : 1, sizeof(double));
        double total = 0;
        for (size_t i = 0; i < n; ++i) {
            double w;
            if (weights.items[i].kind != CMDR_TEXT) {
                status = cmdr_error(error, "random.choices: weight %zu is not a number", i + 1);
                goto done;
            }
            if ((status = cmdr_number(weights.items[i].text, &w, error))) goto done;
            sums[i] = total += w;
        }
        if (n && !(total > 0 && isfinite(total))) {
            status = cmdr_error(error, "random.choices: the weights total %g, not a positive number", total);
            goto done;
        }
    }
    *result = (cmdr_value){.kind = CMDR_LIST};
    for (size_t i = 0; i < k; ++i) {
        size_t j;
        if (sums) {
            double x = random_double() * sums[n - 1];
            size_t lo = 0, hi = n - 1;
            while (lo < hi) {
                size_t mid = (lo + hi) / 2;
                if (x < sums[mid]) hi = mid;
                else lo = mid + 1;
            }
            j = lo;
        } else {
            j = (size_t)floor(random_double() * (double)n);
        }
        cmdr_value item;
        if ((status = cmdr_clone(&list.items[j], &item, error))) {
            cmdr_free(result);
            goto done;
        }
        cmdr_push(result, item);
    }
done:
    free(sums);
    cmdr_free(&list);
    cmdr_free(&weights);
    return status;
}

/* random.shuffle, returning a new list. */
COMMAND(shuffle_command)
{
    if (argc != 1) return cmdr_error(error, "usage: random.shuffle LIST_OR_LINES");
    if (items(repl, &argv[0], result, error)) return 1;
    for (size_t i = result->count; i > 1; --i) {
        size_t j = random_below(i);
        cmdr_value t = result->items[i - 1];
        result->items[i - 1] = result->items[j];
        result->items[j] = t;
    }
    return 0;
}

/* random.sample: from a shrinking pool when the population is small for K,
 * else by redrawing indices already taken. */
COMMAND(sample_command)
{
    if (argc != 2) return cmdr_error(error, "usage: random.sample LIST_OR_LINES K");
    size_t k;
    if (parse_count("random.sample", &argv[1], &k, error)) return 1;
    cmdr_value list;
    if (items(repl, &argv[0], &list, error)) return 1;
    size_t n = list.count;
    if (k > n) {
        cmdr_free(&list);
        return cmdr_error(error, "random.sample: K %zu exceeds %zu items", k, n);
    }
    size_t setsize = 21;
    if (k > 5) setsize += (size_t)pow(4, ceil(log((double)k * 3) / log(4)));
    size_t *picked = cmdr_realloc(NULL, k ? k : 1, sizeof(size_t));
    if (n <= setsize) {
        size_t *pool = cmdr_realloc(NULL, n ? n : 1, sizeof(size_t));
        for (size_t i = 0; i < n; ++i) pool[i] = i;
        for (size_t i = 0; i < k; ++i) {
            size_t j = random_below(n - i);
            picked[i] = pool[j];
            pool[j] = pool[n - i - 1];
        }
        free(pool);
    } else {
        unsigned char *taken = memset(cmdr_realloc(NULL, n, 1), 0, n);
        for (size_t i = 0; i < k; ++i) {
            size_t j;
            do j = random_below(n);
            while (taken[j]);
            taken[j] = 1;
            picked[i] = j;
        }
        free(taken);
    }
    *result = (cmdr_value){.kind = CMDR_LIST};
    int status = 0;
    for (size_t i = 0; i < k && !status; ++i) {
        cmdr_value item;
        if (!(status = cmdr_clone(&list.items[picked[i]], &item, error))) cmdr_push(result, item);
    }
    if (status) cmdr_free(result);
    free(picked);
    cmdr_free(&list);
    return status;
}

#define TEXT(n, u, d, f) {.name = n, .usage = u, .docstring = d, .run = f, .text = 1}
#define VALUE(n, u, d, f) {.name = n, .usage = u, .docstring = d, .run = f}
static const cmdr_command commands[] = {
    TEXT("seed", "seed N", "Seed the generator, as Python's random.seed(N)",
         seed_command),
    TEXT("random", "random", "Return a number in [0, 1)", random_command),
    TEXT("int", "int A B", "Return an integer from A to B inclusive", int_command),
    TEXT("uniform", "uniform A B", "Return a number between A and B",
         uniform_command),
    TEXT("gauss", "gauss MU SIGMA",
         "Return a normally distributed number with mean MU", gauss_command),
    VALUE("choice", "choice LIST_OR_LINES", "Return one item", choice_command),
    VALUE("choices", "choices LIST_OR_LINES K [WEIGHTS]",
          "List K items, with replacement, in proportion to WEIGHTS", choices_command),
    VALUE("shuffle", "shuffle LIST_OR_LINES", "List the items in random order",
          shuffle_command),
    VALUE("sample", "sample LIST_OR_LINES K", "List K distinct items",
          sample_command),
};
const cmdr cmdr_ext_random = {commands, sizeof(commands) / sizeof(commands[0]), NULL};
