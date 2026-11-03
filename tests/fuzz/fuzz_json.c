/* The ext/json scanner, through the commands that parse their argument.
 * Properties: json.format of valid JSON is valid and a fixed point, and
 * json.valid agrees with the other commands on what parses. */
#define _POSIX_C_SOURCE 200809L
#include "cmdr.h"
#include "fuzz.h"

extern const cmdr cmdr_ext_json;

static char *call_text(const char *name, size_t argc, const char *const *args)
{
    cmdr_value out;
    if (fuzz_call(&cmdr_ext_json, name, argc, args, &out)) return NULL;
    char *text = fuzz_render(&out);
    cmdr_free(&out);
    return text;
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    char *text = fuzz_text(data, size);
    cmdr_value out;
    int valid = !fuzz_call(&cmdr_ext_json, "valid", 1, (const char *[]){text}, &out);
    if (valid) cmdr_free(&out);
    static const char *const readers[] = {"type", "get", "keys", "values", "to-dict", "text", "has"};
    for (size_t i = 0; i < sizeof(readers) / sizeof(*readers); ++i) {
        int ok = !fuzz_call(&cmdr_ext_json, readers[i], 1, (const char *[]){text}, &out);
        if (ok) cmdr_free(&out);
        if (!strcmp(readers[i], "type") || !strcmp(readers[i], "get")) REQUIRE(ok == valid);
        else if (ok) REQUIRE(valid);
    }
    /* A path from the input's first key, and a set at it. */
    if (valid && !fuzz_call(&cmdr_ext_json, "keys", 1, (const char *[]){text}, &out)) {
        if (cmdr_count(&out)) {
            const char *key = cmdr_text_of(cmdr_item(&out, 0));
            char *got = call_text("get", 2, (const char *[]){text, key});
            REQUIRE(got);
            char *set = call_text("set", 3, (const char *[]){text, key, "[1,{\"a\":null}]"});
            REQUIRE(set);
            char *back = call_text("get", 2, (const char *[]){set, key});
            REQUIRE(back && !strcmp(back, "[1,{\"a\":null}]"));
            free(got);
            free(set);
            free(back);
        }
        cmdr_free(&out);
    }
    for (int indent = 0; indent < 2; ++indent) {
        const char *args[] = {text, "2"};
        char *once = call_text("format", 1 + (size_t)indent, args);
        REQUIRE(!once == !valid);
        if (!once) continue;
        args[0] = once;
        char *twice = call_text("format", 1 + (size_t)indent, args);
        if (!twice || strcmp(once, twice)) fprintf(stderr, "json.format: '%s' then '%s'\n", once, twice);
        REQUIRE(twice && !strcmp(once, twice));
        free(once);
        free(twice);
    }
    free(text);
    return 0;
}
