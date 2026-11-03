/* Base64, as Python's base64.b64encode and b64decode, with the standard
 * alphabet and padding. */
#define _POSIX_C_SOURCE 200809L
#include "cmdr.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define COMMAND(name) \
    static int name(const cmdr *repl, void *user, size_t argc, const cmdr_value *argv, \
                    cmdr_value *result, char **error)

static const char alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

/* n bytes as base64 text, without line breaks. */
static cmdr_value encode(const unsigned char *data, size_t n)
{
    char *out = cmdr_realloc(NULL, n / 3 * 4 + 5, 1), *p = out;
    for (size_t i = 0; i < n; i += 3) {
        unsigned long v = (unsigned long)data[i] << 16;
        if (i + 1 < n) v |= (unsigned long)data[i + 1] << 8;
        if (i + 2 < n) v |= data[i + 2];
        *p++ = alphabet[v >> 18 & 63];
        *p++ = alphabet[v >> 12 & 63];
        *p++ = i + 1 < n ? alphabet[v >> 6 & 63] : '=';
        *p++ = i + 2 < n ? alphabet[v & 63] : '=';
    }
    *p = '\0';
    return cmdr_text_owned(out);
}

COMMAND(encode_command)
{
    if (argc != 1) return cmdr_error(error, "usage: base64.encode TEXT");
    const char *text = cmdr_text_of(&argv[0]);
    *result = encode((const unsigned char *)text, strlen(text));
    return 0;
}

COMMAND(encode_file_command)
{
    if (argc != 1) return cmdr_error(error, "usage: base64.encode-file PATH");
    const char *path = cmdr_text_of(&argv[0]);
    FILE *file = fopen(path, "rb");
    if (!file) return cmdr_error(error, "base64.encode-file %s: %s", path, strerror(errno));
    unsigned char *data = NULL;
    size_t used = 0, size = 0, n;
    do {
        if (used == size) data = cmdr_realloc(data, size = size ? size * 2 : 4096, 1);
        n = fread(data + used, 1, size - used, file);
        used += n;
    } while (n);
    int failed = ferror(file);
    fclose(file);
    if (failed) {
        free(data);
        return cmdr_error(error, "base64.encode-file %s: read failed", path);
    }
    *result = encode(data, used);
    free(data);
    return 0;
}

static int digit(int c)
{
    const char *at = c ? strchr(alphabet, c) : NULL;
    return at ? (int)(at - alphabet) : -1;
}

/* Spaces, tabs, and line breaks are skipped, so wrapped output decodes.
 * Anything else outside the alphabet, or wrong padding, is an error. */
COMMAND(decode_command)
{
    if (argc != 1) return cmdr_error(error, "usage: base64.decode TEXT");
    const char *text = cmdr_text_of(&argv[0]);
    size_t n = 0;
    char *in = cmdr_realloc(NULL, strlen(text) + 1, 1);
    for (const char *p = text; *p; ++p) {
        if (strchr(" \t\r\n", *p)) continue;
        if (*p != '=' && digit((unsigned char)*p) < 0) {
            free(in);
            return cmdr_error(error, "base64.decode: invalid character at byte %zu", (size_t)(p - text) + 1);
        }
        in[n++] = *p;
    }
    /* '=' may only end the last group of 4, once or twice. */
    size_t pad = n && in[n - 1] == '=' ? (n > 1 && in[n - 2] == '=' ? 2 : 1) : 0;
    int bad = n % 4 != 0 || (n && memchr(in, '=', n - pad));
    char *out = bad ? NULL : cmdr_realloc(NULL, n / 4 * 3 + 1, 1);
    size_t used = 0;
    for (size_t i = 0; !bad && i < n; i += 4) {
        unsigned long v = 0;
        for (size_t k = 0; k < 4; ++k) v = v << 6 | (unsigned long)(in[i + k] == '=' ? 0 : digit(in[i + k]));
        size_t count = i + 4 == n ? 3 - pad : 3;
        for (size_t k = 0; k < count; ++k) {
            char byte = (char)(v >> (16 - 8 * k) & 255);
            if (!byte) {
                free(in);
                free(out);
                return cmdr_error(error, "base64.decode: the data holds a NUL byte, which text cannot");
            }
            out[used++] = byte;
        }
    }
    free(in);
    if (bad) return cmdr_error(error, "base64.decode: incorrect padding");
    out[used] = '\0';
    *result = cmdr_text_owned(out);
    return 0;
}

static const cmdr_command commands[] = {
    {.name = "encode", .usage = "encode TEXT", .docstring = "Encode TEXT as base64",
     .run = encode_command, .text = 1},
    {.name = "encode-file", .usage = "encode-file PATH", .docstring = "Encode a file's bytes as base64",
     .run = encode_file_command, .text = 1},
    {.name = "decode", .usage = "decode TEXT", .docstring = "Decode base64 TEXT",
     .run = decode_command, .text = 1},
};
const cmdr cmdr_ext_base64 = {commands, sizeof(commands) / sizeof(commands[0]), NULL};
