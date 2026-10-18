/* A sandboxed host: no file or process commands, no load. */
#include <cmdr.h>

#include <stdlib.h>
#include <string.h>

int main(void)
{
    cmdr_session *session = cmdr_session_open(&cmdr_pure, 0, NULL);
    cmdr_value value;
    char *error;
    int failed = cmdr_session_prelude(session, &error)
        || cmdr_session_eval(session, "list b a c | distinct | unwords", &value, &error);
    if (failed) {
        fprintf(stderr, "consumer: %s\n", error);
        return 1;
    }
    failed = strcmp(value.text, "a b c") != 0;
    cmdr_free(&value);
    if (!cmdr_session_eval(session, "run true", &value, &error)) failed = 1;
    free(error);
    cmdr_session_close(session);
    return failed;
}
