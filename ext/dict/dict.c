#define _POSIX_C_SOURCE 200809L
#include "cmdr.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define COMMAND(name) \
    static int name(const cmdr *repl, void *user, size_t argc, const cmdr_value *argv, \
                    cmdr_value *result, char **error)

static int require_dict(const char *name, const cmdr_value *v, char **error)
{
    if (cmdr_kind_of(v) == CMDR_DICT) return 0;
    char *d = cmdr_describe(v);
    int failed = cmdr_error(error, "%s: expected a dict, got %s", name, d);
    free(d);
    return failed;
}

static int require_key(const char *name, const cmdr_value *v, char **error)
{
    if (cmdr_kind_of(v) == CMDR_TEXT) return 0;
    char *d = cmdr_describe(v);
    int failed = cmdr_error(error, "%s: KEY must be text, got %s", name, d);
    free(d);
    return failed;
}

/* Put a copy of value into dict under key, freeing dict on failure. */
static int put_pair(const char *name, cmdr_value *dict, const cmdr_value *key,
                    const cmdr_value *value, char **error)
{
    cmdr_value v;
    if (require_key(name, key, error) || cmdr_clone(value, &v, error)) {
        cmdr_free(dict);
        return 1;
    }
    cmdr_dict_put(dict, cmdr_text_of(key), v);
    return 0;
}

/* Put each KEY VALUE pair of argv into dict, which is freed on failure. */
static int put_pairs(const char *name, cmdr_value *dict, size_t argc, const cmdr_value *argv,
                     char **error)
{
    for (size_t i = 0; i + 1 < argc; i += 2)
        if (put_pair(name, dict, &argv[i], &argv[i + 1], error)) return 1;
    return 0;
}

COMMAND(of_command)
{
    if (argc % 2) return cmdr_error(error, "usage: dict.of [KEY VALUE ...]");
    *result = cmdr_dict();
    return put_pairs("dict.of", result, argc, argv, error);
}

COMMAND(from_items_command)
{
    if (argc != 1 || cmdr_kind_of(&argv[0]) != CMDR_LIST)
        return cmdr_error(error, "usage: dict.from-items LIST");
    *result = cmdr_dict();
    for (size_t i = 0; i < cmdr_count(&argv[0]); ++i) {
        const cmdr_value *pair = cmdr_item(&argv[0], i);
        if (cmdr_kind_of(pair) != CMDR_LIST || cmdr_count(pair) != 2) {
            cmdr_free(result);
            return cmdr_error(error, "dict.from-items: item %zu is not a KEY VALUE pair", i + 1);
        }
        if (put_pair("dict.from-items", result, cmdr_item(pair, 0), cmdr_item(pair, 1), error))
            return 1;
    }
    return 0;
}

COMMAND(get_command)
{
    if (argc != 2) return cmdr_error(error, "usage: dict.get DICT KEY");
    if (require_dict("dict.get", &argv[0], error) || require_key("dict.get", &argv[1], error))
        return 1;
    const cmdr_value *v = cmdr_dict_get(&argv[0], cmdr_text_of(&argv[1]));
    if (!v) return cmdr_error(error, "dict.get: no key %s", cmdr_text_of(&argv[1])), CMDR_MISMATCH;
    return cmdr_clone(v, result, error);
}

COMMAND(has_command)
{
    if (argc != 2) return cmdr_error(error, "usage: dict.has DICT KEY");
    if (require_dict("dict.has", &argv[0], error) || require_key("dict.has", &argv[1], error))
        return 1;
    if (!cmdr_dict_get(&argv[0], cmdr_text_of(&argv[1])))
        return cmdr_error(error, "dict.has: no key %s", cmdr_text_of(&argv[1])), CMDR_MISMATCH;
    return cmdr_clone(&argv[0], result, error);
}

COMMAND(set_command)
{
    if (argc < 3 || argc % 2 == 0) return cmdr_error(error, "usage: dict.set DICT KEY VALUE [KEY VALUE ...]");
    if (require_dict("dict.set", &argv[0], error) || cmdr_clone(&argv[0], result, error)) return 1;
    return put_pairs("dict.set", result, argc - 1, argv + 1, error);
}

