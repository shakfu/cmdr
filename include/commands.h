#ifndef CMDR_COMMANDS_H
#define CMDR_COMMANDS_H

#include "cmdr.h"

/* Initialize an interpreter with commands.c and the automatic prelude fallback.
 * The command table has static storage duration. */
void cmdr_commands_init(cmdr *repl);

#endif
