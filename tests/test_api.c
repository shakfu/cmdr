#define _POSIX_C_SOURCE 200809L
#include "prelude.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(cond) do { \
    if (!(cond)) { \
        fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #cond); \
        abort(); \
    } \
} while (0)

static int custom(void *user, size_t argc, const char *const *argv,
                   char **result, char **error)
{
    if (argc != 1) return cmdr_error(error, "custom expects one argument");
    ++*(int *)user;
    size_t n = strlen(argv[0]);
    *result = malloc(n + 2);
    if (!*result) return cmdr_error(error, "out of memory");
    (*result)[0] = '!';
    memcpy(*result + 1, argv[0], n + 1);
    return 0;
}

static void evaluate(cmdr *repl, const char *source, cmdr_value *value)
{
    char *error = NULL;
    CHECK(cmdr_eval_value(repl, source, NULL, value, &error) == 0);
    CHECK(error == NULL);
}

int main(void)
{
    int calls = 0;
    const cmdr_command commands[] = {
        {.name = "upper", .run = custom, .user = &calls},
        {.name = "shout", .pipeline = "upper"},
        {.name = "clean", .pipeline = "trim | lower"},
        {.name = "safe-upper", .pipeline = "try upper | recover fallback",
         .handles_failure = 1},
    };
    cmdr repl = {
        .commands = commands,
        .count = sizeof(commands) / sizeof(commands[0]),
        .parent = cmdr_prelude(),
    };
    cmdr_value value = {0};
    evaluate(&repl, "list 'a\nb' '' 'a|b' | map upper", &value);
    CHECK(value.kind == CMDR_LIST && value.count == 3 && calls == 3);
    CHECK(!strcmp(value.items[0].text, "!a\nb"));
    CHECK(!strcmp(value.items[1].text, "!"));
    CHECK(!strcmp(value.items[2].text, "!a|b"));
    cmdr_value_free(&value);

    evaluate(&repl, "list 'a\nb' 'c' | map list", &value);
    CHECK(value.kind == CMDR_LIST && value.count == 2);
    CHECK(value.items[0].kind == CMDR_LIST && value.items[0].count == 1);
    CHECK(!strcmp(value.items[0].items[0].text, "a\nb"));
    cmdr_value copy = {0};
    char *error = NULL;
    CHECK(!cmdr_value_clone(&value, &copy, &error));
    CHECK(error == NULL);
    cmdr_value_free(&value);
    CHECK(!strcmp(copy.items[0].items[0].text, "a\nb"));
    cmdr_value_free(&copy);

    evaluate(&repl, "list 'a\nb' '' | head 1", &value);
    CHECK(value.kind == CMDR_LIST && value.count == 1);
    CHECK(!strcmp(value.items[0].text, "a\nb"));
    cmdr_value_free(&value);

    evaluate(&repl, "text 'b\na\n' | sort", &value);
    CHECK(value.kind == CMDR_TEXT && !strcmp(value.text, "a\nb\n"));
    cmdr_value_free(&value);

    evaluate(&repl, "text hello | try upper | recover fallback", &value);
    CHECK(value.kind == CMDR_TEXT && !strcmp(value.text, "!hello"));
    cmdr_value_free(&value);

    evaluate(&repl, "list a b | map try 'unknown' | map recover fallback", &value);
    CHECK(value.count == 2 && !strcmp(value.items[0].text, "fallback"));
    cmdr_value_free(&value);

    CHECK(cmdr_eval_value(&repl, "list a b | map try unknown", NULL, &value, &error));
    CHECK(value.kind == CMDR_NONE && error && strstr(error, "unknown"));
    free(error);
    error = NULL;

    cmdr isolated = {0};
    CHECK(cmdr_eval_value(&isolated, "list a", NULL, &value, &error));
    CHECK(value.kind == CMDR_NONE);
    free(error);
    error = NULL;

    evaluate(&repl, "shout hello", &value);
    CHECK(value.kind == CMDR_TEXT && !strcmp(value.text, "!hello"));
    cmdr_value_free(&value);

    evaluate(&repl, "text '  Hello World  ' | clean", &value);
    CHECK(value.kind == CMDR_TEXT && !strcmp(value.text, "hello world"));
    cmdr_value_free(&value);

    evaluate(&repl, "list a b | map shout | join ,", &value);
    CHECK(value.kind == CMDR_TEXT && !strcmp(value.text, "!a,!b") && calls == 7);
    cmdr_value_free(&value);

    evaluate(&repl, "text hello | safe-upper", &value);
    CHECK(value.kind == CMDR_TEXT && !strcmp(value.text, "!hello"));
    cmdr_value_free(&value);

    CHECK(cmdr_eval_value(&repl, "shout a b", NULL, &value, &error));
    CHECK(error && strstr(error, "at most one value"));
    free(error);
    error = NULL;

    /* Cyclic pipeline commands must be reported, not crash the process. */
    const cmdr_command cyclic[] = {
        {.name = "a", .pipeline = "b"},
        {.name = "b", .pipeline = "a"},
    };
    cmdr cycle = {.commands = cyclic, .count = 2, .parent = cmdr_prelude()};
    CHECK(cmdr_eval_value(&cycle, "text x | a", NULL, &value, &error));
    CHECK(error && strstr(error, "possible cycle"));
    CHECK(value.kind == CMDR_NONE);
    free(error);
    error = NULL;

    /* Deep nestings built through the typed API must free without overflowing
     * the stack, and must fail cleanly under clone/render. */
    cmdr_value deep = {.kind = CMDR_LIST};
    cmdr_value *leaf = &deep;
    for (int i = 0; i < 5000; ++i) {
        cmdr_value child = {0};
        if (i == 4999) {
            child.kind = CMDR_TEXT;
            child.text = strdup("x");
            CHECK(child.text != NULL);
        } else {
            child.kind = CMDR_LIST;
        }
        CHECK(!cmdr_value_push(leaf, &child, &error));
        leaf = &leaf->items[leaf->count - 1];
    }
    char *rendered = NULL;
    CHECK(cmdr_value_render(&deep, &rendered, &error));
    CHECK(error && strstr(error, "nested"));
    CHECK(rendered == NULL);
    free(error);
    error = NULL;
    cmdr_value clone = {0};
    CHECK(cmdr_value_clone(&deep, &clone, &error));
    CHECK(error && strstr(error, "nested"));
    free(error);
    error = NULL;
    cmdr_value_free(&deep);

    /* Errors always carry a message, even at the limits of formatting. */
    CHECK(cmdr_eval_value(&isolated, "list a", NULL, &value, &error));
    CHECK(value.kind == CMDR_NONE);
    CHECK(error != NULL);
    free(error);
    error = NULL;

    return 0;
}
