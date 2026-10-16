#ifndef CMDR_PRELUDE_H
#define CMDR_PRELUDE_H

#include "cmdr.h"

/* Shared, immutable standard command registry. No initialization or cleanup
 * is needed. Embed with: cmdr repl = { .parent = cmdr_prelude() }; */
const cmdr *cmdr_prelude(void);

#endif
