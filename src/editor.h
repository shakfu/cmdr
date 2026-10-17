#ifndef CMDR_EDITOR_H
#define CMDR_EDITOR_H

#include "cmdr.h"

#include <sys/types.h>

/* Interactive line editing over linenoise, built only with CMDR_EDITOR. */
typedef struct editor editor;

/* Open an editor completing and highlighting names from repl, which must
 * outlive it. Loads ~/.cmdr_history. Returns NULL on failure. */
editor *editor_open(const cmdr *repl);
/* The prompt: "cmdr> " in light green where the terminal supports color, or
 * "...> " for a continued pipeline. */
const char *editor_prompt(const editor *ed, int continued);
/* Whether the terminal shows color, as the prompt does. */
int editor_color(const editor *ed);
/* Read a line into *line with a trailing newline, as getline does. Returns
 * -1 at end of input and -2 on Ctrl-C. */
ssize_t editor_read(editor *ed, const char *prompt, char **line, size_t *capacity);
/* Add a complete pipeline to the history, joining continued lines. */
void editor_remember(editor *ed, const char *pipeline);
void editor_close(editor *ed);

#endif
