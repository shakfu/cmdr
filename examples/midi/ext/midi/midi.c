#define _POSIX_C_SOURCE 200809L
#include "cmdr.h"
#include "smf.h"

#include <libremidi/libremidi-c.h>

#include <ctype.h>
#include <errno.h>
#include <math.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

/* Outputs get this mode, as open(2) would give. umask is read only by setting
 * it; 077 meanwhile makes other threads' new files stricter, not looser. */
static mode_t file_mode;
static pthread_once_t file_mode_once = PTHREAD_ONCE_INIT;
static void read_umask(void)
{
    mode_t mask = umask(077);
    umask(mask);
    file_mode = 0666 & ~mask;
}

#define COMMAND(name) \
    static int name(const cmdr *repl, void *user, size_t argc, const cmdr_value *argv, \
                    cmdr_value *result, char **error)

/* Ticks per quarter note in files written. */
#define TPQ 480
/* The largest delta time a file can hold, a 4-byte variable-length quantity. */
#define MAX_TICK 0x0FFFFFFF
/* The tempo when play or write is given none. */
#define DEFAULT_BPM "120"
/* The longest Euclidean pattern; longer is more likely a typo than a rhythm. */
#define MAX_STEPS 4096

/* The kinds of score items, in the order they are sent at equal times:
 * tempo, then the state of a channel, then notes. */
enum { TEMPO, PROGRAM, CC, BEND, NOTE, REST };
static const char *const kind_names[] = {"tempo", "program", "cc", "bend", "note", "rest"};

/* One item: KIND START, then by kind
 *   note    LENGTH KEY VELOCITY CHANNEL
 *   cc      CONTROLLER VALUE CHANNEL
 *   program PROGRAM CHANNEL
 *   bend    VALUE CHANNEL, from -8192 to 8191
 *   tempo   BPM
 *   rest    LENGTH
 * Starts and lengths are in beats; channels are 1 to 16. A tempo read from a
 * file keeps its microseconds per beat in a. */
typedef struct {
    int kind;
    double start, length, bpm;
    int a, b, channel;
} item;

/* KEY as 0 to 127, or a name: a letter, any number of # or b, and an octave,
 * with C4 as 60. Returns -1 if TEXT is neither. */
static int parse_key(const char *text)
{
    char *end;
    errno = 0;
    long n = strtol(text, &end, 10);
    if (end != text && !*end) return errno || n < 0 || n > 127 ? -1 : (int)n;
    static const int pitch[] = {9, 11, 0, 2, 4, 5, 7}; /* A to G */
    int c = toupper((unsigned char)*text);
    if (c < 'A' || c > 'G') return -1;
    long key = pitch[c - 'A'];
    const char *p = text + 1;
    for (; *p == '#' || *p == 'b'; ++p) key += *p == '#' ? 1 : -1;
    if (!*p) return -1;
    errno = 0;
    long octave = strtol(p, &end, 10);
    if (*end || errno || octave < -1 || octave > 9) return -1;
    key += 12 * (octave + 1);
    return key < 0 || key > 127 ? -1 : (int)key;
}

static int number(const char *text, double *out)
{
    char *ignored = NULL;
    int bad = cmdr_number(text, out, &ignored);
    free(ignored);
    return bad;
}

static int integer(const char *text, int lo, int hi, int *out)
{
    double x;
    if (number(text, &x) || x != floor(x) || x < lo || x > hi) return 1;
    *out = (int)x;
    return 0;
}

/* Microseconds per quarter note, as a file holds tempo; 0 if out of range. */
static long tempo_of(double bpm)
{
    double mpqn = round(60e6 / bpm);
    return bpm > 0 && mpqn >= 1 && mpqn <= 0xFFFFFF ? (long)mpqn : 0;
}

