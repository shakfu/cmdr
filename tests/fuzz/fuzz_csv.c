/* ext/csv. Properties: csv.format of parsed rows parses back to the same
 * rows, and csv.dicts succeeds only on text csv.rows accepts. */
#define _POSIX_C_SOURCE 200809L
#include "cmdr.h"
#include "fuzz.h"

extern const cmdr cmdr_ext_csv;

static void round_trip(const char *text, const char *delim)
{
    cmdr_value rows, again, out;
    const char *args[] = {text, delim};
    int parsed = !fuzz_call(&cmdr_ext_csv, "rows", 2, args, &rows);
    if (!fuzz_call(&cmdr_ext_csv, "dicts", 2, args, &out)) {
        REQUIRE(parsed);
        cmdr_free(&out);
    }
    if (!parsed) return;
    cmdr_value argv[] = {rows, cmdr_text(delim, 1)};
    char *error = NULL;
    REQUIRE(!cmdr_call(&cmdr_ext_csv, "format", 2, argv, &out, &error));
    args[0] = cmdr_text_of(&out);
    REQUIRE(!fuzz_call(&cmdr_ext_csv, "rows", 2, args, &again));
    char *before = fuzz_render(&rows), *after = fuzz_render(&again);
    /* Rendering joins fields; compare shapes too. */
    int same = !strcmp(before, after) && cmdr_count(&rows) == cmdr_count(&again);
    for (size_t i = 0; same && i < cmdr_count(&rows); ++i) {
        const cmdr_value *a = cmdr_item(&rows, i), *b = cmdr_item(&again, i);
        same = cmdr_count(a) == cmdr_count(b);
        for (size_t k = 0; same && k < cmdr_count(a); ++k)
            same = !strcmp(cmdr_text_of(cmdr_item(a, k)), cmdr_text_of(cmdr_item(b, k)));
    }
    if (!same) fprintf(stderr, "csv: '%s' formats as '%s'\n", text, cmdr_text_of(&out));
    REQUIRE(same);
    free(before);
    free(after);
    cmdr_free(&argv[1]);
    cmdr_free(&rows);
    cmdr_free(&again);
    cmdr_free(&out);
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    char *text = fuzz_text(data, size);
    round_trip(text, ",");
    round_trip(text, "\t");
    free(text);
    return 0;
}
