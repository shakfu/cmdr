#ifndef CMDR_COMMANDS_H
#define CMDR_COMMANDS_H

#include "cmdr.h"

/* Initialize an interpreter with the commands defined in commands.c.
 * The command table has static storage duration. */
void cmdr_commands_init(cmdr *repl);

#endif