/* One item from text of n bytes; failures name the item and field. */
static int parse_item(const char *name, size_t index, const char *text, size_t n, item *out,
                      char **error)
{
    /* Each kind's fields after its name, START first. */
    static const char *const fields[][6] = {
        {"START", "BPM"}, {"START", "PROGRAM", "CHANNEL"},
        {"START", "CONTROLLER", "VALUE", "CHANNEL"}, {"START", "VALUE", "CHANNEL"},
        {"START", "LENGTH", "KEY", "VELOCITY", "CHANNEL"}, {"START", "LENGTH"}};
    char *line = cmdr_strndup(text, n), *w[7], *save = NULL;
    size_t count = 0;
    for (char *t = strtok_r(line, " \t", &save); t; t = strtok_r(NULL, " \t", &save))
        if (count < 7) w[count++] = t;
    int kind = -1;
    for (int k = 0; count && k <= REST; ++k)
        if (!strcmp(w[0], kind_names[k])) kind = k;
    if (kind < 0) {
        cmdr_error(error, "%s: item %zu: want note, rest, cc, program, bend, or tempo, not %s",
                   name, index, count ? w[0] : "nothing");
        free(line);
        return 1;
    }
    size_t want = 1;
    while (want < 6 && fields[kind][want - 1]) ++want;
    if (count != want) {
        char form[80];
        int used = snprintf(form, sizeof(form), "%s", kind_names[kind]);
        for (size_t i = 0; i + 1 < want; ++i)
            used += snprintf(form + used, sizeof(form) - (size_t)used, " %s", fields[kind][i]);
        cmdr_error(error, "%s: item %zu: want %s", name, index, form);
        free(line);
        return 1;
    }
    *out = (item){.kind = kind};
    char **f = w + 1;
    int bad = -1; /* the field that failed */
    const char *range = "a number of beats";
    if (number(f[0], &out->start) || out->start < 0)
        bad = 0;
    else
        switch (kind) {
        case NOTE:
            if (number(f[1], &out->length) || out->length < 0)
                bad = 1;
            else if ((out->a = parse_key(f[2])) < 0)
                bad = 2, range = "0 to 127 or a name such as C4";
            else if (integer(f[3], 1, 127, &out->b))
                bad = 3, range = "1 to 127";
            else if (integer(f[4], 1, 16, &out->channel))
                bad = 4, range = "1 to 16";
            break;
        case CC:
            if (integer(f[1], 0, 127, &out->a))
                bad = 1, range = "0 to 127";
            else if (integer(f[2], 0, 127, &out->b))
                bad = 2, range = "0 to 127";
            else if (integer(f[3], 1, 16, &out->channel))
                bad = 3, range = "1 to 16";
            break;
        case PROGRAM:
            if (integer(f[1], 0, 127, &out->a))
                bad = 1, range = "0 to 127";
            else if (integer(f[2], 1, 16, &out->channel))
                bad = 2, range = "1 to 16";
            break;
        case BEND:
            if (integer(f[1], -8192, 8191, &out->a))
                bad = 1, range = "-8192 to 8191";
            else if (integer(f[2], 1, 16, &out->channel))
                bad = 2, range = "1 to 16";
            break;
        case TEMPO:
            if (number(f[1], &out->bpm) || !tempo_of(out->bpm))
                bad = 1, range = "3.6 to 60000000";
            break;
        case REST:
            if (number(f[1], &out->length) || out->length < 0) bad = 1;
            break;
        }
    if (bad >= 0)
        cmdr_error(error, "%s: item %zu: %s %s is not %s", name, index, fields[kind][bad], f[bad],
                   range);
    free(line);
    return bad >= 0;
}

/* A score: a list of items, or text with one item per line. */
static int parse_score(const char *name, const cmdr_value *score, item **items, size_t *count,
                       char **error)
{
    *items = NULL;
    *count = 0;
    if (score->kind != CMDR_TEXT && score->kind != CMDR_LIST)
        return cmdr_error(error, "%s: SCORE is not a list or text", name);
    size_t capacity = 0;
    const char *text = score->kind == CMDR_TEXT ? score->text : NULL;
    for (size_t i = 0;; ++i) {
        const char *line;
        size_t n;
        if (score->kind == CMDR_LIST) {
            if (i == score->count) break;
            if (score->items[i].kind != CMDR_TEXT) {
                free(*items);
                *items = NULL;
                return cmdr_error(error, "%s: item %zu is not text", name, i + 1);
            }
            line = score->items[i].text;
            n = strlen(line);
        } else {
            if (!*text) break;
            const char *newline = strchr(text, '\n');
            line = text;
            n = newline ? (size_t)(newline - text) : strlen(text);
            text = newline ? newline + 1 : text + n;
        }
        if (*count == capacity) {
            capacity = capacity ? 2 * capacity : 16;
            *items = cmdr_realloc(*items, capacity, sizeof(item));
        }
        if (parse_item(name, i + 1, line, n, &(*items)[*count], error)) {
            free(*items);
            *items = NULL;
            return 1;
        }
        ++*count;
    }
    return 0;
}

