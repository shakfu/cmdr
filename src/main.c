/* Host for cmdr.janet: runs the Janet VM and, with linenoise, the line editor. */
#include <janet.h>
#include <stdlib.h>
#include <string.h>

#include "cmdr_janet.h" /* src/cmdr.janet as a byte array; see cmake/embed.cmake. */

#ifdef CMDR_LINENOISE
#include "editor.h"

static editor *ed;

static void close_editor(void)
{
    editor_close(ed);
}

/* (editor/read prompt names): a line with its newline, nil, or :interrupt. */
static Janet editor_read_cfun(int32_t argc, Janet *argv)
{
    janet_fixarity(argc, 2);
    const char *prompt = janet_getcstring(argv, 0);
    JanetView names = janet_getindexed(argv, 1);
    if (!ed) {
        if (!(ed = editor_open())) janet_panic("cannot open the line editor");
        atexit(close_editor);
    }
    const char **list = janet_smalloc(((size_t)names.len + 1) * sizeof(*list));
    for (int32_t i = 0; i < names.len; ++i) list[i] = janet_getcstring(names.items, i);
    int interrupted = 0;
    char *text = editor_read(ed, prompt, list, (size_t)names.len, &interrupted);
    janet_sfree(list);
    if (!text) return interrupted ? janet_ckeywordv("interrupt") : janet_wrap_nil();
    size_t n = strlen(text);
    uint8_t *line = janet_string_begin((int32_t)n + 1);
    memcpy(line, text, n);
    line[n] = '\n';
    free(text);
    return janet_wrap_string(janet_string_end(line));
}

static Janet editor_remember_cfun(int32_t argc, Janet *argv)
{
    janet_fixarity(argc, 1);
    if (ed) editor_remember(ed, janet_getcstring(argv, 0));
    return janet_wrap_nil();
}
#endif

static JanetFunction *resolve(JanetTable *env, const char *name)
{
    Janet out = janet_wrap_nil();
    janet_resolve(env, janet_csymbol(name), &out);
    if (!janet_checktype(out, JANET_FUNCTION)) janet_panicf("cmdr.janet: missing %s", name);
    return janet_unwrap_function(out);
}

int main(int argc, char **argv)
{
    janet_init();
    JanetTable *env = janet_core_env(NULL);
    if (janet_dobytes(env, cmdr_janet, (int32_t)cmdr_janet_len, "cmdr.janet", NULL))
        return 1;
    /* Add application commands here: define C functions with janet_cfuns,
     * then pass them to `register`, as the editor hooks below are passed. */
#ifdef CMDR_LINENOISE
    const JanetReg hooks[] = {
        {"editor/read", editor_read_cfun, "(editor/read prompt names)"},
        {"editor/remember", editor_remember_cfun, "(editor/remember pipeline)"},
        {NULL, NULL, NULL},
    };
    janet_cfuns(env, NULL, hooks);
    if (janet_dostring(env, "(use-editor editor/read editor/remember)", "main.c", NULL))
        return 1;
#endif
    Janet *args = janet_tuple_begin(argc - 1);
    for (int i = 1; i < argc; ++i) args[i - 1] = janet_cstringv(argv[i]);
    /* main exits through os/exit. A scheduled fiber lets core functions
     * that wait on the event loop, such as os/execute, run. */
    JanetFiber *fiber = janet_fiber(resolve(env, "main"), 64, argc - 1,
                                    janet_tuple_end(args));
    fiber->env = env;
    int status = janet_loop_fiber(fiber);
    janet_deinit();
    return status ? 1 : 0;
}
