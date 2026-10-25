/* List operations that need an index or a second list, as Python's
 * itertools and list methods give them. Results are always lists. */
#include "cmdr.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define COMMAND(name) \
    static int name(const cmdr *repl, void *user, size_t argc, const cmdr_value *argv, \
                    cmdr_value *result, char **error)

/* A list's items cloned, or text split into lines, by the primitive: repl is
 * the caller's scope, where lines may be redefined. */
static int items(const char *name, const cmdr_value *value, cmdr_value *out, char **error)
{
    if (value->kind != CMDR_TEXT && value->kind != CMDR_LIST)
        return cmdr_error(error, "%s: expected a list or lines", name);
    return cmdr_call(&cmdr_pure, "lines", 1, value, out, error);
}

static cmdr_value number(size_t n)
{
    char text[32];
    snprintf(text, sizeof(text), "%zu", n);
    return cmdr_text(text, strlen(text));
}

static cmdr_value copy(const cmdr_value *value)
{
    cmdr_value out;
    char *error = NULL;
    cmdr_clone(value, &out, &error); /* Cannot fail: value came from a clone. */
    free(error);
    return out;
}

/* Fail unless argv[at] is a block taking fixed arguments, then argv[at+1..]. */
static int check_block(const char *name, size_t argc, const cmdr_value *argv, size_t at,
                       size_t fixed, char **error)
{
    if (argc <= at || argv[at].kind != CMDR_BLOCK) return cmdr_error(error, "%s: expected a block { ... }", name);
    char *detail = NULL;
    if (!cmdr_block_arity(&argv[at], fixed + argc - at - 1, &detail)) return 0;
    cmdr_error(error, "%s: %s", name, detail);
    free(detail);
    return 1;
}

/* Call block argv[at] with fixed, then argv[at+1..]. A failure, mismatch
 * included, is an error naming item i, as each reports it. */
static int call(const char *name, size_t i, size_t argc, const cmdr_value *argv, size_t at,
                const cmdr_value *fixed, size_t nfixed, cmdr_value *out, char **error)
{
    size_t n = nfixed + argc - at - 1;
    cmdr_value *args = cmdr_realloc(NULL, n ? n : 1, sizeof(*args));
    memcpy(args, fixed, nfixed * sizeof(*args));
    for (size_t k = at + 1; k < argc; ++k) args[nfixed + k - at - 1] = argv[k];
    char *detail = NULL;
    int status = cmdr_run(&argv[at], n, args, out, &detail);
    free(args);
    if (!status && out->kind == CMDR_NONE) status = 1, detail = cmdr_strndup("block returned no value", 23);
    if (status) {
        cmdr_error(error, "%s item %zu: %s", name, i + 1, detail ? detail : "failed");
        cmdr_free(out);
    }
    free(detail);
    return status ? 1 : 0;
}

COMMAND(enumerate_command)
{
    if (argc < 1 || argc > 2) return cmdr_error(error, "usage: lists.enumerate LIST_OR_LINES [START]");
    size_t start = 1;
    if (argc == 2 && cmdr_natural(&argv[1], &start, error)) return 1;
    cmdr_value xs;
    if (items("lists.enumerate", &argv[0], &xs, error)) return 1;
    *result = (cmdr_value){.kind = CMDR_LIST};
    for (size_t i = 0; i < xs.count; ++i) {
        cmdr_value pair = {.kind = CMDR_LIST};
        cmdr_push(&pair, number(start + i));
        cmdr_push(&pair, xs.items[i]);
        xs.items[i] = (cmdr_value){0};
        cmdr_push(result, pair);
    }
    cmdr_free(&xs);
    return 0;
}