static int parse_bpm(const char *name, const char *text, double *bpm, char **error)
{
    if (number(text, bpm) || !tempo_of(*bpm))
        return cmdr_error(error, "%s: BPM %s is not 3.6 to 60000000", name, text);
    return 0;
}

/* The beat at which the last item ends. */
static double score_end(const item *items, size_t count)
{
    double end = 0;
    for (size_t i = 0; i < count; ++i)
        if (items[i].start + items[i].length > end) end = items[i].start + items[i].length;
    return end;
}

/* A message at a time in beats. A tempo change has size 0 and carries bpm. */
typedef struct {
    double at, bpm;
    size_t item;
    int rank; /* the item's kind; note-offs rank before note-ons */
    unsigned char size, bytes[3];
} event;

enum { RANK_OFF = NOTE, RANK_ON = NOTE + 1 };

/* By time, then rank, then score order. A note of length 0 sends its own
 * note-on before its note-off. */
static int by_time(const void *a, const void *b)
{
    const event *x = a, *y = b;
    if (x->at != y->at) return x->at < y->at ? -1 : 1;
    if (x->item == y->item) return y->rank - x->rank;
    if (x->rank != y->rank) return x->rank - y->rank;
    return x->item < y->item ? -1 : x->item > y->item;
}

/* The score's messages, sorted; rests send none. */
static event *events_of(const item *items, size_t count, size_t *n)
{
    event *events = cmdr_realloc(NULL, count ? 2 * count : 1, sizeof(event));
    *n = 0;
    for (size_t i = 0; i < count; ++i) {
        const item *t = &items[i];
        unsigned char ch = (unsigned char)(t->channel - 1);
        event e = {.at = t->start, .item = i, .rank = t->kind, .size = 3};
        switch (t->kind) {
        case NOTE:
            e.rank = RANK_ON;
            memcpy(e.bytes, (unsigned char[]){0x90 | ch, t->a, t->b}, 3);
            events[(*n)++] = e;
            e.at = t->start + t->length;
            e.rank = RANK_OFF;
            memcpy(e.bytes, (unsigned char[]){0x80 | ch, t->a, 0}, 3);
            break;
        case CC: memcpy(e.bytes, (unsigned char[]){0xB0 | ch, t->a, t->b}, 3); break;
        case PROGRAM:
            e.size = 2;
            memcpy(e.bytes, (unsigned char[]){0xC0 | ch, t->a}, 2);
            break;
        case BEND: {
            int v = t->a + 8192;
            memcpy(e.bytes, (unsigned char[]){0xE0 | ch, v & 0x7F, v >> 7}, 3);
            break;
        }
        case TEMPO:
            e.size = 0;
            e.bpm = t->bpm;
            break;
        case REST: continue;
        }
        events[(*n)++] = e;
    }
    qsort(events, *n, sizeof(event), by_time);
    return events;
}

/* Beats to seconds, following tempo changes as they pass. */
typedef struct {
    double beat, second, per_beat;
} tempo_map;

static double seconds_at(const tempo_map *m, double beat)
{
    return m->second + (beat - m->beat) * m->per_beat;
}

static void change_tempo(tempo_map *m, double beat, double bpm)
{
    m->second = seconds_at(m, beat);
    m->beat = beat;
    m->per_beat = 60.0 / bpm;
}

/* Output ports. */

typedef struct {
    const char *want; /* NULL lists all; "" takes the first port */
    libremidi_midi_out_port *found;
    cmdr_value names;
} port_search;

