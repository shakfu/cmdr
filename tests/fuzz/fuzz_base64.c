/* ext/base64. Properties: decoding an encoding gives the input back, and an
 * input that decodes has a length that is a multiple of 4 once whitespace
 * is removed. */
#define _POSIX_C_SOURCE 200809L
#include "cmdr.h"
#include "fuzz.h"

extern const cmdr cmdr_ext_base64;

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    char *text = fuzz_text(data, size);
    cmdr_value out, back;
    if (!fuzz_call(&cmdr_ext_base64, "decode", 1, (const char *[]){text}, &out)) {
        size_t n = 0, pad = 0;
        for (const char *p = text; *p; ++p)
            if (!strchr(" \t\r\n", *p)) {
                ++n;
                pad = *p == '=' ? pad + 1 : 0;
            }
        REQUIRE(n % 4 == 0 && pad <= 2);
        REQUIRE(strlen(cmdr_text_of(&out)) == n / 4 * 3 - pad);
        cmdr_free(&out);
    }
    REQUIRE(!fuzz_call(&cmdr_ext_base64, "encode", 1, (const char *[]){text}, &out));
    REQUIRE(!fuzz_call(&cmdr_ext_base64, "decode", 1, (const char *[]){cmdr_text_of(&out)}, &back));
    REQUIRE(!strcmp(cmdr_text_of(&back), text));
    cmdr_free(&out);
    cmdr_free(&back);
    free(text);
    return 0;
}
