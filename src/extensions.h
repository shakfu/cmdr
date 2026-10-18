#ifndef CMDR_EXTENSIONS_H
#define CMDR_EXTENSIONS_H

#include "cmdr.h"

/* Extensions under ext/, compiled into the executable by the build. Each array
 * ends with a NULL entry. See README.md, "Extensions". */
typedef struct {
    const char *origin;   /* "ext/NAME" */
    const cmdr *commands; /* cmdr_ext_NAME */
} cmdr_extension;

typedef struct {
    const char *path; /* "ext/NAME/FILE.cmdr" */
    const char *source;
} cmdr_script;

extern const cmdr_extension cmdr_extensions[];
extern const cmdr_script cmdr_extension_scripts[];

#endif