static void on_port(void *data, const libremidi_midi_out_port *port)
{
    port_search *s = data;
    const char *name;
    size_t n;
    if (libremidi_midi_out_port_name(port, &name, &n)) return;
    if (s->names.kind == CMDR_LIST) cmdr_push(&s->names, cmdr_text(name, n));
    if (s->found || !s->want) return;
    if (!*s->want || (strlen(s->want) == n && !memcmp(s->want, name, n)))
        libremidi_midi_out_port_clone(port, &s->found);
}

/* libremidi's last error, for the message. */
typedef struct {
    char text[256];
} midi_error;

static void on_error(void *data, const char *text, size_t n, const void *location)
{
    midi_error *e = data;
    snprintf(e->text, sizeof(e->text), "%.*s", (int)n, text);
}

static int enumerate(port_search *s, midi_error *e)
{
    libremidi_observer_configuration conf;
    libremidi_midi_observer_configuration_init(&conf);
    conf.on_error.context = e;
    conf.on_error.callback = on_error;
    conf.track_hardware = true;
    conf.track_virtual = true;
    libremidi_api_configuration api;
    libremidi_midi_api_configuration_init(&api);
    api.configuration_type = Observer;
    libremidi_midi_observer_handle *observer;
    if (libremidi_midi_observer_new(&conf, &api, &observer)) return 1;
    libremidi_midi_observer_enumerate_output_ports(observer, s, on_port);
    libremidi_midi_observer_free(observer);
    return 0;
}

COMMAND(ports_command)
{
    if (argc) return cmdr_error(error, "usage: midi.ports");
    port_search s = {.names = {.kind = CMDR_LIST}};
    midi_error e = {""};
    if (enumerate(&s, &e)) {
        cmdr_free(&s.names);
        return cmdr_error(error, "midi.ports: cannot list ports%s%s", *e.text ? ": " : "", e.text);
    }
    *result = s.names;
    return 0;
}

/* Playing. */

static double now(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (double)t.tv_sec + (double)t.tv_nsec / 1e9;
}

/* Sleep until monotonic time T; return 1 if interrupted. Sleeps are capped,
 * so an interrupt that does not wake nanosleep is still seen. */
static int wait_until(double t)
{
    for (;;) {
        if (cmdr_interrupted()) return 1;
        double d = t - now();
        if (d <= 0) return 0;
        if (d > 0.05) d = 0.05;
        struct timespec span = {0, (long)(d * 1e9)};
        nanosleep(&span, NULL);
    }
}

static libremidi_midi_out_handle *open_output(const char *name, const char *want, char **error)
{
    port_search s = {.want = want};
    midi_error e = {""};
    if (enumerate(&s, &e)) {
        cmdr_error(error, "%s: cannot list ports%s%s", name, *e.text ? ": " : "", e.text);
        return NULL;
    }
    if (!s.found) {
        if (*want)
            cmdr_error(error, "%s: no output port named %s; midi.ports lists them", name, want);
        else
            cmdr_error(error, "%s: no MIDI output ports", name);
        return NULL;
    }
    libremidi_midi_configuration conf;
    libremidi_midi_configuration_init(&conf);
    conf.version = MIDI1;
    conf.out_port = s.found;
    conf.on_error.context = &e;
    conf.on_error.callback = on_error;
    libremidi_api_configuration api;
    libremidi_midi_api_configuration_init(&api);
    api.configuration_type = Output;
    libremidi_midi_out_handle *out = NULL;
    if (libremidi_midi_out_new(&conf, &api, &out)) {
        cmdr_error(error, "%s: cannot open %s%s%s", name, want, *e.text ? ": " : "", e.text);
        out = NULL;
    }
    libremidi_midi_out_port_free(s.found);
    return out;
}