/* As Python's zip: as long as the shortest. */
COMMAND(zip_command)
{
    if (!argc) return cmdr_error(error, "usage: lists.zip LIST_OR_LINES [LIST_OR_LINES ...]");
    cmdr_value *lists = cmdr_realloc(NULL, argc, sizeof(*lists));
    size_t n = (size_t)-1, got = 0;
    int status = 0;
    for (; got < argc && !status; ++got) {
        status = items("lists.zip", &argv[got], &lists[got], error);
        if (!status && lists[got].count < n) n = lists[got].count;
    }
    if (status) --got;
    *result = (cmdr_value){.kind = CMDR_LIST};
    for (size_t i = 0; !status && i < n; ++i) {
        cmdr_value row = {.kind = CMDR_LIST};
        for (size_t j = 0; j < argc; ++j) cmdr_push(&row, copy(&lists[j].items[i]));
        cmdr_push(result, row);
    }
    for (size_t j = 0; j < got; ++j) cmdr_free(&lists[j]);
    free(lists);
    if (status) cmdr_free(result);
    return status;
}

COMMAND(chain_command)
{
    *result = (cmdr_value){.kind = CMDR_LIST};
    for (size_t i = 0; i < argc; ++i) {
        cmdr_value xs;
        if (items("lists.chain", &argv[i], &xs, error)) { cmdr_free(result); return 1; }
        for (size_t j = 0; j < xs.count; ++j) {
            cmdr_push(result, xs.items[j]);
            xs.items[j] = (cmdr_value){0};
        }
        cmdr_free(&xs);
    }
    return 0;
}

/* One level: a list item adds its items, a text item itself, as concat does. */
COMMAND(flatten_command)
{
    if (argc != 1) return cmdr_error(error, "usage: lists.flatten LIST_OR_LINES");
    cmdr_value xs;
    if (items("lists.flatten", &argv[0], &xs, error)) return 1;
    *result = (cmdr_value){.kind = CMDR_LIST};
    for (size_t i = 0; i < xs.count; ++i) {
        cmdr_value *x = &xs.items[i];
        if (x->kind != CMDR_LIST) {
            cmdr_push(result, *x);
            *x = (cmdr_value){0};
            continue;
        }
        for (size_t j = 0; j < x->count; ++j) {
            cmdr_push(result, x->items[j]);
            x->items[j] = (cmdr_value){0};
        }
    }
    cmdr_free(&xs);
    return 0;
}

/* Item I, counting from 1, replaced by VALUE. */
COMMAND(set_at_command)
{
    if (argc != 3) return cmdr_error(error, "usage: lists.set-at LIST_OR_LINES I VALUE");
    size_t i;
    if (cmdr_natural(&argv[1], &i, error)) return 1;
    cmdr_value xs, value;
    if (items("lists.set-at", &argv[0], &xs, error)) return 1;
    if (!i || i > xs.count) { /* An error, not a mismatch: or must not hide a bad write. */
        cmdr_error(error, "lists.set-at: index %zu out of range for %zu items", i, xs.count);
        cmdr_free(&xs);
        return 1;
    }
    if (cmdr_clone(&argv[2], &value, error)) { cmdr_free(&xs); return 1; }
    cmdr_free(&xs.items[i - 1]);
    xs.items[i - 1] = value;
    *result = xs;
    return 0;
}

/* The position, from 1, of the first item equal to VALUE. */
COMMAND(index_of_command)
{
    if (argc != 2 || argv[1].kind != CMDR_TEXT) return cmdr_error(error, "usage: lists.index-of LIST_OR_LINES TEXT");
    cmdr_value xs;
    if (items("lists.index-of", &argv[0], &xs, error)) return 1;
    size_t i = 0;
    while (i < xs.count && (xs.items[i].kind != CMDR_TEXT || strcmp(xs.items[i].text, argv[1].text))) ++i;
    int found = i < xs.count;
    cmdr_free(&xs);
    if (found) { *result = number(i + 1); return 0; }
    char *d = cmdr_describe(&argv[1]);
    cmdr_error(error, "lists.index-of: %s is not an item", d);
    free(d);
    return CMDR_MISMATCH;
}

typedef struct {
    cmdr_value key, item;
    double n;
    size_t index;
} keyed;

