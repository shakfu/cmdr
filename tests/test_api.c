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
        {.name = "wrap", .pipeline = "text $2 $1 $2"},
        {.name = "tagged", .pipeline = "list $2 $1"},
        {.name = "swap", .pipeline = "replace $2 $3"},
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

    /* Parameters: $1 is the first argument, piped or explicit. */
    evaluate(&repl, "wrap x '*'", &value);
    CHECK(value.kind == CMDR_TEXT && !strcmp(value.text, "* x *"));
    cmdr_value_free(&value);
    evaluate(&repl, "text 'a | upper' | wrap '*'", &value);
    CHECK(value.kind == CMDR_TEXT && !strcmp(value.text, "* a | upper *"));
    cmdr_value_free(&value);
    evaluate(&repl, "list a b | map wrap - | join ,", &value);
    CHECK(value.kind == CMDR_TEXT && !strcmp(value.text, "- a -,- b -"));
    cmdr_value_free(&value);
    /* The first stage gets $1 implicitly when it does not reference $1. */
    evaluate(&repl, "text abc | swap b X", &value);
    CHECK(value.kind == CMDR_TEXT && !strcmp(value.text, "aXc"));
    cmdr_value_free(&value);
    /* A list reference stays one typed argument. */
    evaluate(&repl, "list a b | tagged t", &value);
    CHECK(value.kind == CMDR_LIST && value.count == 2);
    CHECK(!strcmp(value.items[0].text, "t"));
    CHECK(value.items[1].kind == CMDR_LIST && value.items[1].count == 2);
    cmdr_value_free(&value);
    CHECK(cmdr_eval_value(&repl, "wrap x", NULL, &value, &error));
    CHECK(error && strstr(error, "expects 2 arguments, got 1"));
    free(error);
    error = NULL;
    CHECK(cmdr_eval_value(&repl, "wrap x y z", NULL, &value, &error));
    CHECK(error && strstr(error, "expects 2 arguments, got 3"));
    free(error);
    error = NULL;

    /* Typed arguments bind to $1..$N without parsing their contents. */
    cmdr_value args[2] = {{0}, {0}};
    CHECK(!cmdr_value_text(&args[0], "a | b", &error));
    CHECK(!cmdr_value_text(&args[1], "*", &error));
    CHECK(!cmdr_eval_args(&repl, "text $2 $1", 2, args, &value, &error));
    CHECK(value.kind == CMDR_TEXT && !strcmp(value.text, "* a | b"));
    cmdr_value_free(&value);
    CHECK(cmdr_eval_args(&repl, "text $3", 2, args, &value, &error));
    CHECK(error && strstr(error, "$3"));
    free(error);
    error = NULL;
    cmdr_value_free(&args[0]);
    cmdr_value_free(&args[1]);
    CHECK(!cmdr_check(&repl, "equals $1 | upper", 1, &error) && !error);
    /* Mismatch is distinct from error, through pipelines and composition. */
    CHECK(cmdr_eval_value(&repl, "text a | equals b", NULL, &value, &error) == CMDR_MISMATCH);
    free(error);
    error = NULL;
    CHECK(cmdr_eval_value(&repl, "text a | equals", NULL, &value, &error) == 1);
    free(error);
    error = NULL;
    CHECK(cmdr_check(&repl, "equals $1 | bogus", 1, &error));
    CHECK(error && strstr(error, "unknown command: bogus"));
    free(error);
    error = NULL;
    size_t highest = 99;
    CHECK(!cmdr_arity(&repl, "$2 | text $3", &highest, &error) && highest == 3);
    CHECK(!cmdr_arity(&repl, "upper", &highest, &error) && highest == 0);
    CHECK(cmdr_arity(&repl, "bogus", &highest, &error));
    free(error);
    error = NULL;
    CHECK(cmdr_check(&repl, "text $2", 1, &error));
    free(error);
    error = NULL;

    /* A scoped body resolves names in its scope, not the active registry. */
    const cmdr_command scoped[] = {
        {.name = "upper", .pipeline = "text overridden"},
        {.name = "loud", .pipeline = "upper", .scope = NULL},
    };
    cmdr_command lexical = {.name = "loud2", .pipeline = "upper"};
    lexical.scope = cmdr_prelude();
    cmdr inner = {.commands = &lexical, .count = 1, .parent = cmdr_prelude()};
    cmdr outer = {.commands = scoped, .count = 2, .parent = &inner};
    evaluate(&outer, "loud a", &value);
    CHECK(value.kind == CMDR_TEXT && !strcmp(value.text, "a overridden"));
    cmdr_value_free(&value);
    evaluate(&outer, "loud2 a", &value);
    CHECK(value.kind == CMDR_TEXT && !strcmp(value.text, "A"));
    cmdr_value_free(&value);

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