COMMAND(play_command)
{
    if (argc < 1 || argc > 3) return cmdr_error(error, "usage: midi.play SCORE [BPM] [PORT]");
    if ((argc > 1 && argv[1].kind != CMDR_TEXT) || (argc == 3 && argv[2].kind != CMDR_TEXT))
        return cmdr_error(error, "midi.play: BPM and PORT are text");
    item *items;
    size_t count, n;
    double bpm;
    if (parse_bpm("midi.play", argc > 1 ? argv[1].text : DEFAULT_BPM, &bpm, error)) return 1;
    if (parse_score("midi.play", &argv[0], &items, &count, error)) return 1;
    libremidi_midi_out_handle *out = open_output("midi.play", argc == 3 ? argv[2].text : "", error);
    if (!out) {
        free(items);
        return 1;
    }
    double end = score_end(items, count);
    event *events = events_of(items, count, &n);
    free(items);
    /* Notes struck and not yet released, so an interrupt can release them. */
    unsigned short sounding[16][128] = {{0}};
    tempo_map map = {0, now(), 60.0 / bpm};
    int interrupted = 0;
    for (size_t i = 0; i < n && !interrupted; ++i) {
        const event *e = &events[i];
        if ((interrupted = wait_until(seconds_at(&map, e->at)))) break;
        if (!e->size) {
            change_tempo(&map, e->at, e->bpm);
            continue;
        }
        unsigned short *s = &sounding[e->bytes[0] & 0x0F][e->bytes[1]];
        if (e->rank == RANK_ON) ++*s;
        else if (e->rank == RANK_OFF && *s) --*s;
        libremidi_midi_out_send_message(out, e->bytes, e->size);
    }
    /* Trailing rests take their time, so plays in a row keep the gap. */
    if (!interrupted) interrupted = wait_until(seconds_at(&map, end));
    for (int ch = 0; ch < 16; ++ch)
        for (int key = 0; key < 128; ++key)
            for (; sounding[ch][key]; --sounding[ch][key]) {
                unsigned char off[3] = {(unsigned char)(0x80 | ch), (unsigned char)key, 0};
                libremidi_midi_out_send_message(out, off, 3);
            }
    free(events);
    libremidi_midi_out_free(out);
    if (interrupted) return cmdr_error(error, "midi.play: interrupted");
    return cmdr_clone(&argv[0], result, error);
}

/* Writing. */

/* Write SIZE bytes to a temporary file beside PATH, then rename it, so a
 * failure leaves PATH unchanged. */
static int write_file(const char *name, const char *path, const unsigned char *data, size_t size,
                      char **error)
{
    size_t n = strlen(path);
    char *temp = cmdr_realloc(NULL, n + 8, 1);
    memcpy(temp, path, n);
    memcpy(temp + n, ".XXXXXX", 8);
    int fd = mkstemp(temp);
    if (fd < 0) {
        cmdr_error(error, "%s: cannot write %s: %s", name, path, strerror(errno));
        free(temp);
        return 1;
    }
    int status = 0;
    for (size_t done = 0; done < size && !status;) {
        ssize_t w = write(fd, data + done, size - done);
        if (w < 0 && errno != EINTR) status = errno;
        if (w > 0) done += (size_t)w;
    }
    pthread_once(&file_mode_once, read_umask);
    if (!status && fchmod(fd, file_mode)) status = errno;
    if (close(fd) && !status) status = errno;
    if (!status && rename(temp, path)) status = errno;
    if (status) {
        unlink(temp);
        cmdr_error(error, "%s: cannot write %s: %s", name, path, strerror(status));
    }
    free(temp);
    return status != 0;
}

static smf_event tempo_event(long tick, double bpm)
{
    long mpqn = tempo_of(bpm);
    return (smf_event){tick, 6, {0xFF, 0x51, 0x03, mpqn >> 16, (mpqn >> 8) & 0xFF, mpqn & 0xFF}};
}

static long tick_of(double beat) { return (long)round(beat * TPQ); }

