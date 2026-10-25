/* UTF-8 decoding, shared by ext/utf8 and ext/json. Header-only, since each
 * extension builds from its own directory's sources. */
#ifndef CMDR_EXT_UTF8_H
#define CMDR_EXT_UTF8_H

#include <stddef.h>

/* The length of the UTF-8 sequence at p, or 0 if it is invalid: overlong,
 * a surrogate, above U+10FFFF, or cut short. */
static inline size_t utf8_length(const unsigned char *p, const unsigned char *end)
{
    size_t n;
    unsigned char lo = 0x80, hi = 0xbf;
    if (*p < 0x80) return 1;
    if (*p >= 0xc2 && *p <= 0xdf) n = 2;
    else if (*p >= 0xe0 && *p <= 0xef) {
        n = 3;
        if (*p == 0xe0) lo = 0xa0;
        if (*p == 0xed) hi = 0x9f;
    } else if (*p >= 0xf0 && *p <= 0xf4) {
        n = 4;
        if (*p == 0xf0) lo = 0x90;
        if (*p == 0xf4) hi = 0x8f;
    } else return 0;
    if ((size_t)(end - p) < n || p[1] < lo || p[1] > hi) return 0;
    for (size_t i = 2; i < n; ++i)
        if (p[i] < 0x80 || p[i] > 0xbf) return 0;
    return n;
}

/* The code point of a valid sequence of n bytes. */
static inline unsigned long utf8_decode(const unsigned char *p, size_t n)
{
    if (n == 1) return p[0];
    unsigned long c = p[0] & (0x7f >> n);
    for (size_t i = 1; i < n; ++i) c = c << 6 | (p[i] & 0x3f);
    return c;
}

#endif
