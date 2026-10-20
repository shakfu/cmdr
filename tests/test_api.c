/* Checks for the embedding API: host commands, sessions, scripts, threads. */
#define _POSIX_C_SOURCE 200809L
#include "cmdr.h"

#include <errno.h>
#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define CHECK(cond) do { \
    if (!(cond)) { \
        fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #cond); \
        abort(); \
    } \
} while (0)

/* Count calls through user and mark the text. */
static int bang(const cmdr *repl, void *user, size_t argc, const cmdr_value *argv,
                cmdr_value *result, char **error)
{
    if (argc != 1) return cmdr_error(error, "usage: bang TEXT");
    ++*(int *)user;
    size_t n = strlen(argv[0].text);
    *result = cmdr_text("!", 1);
    result->text = cmdr_realloc(result->text, n + 2, 1);
    memcpy(result->text + 1, argv[0].text, n + 1);
    return 0;
}

/* Evaluate source, which must succeed, and return its value as printed. */
static char *eval(cmdr_session *session, const char *source)
{
    cmdr_value value;
    char *error, *text = NULL;
    size_t size = 0;
    int status = cmdr_session_eval(session, source, &value, &error);
    if (status) fprintf(stderr, "%s: %s\n", source, error);
    CHECK(status == 0 && error == NULL);
    FILE *out = open_memstream(&text, &size);
    CHECK(out && !cmdr_print(&value, out));
    fclose(out);
    cmdr_free(&value);
    return text;
}

static void expect(cmdr_session *session, const char *source, const char *printed)
{
    char *text = eval(session, source);
    if (strcmp(text, printed)) fprintf(stderr, "%s: got '%s'\n", source, text);
    CHECK(!strcmp(text, printed));
    free(text);
}

static void expect_error(cmdr_session *session, const char *source, int status,
                         const char *message)
{
    cmdr_value value;
    char *error;
    CHECK(cmdr_session_eval(session, source, &value, &error) == status);
    CHECK(value.kind == CMDR_NONE && error && strstr(error, message));
    free(error);
}

static void host_commands(void)
{
    int calls = 0;
    const cmdr_command commands[] = {
        {.name = "bang", .usage = "bang TEXT", .docstring = "Mark text", .run = bang,
         .user = &calls, .text = 1},
        {.name = "upper", .run = bang, .user = &calls, .text = 1}, /* Shadows. */
        {.name = "greeting", .value = &(cmdr_value){.kind = CMDR_TEXT, .text = "hi"}},
    };
    const cmdr table = {commands, 3, &cmdr_primitives};
    cmdr_session *session = cmdr_session_open(&table, 1, NULL);
    expect(session, "list 'a b' '' 'a|b' | map bang", "!a b\n!\n!a|b\n");
    CHECK(calls == 3);
    expect(session, "upper x", "!x\n");
    expect(session, "lower X", "x\n");
    /* A host's constant is a command and a $NAME, as an as binding is. */
    expect(session, "greeting", "hi\n");
    expect(session, "text $greeting there", "hi there\n");
    expect_error(session, "greeting x", 1, "greeting takes no arguments");
    expect(session, "help | filter 'Mark text' | count", "1\n");
    expect_error(session, "bang", 1, "usage: bang TEXT");
    expect_error(session, "list a | bang", 1, "command 'bang' expects text");
    /* The whole line compiles first, so nothing runs. */
    expect_error(session, "bang a | bogus", 1, "unknown command: bogus");
    CHECK(calls == 4);
    /* A host table can omit the primitives. */
    cmdr_session *bare = cmdr_session_open(&(cmdr){commands, 1, NULL}, 0, NULL);
    expect_error(bare, "text a", 1, "unknown command: text");
    expect(bare, "bang a", "!a\n");
    cmdr_session_close(bare);
    cmdr_session_close(session);
}