COMMAND(write_command)
{
    if (argc < 2 || argc > 3) return cmdr_error(error, "usage: midi.write SCORE PATH [BPM]");
    if (argv[1].kind != CMDR_TEXT || (argc == 3 && argv[2].kind != CMDR_TEXT))
        return cmdr_error(error, "midi.write: PATH and BPM are text");
    item *items;
    size_t count, n;
    double bpm;
    if (parse_bpm("midi.write", argc == 3 ? argv[2].text : DEFAULT_BPM, &bpm, error)) return 1;
    if (parse_score("midi.write", &argv[0], &items, &count, error)) return 1;
    double end = score_end(items, count);
    if (round(end * TPQ) > MAX_TICK) {
        free(items);
        return cmdr_error(error, "midi.write: the score ends at beat %g, past what a file holds",
                          end);
    }
    /* BPM is the tempo at beat 0, unless the score sets one there. */
    int tempo_at_0 = 0;
    for (size_t i = 0; i < count; ++i)
        if (items[i].kind == TEMPO && items[i].start == 0) tempo_at_0 = 1;
    event *events = events_of(items, count, &n);
    free(items);
    smf_event *out = cmdr_realloc(NULL, n + 1, sizeof(smf_event));
    size_t used = 0;
    if (!tempo_at_0) out[used++] = tempo_event(0, bpm);
    for (size_t i = 0; i < n; ++i) {
        const event *e = &events[i];
        if (!e->size) {
            out[used++] = tempo_event(tick_of(e->at), e->bpm);
        } else {
            out[used] = (smf_event){tick_of(e->at), e->size, {0}};
            memcpy(out[used++].bytes, e->bytes, e->size);
        }
    }
    free(events);
    unsigned char *data;
    size_t size;
    int status = smf_encode(out, used, TPQ, tick_of(end), &data, &size);
    free(out);
    if (status) return cmdr_error(error, "midi.write: libremidi cannot encode the score");
    status = write_file("midi.write", argv[1].text, data, size, error);
    free(data);
    if (status) return 1;
    *result = cmdr_text(argv[1].text, strlen(argv[1].text));
    return 0;
}

/* Reading. */

/* By start, then kind as events are sent, channel, and key or controller. */
static int by_start(const void *a, const void *b)
{
    const item *x = a, *y = b;
    if (x->start != y->start) return x->start < y->start ? -1 : 1;
    if (x->kind != y->kind) return x->kind - y->kind;
    if (x->channel != y->channel) return x->channel - y->channel;
    return x->a - y->a;
}

/* By tick, keeping file order at equal ticks; seq is the file order. */
typedef struct {
    smf_event e;
    size_t seq;
} ordered;

static int by_tick(const void *a, const void *b)
{
    const ordered *x = a, *y = b;
    if (x->e.tick != y->e.tick) return x->e.tick < y->e.tick ? -1 : 1;
    return x->seq < y->seq ? -1 : x->seq > y->seq;
}

static unsigned char *read_file(const char *path, size_t *size, char **error)
{
    FILE *f = fopen(path, "rb");
    if (!f) {
        cmdr_error(error, "midi.read: cannot read %s: %s", path, strerror(errno));
        return NULL;
    }
    unsigned char *data = NULL;
    size_t used = 0, capacity = 0, n;
    do {
        if (used == capacity) {
            capacity = capacity ? 2 * capacity : 4096;
            data = cmdr_realloc(data, capacity, 1);
        }
        n = fread(data + used, 1, capacity - used, f);
        used += n;
    } while (n);
    int failed = ferror(f);
    fclose(f);
    if (failed) {
        free(data);
        cmdr_error(error, "midi.read: cannot read %s", path);
        return NULL;
    }
    *size = used;
    return data;
}

/* BPM with the fewest significant digits that give the same microseconds per
 * beat, printed as numbers print. */
static cmdr_value bpm_value(long mpqn)
{
    double bpm = 60e6 / (double)mpqn;
    char text[32];
    for (int digits = 1; digits < 17; ++digits) {
        snprintf(text, sizeof(text), "%.*g", digits, bpm);
        double shorter = strtod(text, NULL);
        if (tempo_of(shorter) == mpqn) return cmdr_number_value(shorter);
    }
    return cmdr_number_value(bpm);
}

static cmdr_value item_text(const item *t)
{
    cmdr_value start = cmdr_number_value(t->start), length = cmdr_number_value(t->length);
    const char *k = kind_names[t->kind];
    char line[160];
    int n = 0;
    switch (t->kind) {
    case NOTE:
        n = snprintf(line, sizeof(line), "%s %s %s %d %d %d", k, start.text, length.text, t->a,
                     t->b, t->channel);
        break;
    case CC:
        n = snprintf(line, sizeof(line), "%s %s %d %d %d", k, start.text, t->a, t->b, t->channel);
        break;
    case PROGRAM:
    case BEND:
        n = snprintf(line, sizeof(line), "%s %s %d %d", k, start.text, t->a, t->channel);
        break;
    case TEMPO: {
        cmdr_value bpm = bpm_value(t->a);
        n = snprintf(line, sizeof(line), "%s %s %s", k, start.text, bpm.text);
        cmdr_free(&bpm);
        break;
    }
    case REST: n = snprintf(line, sizeof(line), "%s %s %s", k, start.text, length.text); break;
    }
    cmdr_free(&start);
    cmdr_free(&length);
    return cmdr_text(line, (size_t)n);
}