/* Stable: equal keys keep their order. */
static int by_key(const void *a, const void *b)
{
    const keyed *x = a, *y = b;
    int c = strcmp(x->key.text, y->key.text);
    return c ? c : (x->index > y->index) - (x->index < y->index);
}

static int by_number(const void *a, const void *b)
{
    const keyed *x = a, *y = b;
    if (x->n != y->n) return x->n < y->n ? -1 : 1;
    return (x->index > y->index) - (x->index < y->index);
}

/* Each item with the key its block returns, which must be text, and for
 * numeric a number. */
static keyed *keys(const char *name, int numeric, size_t argc, const cmdr_value *argv,
                   size_t *count, char **error)
{
    cmdr_value xs;
    if (check_block(name, argc, argv, 1, 1, error) || items(name, &argv[0], &xs, error)) return NULL;
    keyed *ks = cmdr_realloc(NULL, xs.count ? xs.count : 1, sizeof(*ks));
    size_t n = 0;
    int status = 0;
    for (; n < xs.count; ++n) {
        ks[n] = (keyed){.item = xs.items[n], .index = n};
        xs.items[n] = (cmdr_value){0};
        if ((status = call(name, n, argc, argv, 1, &ks[n].item, 1, &ks[n].key, error))) break;
        if (ks[n].key.kind != CMDR_TEXT) {
            status = cmdr_error(error, "%s item %zu: the key must be text", name, n + 1);
            break;
        }
        if (numeric && cmdr_number(ks[n].key.text, &ks[n].n, error)) {
            status = 1;
            break;
        }
    }
    if (status) { /* ks[0..n] hold items; n failed. */
        for (size_t i = 0; i <= n; ++i) cmdr_free(&ks[i].item), cmdr_free(&ks[i].key);
        free(ks);
        cmdr_free(&xs);
        return NULL;
    }
    cmdr_free(&xs);
    *count = n;
    return ks;
}

static int sort_by(const char *name, int numeric, size_t argc, const cmdr_value *argv,
                   cmdr_value *result, char **error)
{
    if (argc < 2) return cmdr_error(error, "usage: %s LIST_OR_LINES BLOCK [ARG ...]", name);
    size_t n;
    keyed *ks = keys(name, numeric, argc, argv, &n, error);
    if (!ks) return 1;
    if (n) qsort(ks, n, sizeof(*ks), numeric ? by_number : by_key);
    *result = (cmdr_value){.kind = CMDR_LIST};
    for (size_t i = 0; i < n; ++i) {
        cmdr_push(result, ks[i].item);
        cmdr_free(&ks[i].key);
    }
    free(ks);
    return 0;
}
COMMAND(sort_by_command) { return sort_by("lists.sort-by", 0, argc, argv, result, error); }
COMMAND(sort_n_by_command) { return sort_by("lists.sort-n-by", 1, argc, argv, result, error); }

/* As itertools.groupby: runs of adjacent items with equal keys, each as KEY
 * and a list of its items. */
COMMAND(group_by_command)
{
    if (argc < 2) return cmdr_error(error, "usage: lists.group-by LIST_OR_LINES BLOCK [ARG ...]");
    size_t n;
    keyed *ks = keys("lists.group-by", 0, argc, argv, &n, error);
    if (!ks) return 1;
    *result = (cmdr_value){.kind = CMDR_LIST};
    for (size_t i = 0; i < n; ++i) {
        if (!i || strcmp(ks[i].key.text, ks[i - 1].key.text)) {
            cmdr_value group = {.kind = CMDR_LIST};
            cmdr_push(&group, copy(&ks[i].key));
            cmdr_push(&group, (cmdr_value){.kind = CMDR_LIST});
            cmdr_push(result, group);
        }
        cmdr_value *last = &result->items[result->count - 1];
        cmdr_push(&last->items[1], ks[i].item);
    }
    for (size_t i = 0; i < n; ++i) cmdr_free(&ks[i].key);
    free(ks);
    return 0;
}