static void definitions(void)
{
    cmdr_session *a = cmdr_session_open(&cmdr_primitives, 1, NULL);
    cmdr_session *b = cmdr_session_open(&cmdr_primitives, 1, NULL);
    char *error;
    CHECK(!cmdr_session_prelude(a, &error) && !error);
    expect(a, "list x y | last", "y\n");
    expect_error(b, "list x y | last", 1, "unknown command: last");
    /* Definitions are lexical: g keeps the f it was defined with. */
    expect(a, "def f { upper }", "");
    expect(a, "def g { f }", "");
    expect(a, "def f { lower }", "");
    expect(a, "g aB", "AB\n");
    expect(a, "f aB", "ab\n");
    expect_error(b, "f x", 1, "unknown command: f");
    /* The live view sees later definitions; the evaluated scope did not. */
    CHECK(cmdr_find(cmdr_session_commands(a), "g"));
    CHECK(!cmdr_find(cmdr_session_commands(b), "g"));
    cmdr_value value, arg = cmdr_text("q", 1);
    CHECK(!cmdr_call(cmdr_session_commands(a), "g", 1, &arg, &value, &error));
    CHECK(!strcmp(value.text, "Q"));
    cmdr_free(&value);
    cmdr_free(&arg);
    expect_error(a, "text a | equals b", CMDR_MISMATCH, "equals: a differs from b");
    /* An as binding is a command with a value, which $NAME refers to. */
    expect(a, "list p q | as pair", "");
    expect(a, "pair | count", "2\n");
    expect(a, "list $pair x | count", "2\n");
    CHECK(cmdr_find(cmdr_session_commands(a), "pair")->value);
    CHECK(!cmdr_find(cmdr_session_commands(a), "g")->value);
    cmdr_session_close(a);
    cmdr_session_close(b);
}

static void scripts(void)
{
    char *text = NULL;
    size_t size = 0;
    FILE *out = open_memstream(&text, &size);
    CHECK(out);
    cmdr_session *session = cmdr_session_open(&cmdr_primitives, 1, out);
    char *error;
    const char good[] = "# defs\ndef shout { upper |\n  # note\n  text ! }\ntext a | shout\n";
    FILE *in = fmemopen((void *)good, sizeof(good) - 1, "r");
    CHECK(!cmdr_session_load(session, in, "good.cmdr", &error) && !error);
    fclose(in);
    const char bad[] = "text b\n\nbogus\ntext unreachable\n";
    in = fmemopen((void *)bad, sizeof(bad) - 1, "r");
    CHECK(cmdr_session_load(session, in, "bad.cmdr", &error) == 1);
    fclose(in);
    CHECK(!strcmp(error, "bad.cmdr:3: unknown command: bogus"));
    free(error);
    const char open[] = "text c |";
    in = fmemopen((void *)open, sizeof(open) - 1, "r");
    CHECK(cmdr_session_load(session, in, "open.cmdr", &error) == 1);
    fclose(in);
    CHECK(strstr(error, "open.cmdr:1: expected a command after '|'"));
    free(error);
    expect(session, "text z | shout", "Z !\n");
    fclose(out);
    CHECK(!strcmp(text, "A !\nb\n"));
    free(text);
    cmdr_session_close(session);
}

/* A script file loaded with arguments sees them through script-args. */
static void script_args(void)
{
    char path[] = "/tmp/cmdr-test-XXXXXX";
    int fd = mkstemp(path);
    CHECK(fd >= 0);
    const char source[] = "def given { () script-args | join , }\n";
    CHECK(write(fd, source, sizeof(source) - 1) == (ssize_t)(sizeof(source) - 1));
    FILE *in = fdopen(fd, "r");
    CHECK(in && !fseek(in, 0, SEEK_SET));
    cmdr_session *session = cmdr_session_open(&cmdr_primitives, 1, NULL);
    char *error;
    const char *argv[] = {"a", "b c"};
    CHECK(!cmdr_session_load_args(session, in, path, 2, argv, &error) && !error);
    fclose(in);
    unlink(path);
    expect(session, "given", "a,b c\n");
    cmdr_session_close(session);
}