/* Note-ons pair with note-offs, or note-ons of velocity 0, first in, first
 * out per channel and key. Notes still sounding end at the file's end. */
COMMAND(read_command)
{
    if (argc != 1) return cmdr_error(error, "usage: midi.read PATH");
    size_t size;
    unsigned char *data = read_file(argv[0].text, &size, error);
    if (!data) return 1;
    smf_event *events;
    size_t count;
    unsigned tpq;
    long last;
    int status = smf_decode(data, size, &events, &count, &tpq, &last);
    free(data);
    if (status)
        return cmdr_error(error, "midi.read: %s is not a MIDI file timed in ticks per beat",
                          argv[0].text);
    ordered *sorted = cmdr_realloc(NULL, count ? count : 1, sizeof(ordered));
    for (size_t i = 0; i < count; ++i) sorted[i] = (ordered){events[i], i};
    free(events);
    qsort(sorted, count, sizeof(ordered), by_tick);

    /* Starts and lengths are in ticks until the end; a length of -1 is a
     * note still open. */
    item *items = cmdr_realloc(NULL, count + 1, sizeof(item));
    size_t made = 0, *next = cmdr_realloc(NULL, count + 1, sizeof(size_t));
    /* Per channel and key, the open notes as a queue linked through next,
     * with indices stored plus 1, so 0 is empty. */
    size_t (*head)[128] = cmdr_realloc(NULL, 32, sizeof(*head)), (*tail)[128] = head + 16;
    memset(head, 0, 32 * sizeof(*head));
    for (size_t i = 0; i < count; ++i) {
        const unsigned char *b = sorted[i].e.bytes;
        double at = (double)sorted[i].e.tick;
        int ch = b[0] & 0x0F, type = b[0] & 0xF0;
        item t = {.start = at, .channel = ch + 1};
        if (b[0] == 0xFF) {
            t = (item){.kind = TEMPO, .start = at, .a = (b[3] << 16) | (b[4] << 8) | b[5]};
            if (!t.a) continue;
        } else if (type == 0x90 && b[2]) {
            t.kind = NOTE, t.a = b[1], t.b = b[2], t.length = -1;
            next[made] = 0;
            if (tail[ch][b[1]]) next[tail[ch][b[1]] - 1] = made + 1;
            else head[ch][b[1]] = made + 1;
            tail[ch][b[1]] = made + 1;
        } else if (type == 0x80 || type == 0x90) {
            size_t k = head[ch][b[1]];
            if (k--) {
                items[k].length = at - items[k].start;
                head[ch][b[1]] = next[k];
                if (!head[ch][b[1]]) tail[ch][b[1]] = 0;
            }
            continue;
        } else if (type == 0xB0) {
            t.kind = CC, t.a = b[1], t.b = b[2];
        } else if (type == 0xC0) {
            t.kind = PROGRAM, t.a = b[1];
        } else if (type == 0xE0) {
            t.kind = BEND, t.a = (b[1] | b[2] << 7) - 8192;
        } else {
            continue;
        }
        items[made++] = t;
    }
    free(sorted);
    free(next);
    free(head);
    double end = 0;
    for (size_t i = 0; i < made; ++i) {
        item *t = &items[i];
        if (t->length < 0) t->length = (double)last - t->start;
        if (t->start + t->length > end) end = t->start + t->length;
    }
    /* A file that ends after its last item ends with a rest. */
    if ((double)last > end)
        items[made++] = (item){.kind = REST, .start = end, .length = (double)last - end};
    for (size_t i = 0; i < made; ++i) {
        items[i].start /= tpq;
        items[i].length /= tpq;
    }
    qsort(items, made, sizeof(item), by_start);
    *result = (cmdr_value){.kind = CMDR_LIST};
    for (size_t i = 0; i < made; ++i) cmdr_push(result, item_text(&items[i]));
    free(items);
    return 0;
}