COMMAND(unset_command)
{
    if (!argc) return cmdr_error(error, "usage: dict.unset DICT [KEY ...]");
    if (require_dict("dict.unset", &argv[0], error)) return 1;
    for (size_t k = 1; k < argc; ++k)
        if (require_key("dict.unset", &argv[k], error)) return 1;
    *result = cmdr_dict();
    for (size_t i = 0; i < cmdr_count(&argv[0]); ++i) {
        const char *key = cmdr_dict_key(&argv[0], i);
        size_t k = 1;
        while (k < argc && strcmp(cmdr_text_of(&argv[k]), key)) ++k;
        if (k < argc) continue;
        cmdr_value v;
        if (cmdr_clone(cmdr_dict_value(&argv[0], i), &v, error)) { cmdr_free(result); return 1; }
        cmdr_dict_put(result, key, v);
    }
    return 0;
}

COMMAND(merge_command)
{
    if (!argc) return cmdr_error(error, "usage: dict.merge DICT [DICT ...]");
    for (size_t k = 0; k < argc; ++k)
        if (require_dict("dict.merge", &argv[k], error)) return 1;
    *result = cmdr_dict();
    for (size_t k = 0; k < argc; ++k)
        for (size_t i = 0; i < cmdr_count(&argv[k]); ++i) {
            cmdr_value v;
            if (cmdr_clone(cmdr_dict_value(&argv[k], i), &v, error)) {
                cmdr_free(result);
                return 1;
            }
            cmdr_dict_put(result, cmdr_dict_key(&argv[k], i), v);
        }
    return 0;
}

enum part { KEYS, VALUES, ITEMS };
static int list_of(enum part part, const char *name, size_t argc, const cmdr_value *argv,
                   cmdr_value *result, char **error)
{
    if (argc != 1) return cmdr_error(error, "usage: %s DICT", name);
    if (require_dict(name, &argv[0], error)) return 1;
    *result = cmdr_list();
    for (size_t i = 0; i < cmdr_count(&argv[0]); ++i) {
        const char *key = cmdr_dict_key(&argv[0], i);
        cmdr_value k = cmdr_text(key, strlen(key)), v;
        if (part == KEYS) { cmdr_push(result, k); continue; }
        if (cmdr_clone(cmdr_dict_value(&argv[0], i), &v, error)) {
            cmdr_free(&k);
            cmdr_free(result);
            return 1;
        }
        if (part == VALUES) { cmdr_free(&k); cmdr_push(result, v); continue; }
        cmdr_value item = cmdr_list();
        cmdr_push(&item, k);
        cmdr_push(&item, v);
        cmdr_push(result, item);
    }
    return 0;
}

COMMAND(keys_command) { return list_of(KEYS, "dict.keys", argc, argv, result, error); }
COMMAND(values_command) { return list_of(VALUES, "dict.values", argc, argv, result, error); }
COMMAND(items_command) { return list_of(ITEMS, "dict.items", argc, argv, result, error); }

COMMAND(count_command)
{
    if (argc != 1) return cmdr_error(error, "usage: dict.count DICT");
    if (require_dict("dict.count", &argv[0], error)) return 1;
    char n[24];
    *result = cmdr_text(n, (size_t)snprintf(n, sizeof(n), "%zu", cmdr_count(&argv[0])));
    return 0;
}

#define VALUE(n, u, d, f) {.name = n, .usage = u, .docstring = d, .run = f}
static const cmdr_command commands[] = {
    VALUE("of", "of [KEY VALUE ...]", "Return a dict; a repeated KEY keeps its place and the last VALUE", of_command),
    VALUE("from-items", "from-items LIST", "Return a dict of a list of KEY VALUE pairs", from_items_command),
    VALUE("get", "get DICT KEY", "Return the value under KEY; a mismatch if there is none", get_command),
    VALUE("has", "has DICT KEY", "Return DICT if it has KEY", has_command),
    VALUE("set", "set DICT KEY VALUE [KEY VALUE ...]", "Return DICT with each KEY set to VALUE", set_command),
    VALUE("unset", "unset DICT [KEY ...]", "Return DICT without the KEYs; a missing KEY is ignored", unset_command),
    VALUE("merge", "merge DICT [DICT ...]", "Return the entries of each DICT; later ones win", merge_command),
    VALUE("keys", "keys DICT", "List the keys, in order", keys_command),
    VALUE("values", "values DICT", "List the values, in order", values_command),
    VALUE("items", "items DICT", "List KEY VALUE pairs, in order", items_command),
    VALUE("count", "count DICT", "Count entries", count_command),
};
const cmdr cmdr_ext_dict = {commands, sizeof(commands) / sizeof(commands[0]), NULL};