static void sandbox(void)
{
    cmdr_session *session = cmdr_session_open(&cmdr_pure, 0, NULL);
    char *error;
    CHECK(!cmdr_session_prelude(session, &error) && !error);
    expect(session, "path-basename a/b.c | path-stem", "b\n");
    const char *denied[] = {"read x", "run true", "files", "load x", "sh true", "cat x"};
    for (size_t i = 0; i < sizeof(denied) / sizeof(denied[0]); ++i)
        expect_error(session, denied[i], 1, "unknown command");
    expect(session, "help | filter load | count", "0\n");
    cmdr_session_close(session);
    /* The effects prelude needs the effect primitives. */
    session = cmdr_session_open(&cmdr_pure, 1, NULL);
    CHECK(cmdr_session_prelude(session, &error) == 1);
    CHECK(strstr(error, "prelude-effects.cmdr:4: unknown command: read"));
    free(error);
    cmdr_session_close(session);
}

static int marker;

static void quit_with_42(void *data, size_t size)
{
    _exit(size == SIZE_MAX && data == &marker ? 42 : 3);
}

static void returns(void *data, size_t size) {}

/* Fail an allocation in a child; return its exit status. */
static int exhaust(void (*handler)(void *, size_t))
{
    pid_t pid = fork();
    CHECK(pid >= 0);
    if (!pid) {
        cmdr_set_out_of_memory(handler, &marker);
        cmdr_realloc(NULL, SIZE_MAX, 2);
        _exit(0);
    }
    int status;
    CHECK(waitpid(pid, &status, 0) == pid && WIFEXITED(status));
    return WEXITSTATUS(status);
}

static void out_of_memory(void)
{
    CHECK(exhaust(quit_with_42) == 42);
    CHECK(exhaust(returns) == 1);
    CHECK(exhaust(NULL) == 1);
    /* A zero size allocates, as a zero count does, instead of dividing by it. */
    void *p = cmdr_realloc(NULL, 4, 0);
    CHECK(p != NULL);
    free(p);
}

/* cmdr_push grows lists from {0}, from a whole allocation with capacity 0,
 * and from a clone; ASan reports a write past any of them. */
static void lists(void)
{
    enum { N = 1000 };
    cmdr_value grown = {.kind = CMDR_LIST};
    for (int i = 0; i < N; ++i) cmdr_push(&grown, cmdr_text("x", 1));
    CHECK(grown.count == N && grown.capacity >= N);
    cmdr_value whole = {.kind = CMDR_LIST, .count = 3};
    whole.items = cmdr_realloc(NULL, 3, sizeof(*whole.items));
    for (int i = 0; i < 3; ++i) whole.items[i] = cmdr_text("w", 1);
    cmdr_push(&whole, cmdr_text("v", 1));
    CHECK(whole.count == 4 && !strcmp(whole.items[3].text, "v"));
    cmdr_value copy;
    char *error = NULL;
    CHECK(!cmdr_clone(&grown, &copy, &error));
    for (int i = 0; i < N; ++i) cmdr_push(&copy, cmdr_text("y", 1));
    CHECK(copy.count == 2 * N && !strcmp(copy.items[2 * N - 1].text, "y"));
    cmdr_free(&grown);
    cmdr_free(&whole);
    cmdr_free(&copy);
}

/* Each thread nests a chain of defs and meets the other at the bottom, so
 * both are deep at once. A shared depth would exceed the limit. */
enum { CHAIN = 100 };

typedef struct {
    int status;
} party;

/* A mutex and condition, not semaphores: macOS lacks sem_timedwait. */
static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t arrival = PTHREAD_COND_INITIALIZER;
static int arrived;

static int meet(const cmdr *repl, void *user, size_t argc, const cmdr_value *argv,
                cmdr_value *result, char **error)
{
    struct timespec deadline;
    clock_gettime(CLOCK_REALTIME, &deadline);
    deadline.tv_sec += 10;
    pthread_mutex_lock(&lock);
    ++arrived;
    pthread_cond_broadcast(&arrival);
    while (arrived < 2 && pthread_cond_timedwait(&arrival, &lock, &deadline) != ETIMEDOUT) {}
    int met = arrived >= 2;
    pthread_mutex_unlock(&lock);
    if (!met) return cmdr_error(error, "meet: the other thread did not arrive");
    *result = cmdr_text("met", 3);
    return 0;
}