COMMAND(key_command)
{
    if (argc != 1) return cmdr_error(error, "usage: midi.key KEY");
    int key = parse_key(argv[0].text);
    if (key < 0)
        return cmdr_error(error, "midi.key: %s is not 0 to 127 or a name such as C4", argv[0].text);
    char text[4];
    *result = cmdr_text(text, (size_t)snprintf(text, sizeof(text), "%d", key));
    return 0;
}

/* A run of steps: one group in Bjorklund's algorithm. */
typedef struct {
    char *steps;
    size_t n;
} group;

static group join_groups(group a, group b)
{
    a.steps = cmdr_realloc(a.steps, a.n + b.n, 1);
    memcpy(a.steps + a.n, b.steps, b.n);
    a.n += b.n;
    free(b.steps);
    return a;
}

/* Bjorklund's algorithm, as Toussaint describes it: K groups "1", then N-K
 * groups "0". Append the second set's groups to the first set's, pairwise;
 * the unpaired groups become the second set. Pair once, then again while the
 * second set has more than one group, so E(3,4) is 1011, not 1110. */
COMMAND(euclid_command)
{
    if (argc != 2) return cmdr_error(error, "usage: midi.bjorklund K N");
    int k, n;
    if (integer(argv[1].text, 1, MAX_STEPS, &n))
        return cmdr_error(error, "midi.bjorklund: N %s is not 1 to %d", argv[1].text, MAX_STEPS);
    if (integer(argv[0].text, 0, n, &k))
        return cmdr_error(error, "midi.bjorklund: K %s is not 0 to %d", argv[0].text, n);
    /* One array: the first set's na groups, then the second set's nb. */
    group *g = cmdr_realloc(NULL, (size_t)n, sizeof(group));
    size_t na = (size_t)k, nb = (size_t)(n - k);
    for (int i = 0; i < n; ++i) {
        g[i].steps = cmdr_realloc(NULL, 1, 1);
        g[i].steps[0] = i < k ? '1' : '0';
        g[i].n = 1;
    }
    for (int first = 1; na && nb && (first || nb > 1); first = 0) {
        size_t m = na < nb ? na : nb;
        for (size_t i = 0; i < m; ++i) g[i] = join_groups(g[i], g[na + i]);
        /* The unpaired groups of the longer set follow the m joined ones. */
        size_t from = na > m ? m : na + m, left = (na > m ? na : nb) - m;
        memmove(g + m, g + from, left * sizeof(group));
        na = m;
        nb = left;
    }
    *result = (cmdr_value){.kind = CMDR_LIST};
    for (size_t i = 0; i < na + nb; ++i) {
        for (size_t j = 0; j < g[i].n; ++j) cmdr_push(result, cmdr_text(&g[i].steps[j], 1));
        free(g[i].steps);
    }
    free(g);
    return 0;
}

#define TEXT(n, u, d, f) {.name = n, .usage = u, .docstring = d, .run = f, .text = 1}
static const cmdr_command commands[] = {
    TEXT("key", "key KEY", "Return KEY, a number or a name such as C4 or F#3, as 0 to 127",
         key_command),
    TEXT("bjorklund", "bjorklund K N",
         "List N steps, 1 on K of them spread evenly and 0 elsewhere", euclid_command),
    TEXT("ports", "ports", "List MIDI output ports", ports_command),
    {.name = "play", .usage = "play SCORE [BPM] [PORT]",
     .docstring = "Play SCORE at BPM, default 120, on PORT, or the first output port; return SCORE",
     .run = play_command},
    {.name = "write", .usage = "write SCORE PATH [BPM]",
     .docstring = "Write SCORE to the MIDI file PATH at BPM, default 120; return PATH",
     .run = write_command},
    TEXT("read", "read PATH", "List the items of the MIDI file PATH", read_command),
};
const cmdr cmdr_ext_midi = {commands, sizeof(commands) / sizeof(commands[0]), NULL};
