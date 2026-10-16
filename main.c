#define _POSIX_C_SOURCE 200809L
#include "commands.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int evaluate(cmdr *repl, const char *line)
{
    char *result, *error;
    int status = cmdr_eval(repl, line, &result, &error);
    if (status) fprintf(stderr, "cmdr: %s\n", error ? error : "command failed");
    else if (result) {
        fputs(result, stdout);
        size_t n = strlen(result);
        if (!n || result[n - 1] != '\n') putchar('\n');
        if (ferror(stdout)) { fprintf(stderr, "cmdr: output failed\n"); status = 1; }
    }
    free(result);
    free(error);
    return status;
}

int main(int argc, char **argv)
{
    cmdr repl;
    cmdr_commands_init(&repl);
    if (argc == 3 && !strcmp(argv[1], "-c")) return evaluate(&repl, argv[2]) ? 1 : 0;
    if (argc != 1) { fprintf(stderr, "usage: cmdr [-c PIPELINE]\n"); return 2; }
    int interactive = isatty(STDIN_FILENO);
    char *line = NULL;
    size_t capacity = 0;
    int status = 0;
    for (;;) {
        if (interactive) { fputs("cmdr> ", stdout); fflush(stdout); }
        if (getline(&line, &capacity, stdin) < 0) break;
        if (evaluate(&repl, line)) status = 1;
    }
    if (ferror(stdin)) { perror("cmdr: stdin"); status = 1; }
    free(line);
    return status;
}