static void *deep(void *arg)
{
    party *p = arg;
    const cmdr_command commands[] = {{.name = "meet", .run = meet, .user = p}};
    const cmdr table = {commands, 1, &cmdr_primitives};
    cmdr_session *session = cmdr_session_open(&table, 1, NULL);
    char line[64];
    expect(session, "def d0 { () meet }", "");
    for (int i = 1; i < CHAIN; ++i) {
        snprintf(line, sizeof(line), "def d%d { () d%d }", i, i - 1);
        expect(session, line, "");
    }
    snprintf(line, sizeof(line), "d%d", CHAIN - 1);
    cmdr_value value;
    char *error;
    p->status = cmdr_session_eval(session, line, &value, &error);
    if (p->status) fprintf(stderr, "%s\n", error);
    cmdr_free(&value);
    free(error);
    cmdr_session_close(session);
    return NULL;
}

static void threads(void)
{
    party a = {-1}, b = {-1};
    pthread_t ta, tb;
    CHECK(!pthread_create(&ta, NULL, deep, &a) && !pthread_create(&tb, NULL, deep, &b));
    CHECK(!pthread_join(ta, NULL) && !pthread_join(tb, NULL));
    CHECK(a.status == 0 && b.status == 0);
}

/* Set the interrupt flag, as a SIGINT handler would while a line runs. */
static int stop(const cmdr *repl, void *user, size_t argc, const cmdr_value *argv,
                cmdr_value *result, char **error)
{
    cmdr_interrupt(1);
    *result = cmdr_text("", 0);
    return 0;
}

/* The flag fails every later call, try does not catch it, and it stays set
 * until the host clears it. */
static void interrupts(void)
{
    const cmdr_command commands[] = {{.name = "stop", .run = stop}};
    const cmdr table = {commands, 1, &cmdr_pure};
    cmdr_session *session = cmdr_session_open(&table, 0, NULL);
    expect_error(session, "stop | upper", 1, "interrupted");
    CHECK(cmdr_interrupted());
    expect_error(session, "text a", 1, "interrupted");
    cmdr_interrupt(0);
    expect_error(session, "try { () stop | upper } caught", 1, "interrupted");
    cmdr_interrupt(0);
    expect(session, "try { () text a | equals b } caught", "caught\n");
    cmdr_session_close(session);
}

/* Return the block argument, so the test can call it through the API. */
static int keep_block(const cmdr *repl, void *user, size_t argc, const cmdr_value *argv,
                      cmdr_value *result, char **error)
{
    if (argc != 1 || argv[0].kind != CMDR_BLOCK) return cmdr_error(error, "usage: keep-block BLOCK");
    return cmdr_clone(&argv[0], result, error);
}

/* A block with a default accepts 1 or 2 arguments; cmdr_run fills the second. */
static void block_defaults(void)
{
    const cmdr_command commands[] = {{.name = "keep-block", .run = keep_block}};
    const cmdr table = {commands, 1, &cmdr_primitives};
    cmdr_session *session = cmdr_session_open(&table, 1, NULL);
    cmdr_value block, out;
    char *error;
    CHECK(!cmdr_session_eval(session, "keep-block { (a b=2) text $a $b }", &block, &error));
    CHECK(block.kind == CMDR_BLOCK);
    CHECK(!cmdr_block_arity(&block, 1, &error) && !cmdr_block_arity(&block, 2, &error));
    CHECK(cmdr_block_arity(&block, 0, &error) && strstr(error, "block takes 1 to 2 arguments, got 0"));
    free(error);
    CHECK(cmdr_block_arity(&block, 3, &error));
    free(error);
    cmdr_value args[] = {cmdr_text("x", 1), cmdr_text("y", 1)};
    CHECK(!cmdr_run(&block, 1, args, &out, &error) && !strcmp(out.text, "x 2"));
    cmdr_free(&out);
    CHECK(!cmdr_run(&block, 2, args, &out, &error) && !strcmp(out.text, "x y"));
    cmdr_free(&out);
    cmdr_free(&args[0]);
    cmdr_free(&args[1]);
    cmdr_free(&block);
    cmdr_session_close(session);
}

int main(void)
{
    host_commands();
    definitions();
    block_defaults();
    scripts();
    script_args();
    sandbox();
    out_of_memory();
    lists();
    threads();
    interrupts();
    puts("test_api: ok");
    return 0;
}
