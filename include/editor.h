#ifndef CMDR_EDITOR_H
#define CMDR_EDITOR_H

#include <stddef.h>

/* Interactive line editing over linenoise, built only with CMDR_LINENOISE. */
typedef struct editor editor;

/* Open an editor and load ~/.cmdr_history. Returns NULL on failure. */
editor *editor_open(void);
/* Read a line without its newline, completing and highlighting the command
 * names, which are borrowed for the call. Returns a malloc-owned string, or
 * NULL at end of input; *interrupted is set on Ctrl-C. */
char *editor_read(editor *ed, const char *prompt, const char *const *names,
                  size_t count, int *interrupted);
/* Add a complete pipeline to the history, joining continued lines. */
void editor_remember(editor *ed, const char *pipeline);
void editor_close(editor *ed);

#endif
