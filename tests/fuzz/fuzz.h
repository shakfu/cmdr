/* Shared by the fuzz targets, which define _POSIX_C_SOURCE 200809L and
 * include cmdr.h first. Each defines LLVMFuzzerTestOneInput; replay.c or
 * libFuzzer supplies main. */
#ifndef CMDR_FUZZ_H
#define CMDR_FUZZ_H

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* A failed property is a finding: abort, so libFuzzer saves the input. */
#define REQUIRE(cond) do { \
    if (!(cond)) { \
        fprintf(stderr, "%s:%d: property failed: %s\n", __FILE__, __LINE__, #cond); \
        abort(); \
    } \
} while (0)

/* The input up to its first NUL, as a C caller would pass it. */
static inline char *fuzz_text(const uint8_t *data, size_t size)
{
    const uint8_t *nul = memchr(data, 0, size);
    return cmdr_strndup((const char *)data, nul ? (size_t)(nul - data) : size);
}

/* value as cmdr_render writes it, malloc-owned. */
static inline char *fuzz_render(const cmdr_value *value)
{
    char *text = NULL;
    size_t size = 0;
    FILE *out = open_memstream(&text, &size);
    REQUIRE(out);
    cmdr_render(value, out);
    REQUIRE(!fclose(out));
    return text;
}

/* Call table's command name with text arguments; free *error unless asked for. */
static inline int fuzz_call(const cmdr *table, const char *name, size_t argc, const char *const *args,
                            cmdr_value *result)
{
    cmdr_value argv[4];
    REQUIRE(argc <= 4);
    for (size_t i = 0; i < argc; ++i) argv[i] = cmdr_text(args[i], strlen(args[i]));
    char *error = NULL;
    int status = cmdr_call(table, name, argc, argv, result, &error);
    for (size_t i = 0; i < argc; ++i) cmdr_free(&argv[i]);
    REQUIRE(!status == !error);
    REQUIRE(!status || cmdr_kind_of(result) == CMDR_NONE);
    free(error);
    return status;
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);

#endif