/* As itertools.batched: lists of N items, the last perhaps shorter. */
COMMAND(batched_command)
{
    if (argc != 2) return cmdr_error(error, "usage: lists.batched LIST_OR_LINES N");
    size_t size;
    if (cmdr_natural(&argv[1], &size, error)) return 1;
    if (!size) return cmdr_error(error, "lists.batched: N must be at least 1");
    cmdr_value xs;
    if (items("lists.batched", &argv[0], &xs, error)) return 1;
    *result = (cmdr_value){.kind = CMDR_LIST};
    for (size_t i = 0; i < xs.count; ++i) {
        if (i % size == 0) cmdr_push(result, (cmdr_value){.kind = CMDR_LIST});
        cmdr_push(&result->items[result->count - 1], xs.items[i]);
        xs.items[i] = (cmdr_value){0};
    }
    cmdr_free(&xs);
    return 0;
}

/* As itertools.accumulate: the first item, then each block result, as fold
 * returns only the last. */
COMMAND(accumulate_command)
{
    if (argc < 2) return cmdr_error(error, "usage: lists.accumulate LIST_OR_LINES BLOCK [ARG ...]");
    cmdr_value xs;
    if (check_block("lists.accumulate", argc, argv, 1, 2, error) || items("lists.accumulate", &argv[0], &xs, error))
        return 1;
    *result = (cmdr_value){.kind = CMDR_LIST};
    int status = 0;
    for (size_t i = 0; i < xs.count && !status; ++i) {
        if (!i) { cmdr_push(result, copy(&xs.items[0])); continue; }
        cmdr_value args[2] = {result->items[result->count - 1], xs.items[i]}, next;
        if (!(status = call("lists.accumulate", i, argc, argv, 1, args, 2, &next, error))) cmdr_push(result, next);
    }
    cmdr_free(&xs);
    if (status) cmdr_free(result);
    return status;
}

static const cmdr_command commands[] = {
    {.name = "enumerate", .usage = "enumerate LIST_OR_LINES [START]",
     .docstring = "List each item as a pair: its position, from START or 1, then the item", .run = enumerate_command},
    {.name = "zip", .usage = "zip LIST_OR_LINES [LIST_OR_LINES ...]",
     .docstring = "List the Nth items of each list together, as long as the shortest", .run = zip_command},
    {.name = "chain", .usage = "chain [LIST_OR_LINES ...]",
     .docstring = "List the items of each argument, in turn", .run = chain_command},
    {.name = "flatten", .usage = "flatten LIST_OR_LINES",
     .docstring = "List the items of each list item, and each text item itself", .run = flatten_command},
    {.name = "set-at", .usage = "set-at LIST_OR_LINES I VALUE",
     .docstring = "Return the list with item I, counting from 1, replaced by VALUE", .run = set_at_command},
    {.name = "index-of", .usage = "index-of LIST_OR_LINES TEXT",
     .docstring = "Return the position, from 1, of the first item equal to TEXT", .run = index_of_command},
    {.name = "sort-by", .usage = "sort-by LIST_OR_LINES BLOCK [ARG ...]",
     .docstring = "Sort items by the text the block returns for each, keeping ties in order", .run = sort_by_command},
    {.name = "sort-n-by", .usage = "sort-n-by LIST_OR_LINES BLOCK [ARG ...]",
     .docstring = "Sort items by the number the block returns for each, keeping ties in order", .run = sort_n_by_command},
    {.name = "group-by", .usage = "group-by LIST_OR_LINES BLOCK [ARG ...]",
     .docstring = "List runs of adjacent items with equal keys, each as KEY and its items", .run = group_by_command},
    {.name = "batched", .usage = "batched LIST_OR_LINES N",
     .docstring = "Split items into lists of N, the last perhaps shorter", .run = batched_command},
    {.name = "accumulate", .usage = "accumulate LIST_OR_LINES BLOCK [ARG ...]",
     .docstring = "List the first item, then each result of the block on the last result and the next item",
     .run = accumulate_command},
};
const cmdr cmdr_ext_lists = {commands, sizeof(commands) / sizeof(commands[0]), NULL};
