#define _POSIX_C_SOURCE 200809L
#include "cmdr.h"

#include <errno.h>
#include <math.h>
#include <pthread.h>
#include <signal.h>
#include <stdint.h>
#include <sox.h>
#include <soxr.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define COMMAND(name) \
    static int name(const cmdr *repl, void *user, size_t argc, const cmdr_value *argv, \
                    cmdr_value *result, char **error)

/* libsox keeps global state, so one snd command runs at a time. */
static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static int ready;
/* The last failure libsox reported, for the error message. */
static char message[512];
/* Set by SIGINT during a run; the flow callback then stops the chain. */
static volatile sig_atomic_t interrupted;

static void on_message(unsigned level, const char *file, const char *format, va_list ap)
{
    (void)file;
    if (level == 1) {
        vsnprintf(message, sizeof(message), format, ap);
    } else if (level == 2) {
        fputs("snd: warning: ", stderr);
        vfprintf(stderr, format, ap);
        fputc('\n', stderr);
    }
}

static int enter(const char *name, char **error)
{
    pthread_mutex_lock(&lock);
    if (!ready) {
        if (sox_init() != SOX_SUCCESS) {
            pthread_mutex_unlock(&lock);
            return cmdr_error(error, "%s: cannot initialise libsox", name);
        }
        sox_globals.output_message_handler = on_message;
        sox_globals.verbosity = 2;
        ready = 1;
    }
    message[0] = '\0';
    return 0;
}

static void leave(void) { pthread_mutex_unlock(&lock); }

/* Fail with WHAT, followed by libsox's own message if it gave one. */
static int fail(char **error, const char *name, const char *what)
{
    return cmdr_error(error, "%s: %s%s%s", name, what, message[0] ? ": " : "", message);
}

/* varispeed FROM TO: speed moves linearly from FROM to TO over the output,
 * with libsoxr's variable-rate mode, as in its examples/5-variable-rate.c. N
 * input frames give exactly round(2N/(FROM+TO)), the length start reports to
 * later effects; soxr gives about 0.1% fewer, so the end is padded with
 * silence. The ramp follows output, not input consumed, since soxr reads
 * input ahead of its output. */

/* FROM and TO lie in [1/MAX_SPEED, MAX_SPEED], the range of that example. */
#define MAX_SPEED 32.0

typedef struct {
    double from, to, length;
    sox_uint64_t done, target;
    soxr_t soxr;
} varispeed;

static int varispeed_getopts(sox_effect_t *effp, int argc, char **argv)
{
    varispeed *p = effp->priv;
    double *x[] = {&p->from, &p->to};
    int ok = argc == 3;
    for (int i = 0; ok && i < 2; ++i) {
        char *end;
        *x[i] = strtod(argv[i + 1], &end);
        ok = end != argv[i + 1] && !*end && *x[i] >= 1 / MAX_SPEED && *x[i] <= MAX_SPEED;
    }
    if (ok) return SOX_SUCCESS;
    snprintf(message, sizeof(message), "usage: %s; each from 1/32 to 32",
             effp->handler.usage);
    return SOX_EOF;
}

static int varispeed_start(sox_effect_t *effp)
{
    varispeed *p = effp->priv;
    sox_uint64_t length = effp->in_signal.length;
    if (!length || length == SOX_UNKNOWN_LEN) {
        snprintf(message, sizeof(message), "varispeed: audio length is unknown");
        return SOX_EOF;
    }
    if (p->from == 1 && p->to == 1) return SOX_EFF_NULL;
    unsigned ch = effp->in_signal.channels;
    p->length = 2 * (double)(length / ch) / (p->from + p->to);
    p->done = 0;
    p->target = (sox_uint64_t)(p->length + .5);
    effp->out_signal.length = p->target * ch;
    soxr_io_spec_t io = soxr_io_spec(SOXR_INT32_I, SOXR_INT32_I);
    soxr_quality_spec_t quality = soxr_quality_spec(SOXR_HQ, SOXR_VR);
    soxr_error_t e = NULL;
    /* In VR mode the creation ratio is the largest io ratio used later. */
    p->soxr = soxr_create(fmax(p->from, p->to), 1, ch, &e, &io, &quality, NULL);
    if (!e) e = soxr_set_io_ratio(p->soxr, p->from, 0);
    if (!e) return SOX_SUCCESS;
    snprintf(message, sizeof(message), "varispeed: %s", e);
    soxr_delete(p->soxr);
    p->soxr = NULL;
    return SOX_EOF;
}

/* Resample into OBUF, moving the ratio to its value at the end of this block.
 * Past the target length, input is consumed and dropped. */
static int varispeed_process(sox_effect_t *effp, const sox_sample_t *ibuf, size_t *isamp,
                             sox_sample_t *obuf, size_t *osamp)
{
    varispeed *p = effp->priv;
    unsigned ch = effp->in_signal.channels;
    size_t olen = *osamp / ch, idone = 0, odone = 0;
    if (olen > p->target - p->done) olen = (size_t)(p->target - p->done);
    if (!olen) {
        *osamp = 0;
        return SOX_SUCCESS;
    }
    double position = fmin(((double)p->done + (double)olen) / p->length, 1);
    soxr_error_t e = soxr_set_io_ratio(p->soxr, p->from + (p->to - p->from) * position, olen);
    /* NULL input tells soxr the input has ended, so it flushes. */
    if (!e) e = soxr_process(p->soxr, ibuf, ibuf ? *isamp / ch : 0, &idone, obuf, olen, &odone);
    if (e) {
        snprintf(message, sizeof(message), "varispeed: %s", e);
        return SOX_EOF;
    }
    p->done += odone;
    if (isamp) *isamp = idone * ch;
    *osamp = odone * ch;
    return SOX_SUCCESS;
}

static int varispeed_flow(sox_effect_t *effp, const sox_sample_t *ibuf, sox_sample_t *obuf,
                          size_t *isamp, size_t *osamp)
{
    return varispeed_process(effp, ibuf, isamp, obuf, osamp);
}

static int varispeed_drain(sox_effect_t *effp, sox_sample_t *obuf, size_t *osamp)
{
    varispeed *p = effp->priv;
    unsigned ch = effp->in_signal.channels;
    size_t room = *osamp;
    int status = varispeed_process(effp, NULL, NULL, obuf, osamp);
    if (status != SOX_SUCCESS) return status;
    if (!*osamp && p->done < p->target) {
        size_t n = room / ch;
        if (n > p->target - p->done) n = (size_t)(p->target - p->done);
        memset(obuf, 0, n * ch * sizeof(*obuf));
        p->done += n;
        *osamp = n * ch;
    }
    return *osamp ? SOX_SUCCESS : SOX_EOF;
}

static int varispeed_stop(sox_effect_t *effp)
{
    varispeed *p = effp->priv;
    effp->clips += *soxr_num_clips(p->soxr);
    soxr_delete(p->soxr);
    return SOX_SUCCESS;
}

static const sox_effect_handler_t varispeed_handler = {
    "varispeed", "FROM TO", SOX_EFF_MCHAN | SOX_EFF_LENGTH, varispeed_getopts, varispeed_start,
    varispeed_flow, varispeed_drain, varispeed_stop, NULL, sizeof(varispeed),
};

/* An effect by name: libsox's, or varispeed. */
static const sox_effect_handler_t *find_effect(const char *name)
{
    return !strcmp(name, varispeed_handler.name) ? &varispeed_handler : sox_find_effect(name);
}

/* Append the text in VALUE, or in its items at any depth, to WORDS. */
static int flatten(const char *name, const cmdr_value *value, const char ***words, size_t *count,
                   char **error)
{
    if (value->kind == CMDR_LIST) {
        for (size_t i = 0; i < value->count; ++i)
            if (flatten(name, &value->items[i], words, count, error)) return 1;
        return 0;
    }
    if (value->kind != CMDR_TEXT) return cmdr_error(error, "%s: arguments must be text", name);
    *words = cmdr_realloc(*words, *count + 1, sizeof(**words));
    (*words)[(*count)++] = value->text;
    return 0;
}

/* A file and the format options before it, as sox takes them. */
typedef struct {
    const char *path;
    const char *type;
    sox_signalinfo_t signal;
    sox_encodinginfo_t encoding;
    double volume; /* HUGE_VAL if not given. */
    sox_format_t *ft;
} file_spec;

static void init_spec(file_spec *f)
{
    memset(f, 0, sizeof(*f));
    sox_init_encodinginfo(&f->encoding);
    f->volume = HUGE_VAL;
}

static const struct {
    const char *name;
    sox_encoding_t encoding;
} encodings[] = {
    {"signed-integer", SOX_ENCODING_SIGN2}, {"unsigned-integer", SOX_ENCODING_UNSIGNED},
    {"floating-point", SOX_ENCODING_FLOAT}, {"ms-adpcm", SOX_ENCODING_MS_ADPCM},
    {"ima-adpcm", SOX_ENCODING_IMA_ADPCM},  {"oki-adpcm", SOX_ENCODING_OKI_ADPCM},
    {"gsm-full-rate", SOX_ENCODING_GSM},    {"u-law", SOX_ENCODING_ULAW},
    {"mu-law", SOX_ENCODING_ULAW},          {"a-law", SOX_ENCODING_ALAW},
};

/* A positive number, with an optional k suffix for thousands, as sox's -r. */
static int positive(const char *name, const char *option, const char *text, double *x,
                    char **error)
{
    char *end;
    errno = 0;
    *x = strtod(text, &end);
    if (end != text && *end == 'k') *x *= 1000, ++end;
    if (errno || end == text || *end || !(*x > 0) || !isfinite(*x))
        return cmdr_error(error, "%s: %s needs a positive number, got %s", name, option, text);
    return 0;
}

/* Parse the format option at WORDS[*I] into F; *matched is 0 if it is not one. */
static int format_option(const char *name, const char *const *words, size_t count, size_t *i,
                         file_spec *f, int *matched, char **error)
{
    static const char *const options[][2] = {
        {"-v", "--volume"}, {"-t", "--type"}, {"-r", "--rate"},
        {"-c", "--channels"}, {"-b", "--bits"}, {"-e", "--encoding"},
    };
    const char *w = words[*i];
    size_t k = 0, n = sizeof(options) / sizeof(options[0]);
    while (k < n && strcmp(w, options[k][0]) && strcmp(w, options[k][1])) ++k;
    *matched = k < n;
    if (!*matched) return 0;
    if (*i + 1 >= count) return cmdr_error(error, "%s: %s needs a value", name, w);
    const char *v = words[++*i];
    double x;
    switch (k) {
    case 0: {
        char *end;
        f->volume = strtod(v, &end);
        if (end == v || *end || !isfinite(f->volume))
            return cmdr_error(error, "%s: -v needs a number, got %s", name, v);
        return 0;
    }
    case 1: f->type = v; return 0;
    case 2: if (positive(name, w, v, &x, error)) return 1; f->signal.rate = x; return 0;
    case 3:
        if (positive(name, w, v, &x, error)) return 1;
        f->signal.channels = (unsigned)x;
        return 0;
    case 4:
        if (positive(name, w, v, &x, error)) return 1;
        f->encoding.bits_per_sample = (unsigned)x;
        return 0;
    default:
        for (size_t e = 0; e < sizeof(encodings) / sizeof(encodings[0]); ++e)
            if (!strcmp(v, encodings[e].name)) {
                f->encoding.encoding = encodings[e].encoding;
                return 0;
            }
        return cmdr_error(error, "%s: unknown encoding: %s", name, v);
    }
}

/* The default audio device, as sox's -d: AUDIODRIVER, else the first driver
 * libsox has; AUDIODEV, else "default". */
static int default_device(const char *name, file_spec *f, char **error)
{
    static const char *const drivers[] = {"coreaudio", "pulseaudio", "alsa", "waveaudio",
                                          "sndio", "oss", "sunau", "ao"};
    const char *driver = getenv("AUDIODRIVER");
    for (size_t i = 0; !driver && i < sizeof(drivers) / sizeof(drivers[0]); ++i)
        if (sox_find_format(drivers[i], sox_false)) driver = drivers[i];
    if (!driver) return cmdr_error(error, "%s: no audio device driver in libsox", name);
    const char *device = getenv("AUDIODEV");
    f->type = driver;
    f->path = device ? device : "default";
    return 0;
}

/* Parse a file: format options, then a path, -n (the null file), or -d (the
 * default device). *done is set once the file itself is parsed. */
static int file_word(const char *name, const char *const *words, size_t count, size_t *i,
                     file_spec *f, int *done, char **error)
{
    int matched;
    *done = 0;
    if (format_option(name, words, count, i, f, &matched, error)) return 1;
    if (matched) return 0;
    const char *w = words[*i];
    if (!strcmp(w, "-n")) {
        f->type = "null";
        f->path = "-n";
    } else if (!strcmp(w, "-d")) {
        if (default_device(name, f, error)) return 1;
    } else if (w[0] == '-' && w[1]) {
        return cmdr_error(error, "%s: unknown option: %s", name, w);
    } else {
        f->path = w;
    }
    *done = 1;
    return 0;
}

typedef enum { SEQUENCE, CONCATENATE, MIX, MIX_POWER, MERGE, MULTIPLY } combine_method;

static const char *const methods[] = {
    [SEQUENCE] = "sequence", [CONCATENATE] = "concatenate", [MIX] = "mix",
    [MIX_POWER] = "mix-power", [MERGE] = "merge", [MULTIPLY] = "multiply",
};

typedef struct {
    const char *name;
    size_t argc;
    char **argv;
    sox_effect_t *effect;
} effect_spec;

typedef struct {
    const char *name;
    file_spec out;
    const char *out_type;
    const sox_format_handler_t *out_handler;
    file_spec *inputs;
    size_t input_count;
    combine_method method;
    int no_dither;
    effect_spec *effects;
    size_t effect_count;
    sox_signalinfo_t combined;
} run;

/* The extension of PATH's basename, or NULL. */
static const char *extension(const char *path)
{
    const char *slash = strrchr(path, '/'), *dot = strrchr(path, '.');
    return dot && (!slash || dot > slash) && dot[1] ? dot + 1 : NULL;
}

static int is_effect(const char *w)
{
    return find_effect(w) != NULL || !strcmp(w, ":") || !strcmp(w, "newfile")
           || !strcmp(w, "restart");
}

/* Parse OUT, then [gopts] [[fopts] infile]... [effect [effopt]]... */
static int parse(run *r, const char *const *out, size_t out_count, const char *const *words,
                 size_t count, char **error)
{
    const char *name = r->name;
    size_t i = 0;
    int done = 0;
    init_spec(&r->out);
    for (; i < out_count && !done; ++i)
        if (file_word(name, out, out_count, &i, &r->out, &done, error)) return 1;
    if (!done) return cmdr_error(error, "%s: OUT names no file", name);
    if (i < out_count) return cmdr_error(error, "%s: OUT has more than one file: %s", name, out[i]);
    if (r->out.volume != HUGE_VAL)
        return cmdr_error(error, "%s: -v applies only to an input; use the gain effect", name);
    /* Checked here, before effects parse their arguments, since an effect
     * not yet in a chain cannot always be freed whole. */
    r->out_type = r->out.type ? r->out.type : extension(r->out.path);
    if (!r->out_type)
        return cmdr_error(error, "%s: %s has no extension; give -t TYPE", name, r->out.path);
    if (!(r->out_handler = sox_write_handler(r->out.path, r->out_type, NULL)))
        return cmdr_error(error, "%s: cannot write type %s", name, r->out_type);

    r->method = CONCATENATE;
    int method_given = 0, pending = 0;
    file_spec f;
    init_spec(&f);
    for (i = 0; i < count && !is_effect(words[i]); ++i) {
        const char *w = words[i];
        if (!strcmp(w, "--combine")) {
            if (i + 1 >= count) return cmdr_error(error, "%s: --combine needs a method", name);
            size_t k = 0, n = sizeof(methods) / sizeof(methods[0]);
            while (k < n && strcmp(words[i + 1], methods[k])) ++k;
            if (k == n)
                return cmdr_error(error, "%s: unknown --combine method: %s", name, words[i + 1]);
            r->method = (combine_method)k, method_given = 1, ++i;
        } else if (!strcmp(w, "-m") || !strcmp(w, "-M") || !strcmp(w, "-T")) {
            r->method = w[1] == 'm' ? MIX : w[1] == 'M' ? MERGE : MULTIPLY, method_given = 1;
        } else if (!strcmp(w, "-D") || !strcmp(w, "--no-dither")) {
            r->no_dither = 1;
        } else {
            if (file_word(name, words, count, &i, &f, &done, error)) return 1;
            pending = !done;
            if (done) {
                r->inputs = cmdr_realloc(r->inputs, r->input_count + 1, sizeof(*r->inputs));
                r->inputs[r->input_count++] = f;
                init_spec(&f);
            }
        }
    }
    if (pending) return cmdr_error(error, "%s: format options without a file", name);
    if (!r->input_count) return cmdr_error(error, "%s: no input file", name);
    if (r->input_count == 1 && r->method == SEQUENCE) r->method = CONCATENATE;
    if (method_given && r->method >= MIX && r->input_count < 2)
        return cmdr_error(error, "%s: %s needs at least two inputs", name, methods[r->method]);

    while (i < count) {
        const char *w = words[i];
        if (!find_effect(w))
            return cmdr_error(error, "%s: %s is not supported; use snd.split-*", name, w);
        effect_spec e = {.name = w};
        size_t start = ++i;
        while (i < count && !is_effect(words[i])) ++i;
        e.argc = i - start;
        /* Effects may keep pointers into their arguments, so these live until
         * the chain is deleted. */
        e.argv = cmdr_realloc(NULL, e.argc, sizeof(*e.argv));
        for (size_t j = 0; j < e.argc; ++j)
            e.argv[j] = cmdr_strndup(words[start + j], strlen(words[start + j]));
        r->effects = cmdr_realloc(r->effects, r->effect_count + 1, sizeof(*r->effects));
        r->effects[r->effect_count++] = e;
    }
    return 0;
}

/* The input combiner, as sox.c's: reads every input and combines them. */
typedef struct {
    run *r;
    sox_sample_t **ibuf;
    size_t *ilen;
    size_t current;
} combiner;

static int serial(combine_method m) { return m == SEQUENCE || m == CONCATENATE; }

static size_t read_wide(const run *r, sox_format_t *ft, sox_sample_t *buf, size_t max)
{
    size_t len = max / r->combined.channels;
    return sox_read(ft, buf, len * ft->signal.channels) / ft->signal.channels;
}

static void balance(sox_sample_t *buf, size_t wide, const file_spec *f, sox_uint64_t *clips)
{
    if (f->volume == 1 || f->volume == HUGE_VAL) return;
    for (size_t s = 0; s < wide * f->ft->signal.channels; ++s) {
        double d = f->volume * buf[s];
        buf[s] = SOX_ROUND_CLIP_COUNT(d, *clips);
    }
}

static int combiner_start(sox_effect_t *effp)
{
    combiner *z = effp->priv;
    run *r = z->r;
    if (!serial(r->method)) {
        z->ibuf = cmdr_realloc(NULL, r->input_count, sizeof(*z->ibuf));
        for (size_t i = 0; i < r->input_count; ++i)
            z->ibuf[i] = cmdr_realloc(NULL, sox_globals.bufsiz, sizeof(**z->ibuf));
    }
    z->ilen = cmdr_realloc(NULL, r->input_count, sizeof(*z->ilen));
    return SOX_SUCCESS;
}

static int combiner_drain(sox_effect_t *effp, sox_sample_t *obuf, size_t *osamp)
{
    combiner *z = effp->priv;
    run *r = z->r;
    size_t olen = 0;
    if (serial(r->method)) {
        while (z->current < r->input_count) {
            file_spec *f = &r->inputs[z->current];
            olen = read_wide(r, f->ft, obuf, *osamp);
            if (!olen && f->ft->sox_errno) return SOX_EOF;
            if (olen) {
                balance(obuf, olen, f, &effp->clips);
                break;
            }
            ++z->current;
        }
    } else {
        sox_sample_t *p = obuf;
        for (size_t i = 0; i < r->input_count; ++i) {
            file_spec *f = &r->inputs[i];
            z->ilen[i] = read_wide(r, f->ft, z->ibuf[i], *osamp);
            balance(z->ibuf[i], z->ilen[i], f, &effp->clips);
            if (z->ilen[i] > olen) olen = z->ilen[i];
        }
        for (size_t ws = 0; ws < olen; ++ws) {
            if (r->method == MERGE) {
                for (size_t i = 0; i < r->input_count; ++i) {
                    unsigned ch = r->inputs[i].ft->signal.channels;
                    for (unsigned s = 0; s < ch; ++s)
                        *p++ = ws < z->ilen[i] ? z->ibuf[i][ws * ch + s] : 0;
                }
                continue;
            }
            for (unsigned s = 0; s < effp->in_signal.channels; ++s, ++p) {
                double acc = 0;
                for (size_t i = 0; i < r->input_count; ++i) {
                    unsigned ch = r->inputs[i].ft->signal.channels;
                    double x = ws < z->ilen[i] && s < ch ? z->ibuf[i][ws * ch + s] : 0;
                    /* multiply scales products back to the sample range. */
                    acc = r->method != MULTIPLY ? acc + x : i ? acc * x * (-1. / SOX_SAMPLE_MIN) : x;
                }
                *p = SOX_ROUND_CLIP_COUNT(acc, effp->clips);
            }
        }
    }
    olen *= effp->in_signal.channels;
    *osamp = olen;
    return olen ? SOX_SUCCESS : SOX_EOF;
}

static int combiner_stop(sox_effect_t *effp)
{
    combiner *z = effp->priv;
    if (z->ibuf) {
        for (size_t i = 0; i < z->r->input_count; ++i) free(z->ibuf[i]);
        free(z->ibuf);
    }
    free(z->ilen);
    return SOX_SUCCESS;
}

static const sox_effect_handler_t combiner_handler = {
    "input", NULL, SOX_EFF_MCHAN | SOX_EFF_MODIFY, NULL, combiner_start, NULL,
    combiner_drain, combiner_stop, NULL, sizeof(combiner),
};

/* The combined signal, as sox.c's calculate_combiner_signal_parameters. */
static int combine_signal(run *r, char **error)
{
    unsigned total = 0, min_ch = UINT32_MAX, max_ch = 0;
    double min_rate = HUGE_VAL, max_rate = 0;
    sox_uint64_t length = 0, longest = 0;
    for (size_t i = 0; i < r->input_count; ++i) {
        sox_signalinfo_t *s = &r->inputs[i].ft->signal;
        /* A null input takes its channels from the first effect that sets
         * them, as synth does, else 1. */
        for (size_t j = 0; j < r->effect_count && !s->channels; ++j)
            s->channels = r->effects[j].effect->in_signal.channels;
        if (!s->channels) s->channels = 1;
        total += s->channels;
        if (s->channels < min_ch) min_ch = s->channels;
        if (s->channels > max_ch) max_ch = s->channels;
        if (s->rate < min_rate) min_rate = s->rate;
        if (s->rate > max_rate) max_rate = s->rate;
        if (longest != SOX_UNKNOWN_LEN)
            longest = !s->length ? SOX_UNKNOWN_LEN
                      : s->length / s->channels > longest ? s->length / s->channels : longest;
        length = length != SOX_UNKNOWN_LEN && s->length ? length + s->length : SOX_UNKNOWN_LEN;
    }
    if (min_rate != max_rate)
        return cmdr_error(error, "%s: inputs must have the same rate; resample one first",
                          r->name);
    if (min_ch != max_ch && serial(r->method))
        return cmdr_error(error, "%s: %s needs inputs with the same channels", r->name,
                          methods[r->method]);
    r->combined = r->inputs[0].ft->signal;
    r->combined.channels = r->method == MERGE ? total : max_ch;
    if (serial(r->method))
        r->combined.length = length;
    else
        r->combined.length = longest != SOX_UNKNOWN_LEN ? longest * r->combined.channels
                                                        : SOX_UNKNOWN_LEN;
    return 0;
}

/* Free an effect not added to a chain. sox_delete_effect frees priv once per
 * flow, and flows is 0 until sox_add_effect sets it. kill is not called: some
 * assume start ran, as channels does, so what getopts allocated leaks. */
static void discard(sox_effect_t *e)
{
    free(e->priv);
    free(e->obuf);
    free(e);
}

/* Add E to CHAIN, which copies it, or discard E if its start fails. */
static int add(sox_effects_chain_t *chain, sox_effect_t *e, sox_signalinfo_t *signal,
               const sox_signalinfo_t *out)
{
    int status = sox_add_effect(chain, e, signal, out);
    if (status == SOX_SUCCESS) free(e);
    else discard(e);
    return status;
}

static int auto_effect(sox_effects_chain_t *chain, const char *name, sox_signalinfo_t *signal,
                       const sox_signalinfo_t *out)
{
    sox_effect_t *e = sox_create_effect(sox_find_effect(name));
    if (sox_effect_options(e, 0, NULL) != SOX_SUCCESS) {
        discard(e);
        return SOX_EOF;
    }
    return add(chain, e, signal, out);
}

static int on_flow(sox_bool all_done, void *data)
{
    (void)all_done, (void)data;
    return interrupted ? SOX_EOF : SOX_SUCCESS;
}

static void on_interrupt(int sig)
{
    (void)sig;
    interrupted = 1;
}

static int failed_on(char **error, const char *name, const char *what, const char *path)
{
    size_t size = strlen(what) + strlen(path) + 2;
    char *text = cmdr_realloc(NULL, size, 1);
    snprintf(text, size, "%s %s", what, path);
    int status = fail(error, name, text);
    free(text);
    return status;
}

/* Open the output, writing a regular file through a temporary one that
 * execute renames to OUT only on success, so OUT may be an input. */
static sox_format_t *open_output(run *r, const sox_signalinfo_t *signal, char **temp,
                                 char **error)
{
    const char *name = r->name;
    sox_encodinginfo_t encoding = r->out.encoding, t = encoding;
    if (!t.encoding) t.encoding = r->inputs[0].ft->encoding.encoding;
    if (!t.bits_per_sample) t.bits_per_sample = r->inputs[0].ft->encoding.bits_per_sample;
    const char *type = r->out_type;
    /* The input's encoding, if the output type allows it. */
    if (sox_format_supports_encoding(r->out.path, type, &t)) encoding = t;
    const char *path = r->out.path;
    if (!(r->out_handler->flags & SOX_FILE_DEVICE)) {
        size_t size = strlen(path) + 8;
        *temp = cmdr_realloc(NULL, size, 1);
        snprintf(*temp, size, "%s.XXXXXX", path);
        int fd = mkstemp(*temp);
        if (fd < 0) {
            cmdr_error(error, "%s: %s: %s", name, *temp, strerror(errno));
            free(*temp), *temp = NULL;
            return NULL;
        }
        fchmod(fd, 0644); /* mkstemp creates 0600. */
        close(fd);
        path = *temp;
    }
    sox_format_t *out = sox_open_write(path, signal, &encoding, type, NULL, NULL);
    if (!out) failed_on(error, name, "cannot write", r->out.path);
    return out;
}

/* Build the chain as sox.c's add_effects: the combiner, user effects up to
 * dither, then rate, channels, and dither if the output needs them, then the
 * rest, then the output. */
static int build(run *r, sox_effects_chain_t *chain, sox_format_t *out)
{
    sox_signalinfo_t signal = r->combined;
    const sox_signalinfo_t *target = &out->signal;
    sox_effect_t *input = sox_create_effect(&combiner_handler);
    ((combiner *)input->priv)->r = r;
    if (add(chain, input, &signal, target) != SOX_SUCCESS) return 1;
    size_t i = 0;
    for (; i < r->effect_count && strcmp(r->effects[i].name, "dither"); ++i) {
        sox_effect_t *e = r->effects[i].effect;
        r->effects[i].effect = NULL;
        if (add(chain, e, &signal, target) != SOX_SUCCESS) return 1;
    }
    if (signal.channels < target->channels && signal.rate != target->rate
        && auto_effect(chain, "rate", &signal, target) != SOX_SUCCESS)
        return 1;
    if (signal.channels != target->channels
        && auto_effect(chain, "channels", &signal, target) != SOX_SUCCESS)
        return 1;
    if (signal.rate != target->rate && auto_effect(chain, "rate", &signal, target) != SOX_SUCCESS)
        return 1;
    if (i == r->effect_count && !r->no_dither && signal.precision > target->precision
        && target->precision < 24 && auto_effect(chain, "dither", &signal, target) != SOX_SUCCESS)
        return 1;
    for (; i < r->effect_count; ++i) {
        sox_effect_t *e = r->effects[i].effect;
        r->effects[i].effect = NULL;
        if (add(chain, e, &signal, target) != SOX_SUCCESS) return 1;
    }
    sox_effect_t *output = sox_create_effect(sox_find_effect("output"));
    char *args[] = {(char *)out};
    if (sox_effect_options(output, 1, args) != SOX_SUCCESS) {
        discard(output);
        return 1;
    }
    return add(chain, output, &signal, target) != SOX_SUCCESS;
}

/* The output signal, as sox.c's calculate_output_signal_parameters: the
 * options given, else the last effect that sets rate or channels, else the
 * combined signal. */
static sox_signalinfo_t output_signal(const run *r)
{
    sox_signalinfo_t s = r->out.signal;
    for (size_t i = r->effect_count; i-- > 0 && !s.rate;)
        s.rate = r->effects[i].effect->out_signal.rate;
    for (size_t i = r->effect_count; i-- > 0 && !s.channels;)
        s.channels = r->effects[i].effect->out_signal.channels;
    if (!s.rate) s.rate = r->combined.rate;
    if (!s.channels) s.channels = r->combined.channels;
    s.precision = r->combined.precision;
    int known = r->combined.length != SOX_UNKNOWN_LEN;
    for (size_t i = 0; i < r->effect_count; ++i)
        known = known && !(r->effects[i].effect->handler.flags & SOX_EFF_LENGTH);
    s.length = known ? (sox_uint64_t)((double)(r->combined.length / r->combined.channels)
                                          * s.rate / r->combined.rate + .5) * s.channels
                     : 0;
    return s;
}

static int execute(run *r, char **error)
{
    const char *name = r->name;
    int status = 1;
    char *temp = NULL;
    sox_effects_chain_t *chain = NULL;
    sox_format_t *out = NULL;

    /* As sox, mixing scales each input so the sum cannot clip, unless any
     * input has -v. */
    int scaled = 0;
    for (size_t i = 0; i < r->input_count; ++i) scaled |= r->inputs[i].volume != HUGE_VAL;
    for (size_t i = 0; i < r->input_count; ++i) {
        file_spec *f = &r->inputs[i];
        if (!scaled && r->method == MIX) f->volume = 1.0 / (double)r->input_count;
        if (!scaled && r->method == MIX_POWER) f->volume = 1.0 / sqrt((double)r->input_count);
        f->ft = sox_open_read(f->path, &f->signal, &f->encoding, f->type);
        if (!f->ft) {
            failed_on(error, name, "cannot read", f->path);
            goto done;
        }
    }
    for (size_t i = 0; i < r->effect_count; ++i) {
        effect_spec *e = &r->effects[i];
        sox_effect_t *effp = sox_create_effect(find_effect(e->name));
        e->effect = effp;
        if (effp->handler.flags & SOX_EFF_INTERNAL) {
            cmdr_error(error, "%s: %s is internal to libsox", name, e->name);
            goto done;
        }
        if (effp->handler.flags & SOX_EFF_DEPRECATED)
            fprintf(stderr, "snd: warning: effect %s is deprecated\n", e->name);
        if (sox_effect_options(effp, (int)e->argc, e->argv) != SOX_SUCCESS) {
            fail(error, name, e->name);
            goto done;
        }
    }
    if (combine_signal(r, error)) goto done;
    sox_signalinfo_t osignal = output_signal(r);
    if (!(out = open_output(r, &osignal, &temp, error))) goto done;
    chain = sox_create_effects_chain(&r->inputs[0].ft->encoding, &out->encoding);
    if (build(r, chain, out)) {
        fail(error, name, "cannot build the effects chain");
        goto done;
    }

    interrupted = 0;
    struct sigaction action = {.sa_handler = on_interrupt}, previous;
    sigemptyset(&action.sa_mask);
    sigaction(SIGINT, &action, &previous);
    /* SOX_EOF from the flow is normal when an effect such as trim ends the
     * audio early, so failures are the streams' errors or a logged failure. */
    sox_flow_effects(chain, on_flow, NULL);
    sigaction(SIGINT, &previous, NULL);
    const sox_format_t *bad = out->sox_errno ? out : NULL;
    for (size_t k = 0; !bad && k < r->input_count; ++k)
        if (r->inputs[k].ft->sox_errno) bad = r->inputs[k].ft;
    if (interrupted)
        cmdr_error(error, "%s: interrupted", name);
    else if (bad)
        failed_on(error, name, bad->sox_errstr, bad->filename);
    else if (message[0])
        fail(error, name, "processing failed");
    else
        status = 0;

done:
    if (chain) sox_delete_effects_chain(chain);
    if (out && sox_close(out) != SOX_SUCCESS && !status) status = fail(error, name, "close failed");
    for (size_t k = 0; k < r->input_count; ++k)
        if (r->inputs[k].ft) sox_close(r->inputs[k].ft);
    for (size_t k = 0; k < r->effect_count; ++k)
        if (r->effects[k].effect) discard(r->effects[k].effect);
    if (temp) {
        if (!status && rename(temp, r->out.path))
            status = cmdr_error(error, "%s: %s: %s", name, r->out.path, strerror(errno));
        if (status) unlink(temp);
        free(temp);
    }
    return status;
}

static void free_run(run *r)
{
    for (size_t i = 0; i < r->effect_count; ++i) {
        for (size_t j = 0; j < r->effects[i].argc; ++j) free(r->effects[i].argv[j]);
        free(r->effects[i].argv);
    }
    free(r->effects);
    free(r->inputs);
}

/* FILE's contents as text, with any NUL bytes as spaces. */
static cmdr_value slurp(FILE *file)
{
    fflush(file);
    long size = ftell(file);
    size_t n = size > 0 ? (size_t)size : 0;
    rewind(file);
    char *text = cmdr_realloc(NULL, n, 1);
    n = fread(text, 1, n, file);
    for (size_t i = 0; i < n; ++i)
        if (!text[i]) text[i] = ' ';
    cmdr_value v = cmdr_text(text, n);
    free(text);
    return v;
}

/* snd-run and snd-report: OUT, then sox's arguments after its output file. */
static int run_or_report(const char *name, int report, size_t argc, const cmdr_value *argv,
                         cmdr_value *result, char **error)
{
    if (argc < 1)
        return cmdr_error(error, "usage: %s OUT [[OPTION ...] IN ...] [EFFECT [ARG ...] ...]",
                          name);
    const char **out = NULL, **words = NULL;
    size_t out_count = 0, count = 0;
    run r = {.name = name};
    int status = flatten(name, &argv[0], &out, &out_count, error);
    for (size_t i = 1; i < argc && !status; ++i)
        status = flatten(name, &argv[i], &words, &count, error);
    if (!status) status = enter(name, error);
    if (status) {
        free(out), free(words);
        return status;
    }
    status = parse(&r, out, out_count, words, count, error);
    FILE *capture = NULL;
    int saved = -1;
    if (!status && report) {
        /* stat and stats write to stderr; capture it for the result. */
        fflush(stderr);
        if (!(capture = tmpfile()) || (saved = dup(STDERR_FILENO)) < 0
            || dup2(fileno(capture), STDERR_FILENO) < 0) {
            status = cmdr_error(error, "%s: cannot capture stderr: %s", name, strerror(errno));
            if (saved >= 0) close(saved), saved = -1;
        }
    }
    if (!status) status = execute(&r, error);
    if (saved >= 0) {
        fflush(stderr);
        dup2(saved, STDERR_FILENO);
        close(saved);
    }
    if (!status) *result = report ? slurp(capture) : cmdr_text(r.out.path, strlen(r.out.path));
    if (capture) fclose(capture);
    leave();
    free_run(&r);
    free(out), free(words);
    return status;
}

COMMAND(run_command) { return run_or_report("snd-run", 0, argc, argv, result, error); }
COMMAND(report_command) { return run_or_report("snd-report", 1, argc, argv, result, error); }

static void push_line(cmdr_value *list, const char *name, const char *value)
{
    size_t size = strlen(name) + strlen(value) + 2;
    char *line = cmdr_realloc(NULL, size, 1);
    snprintf(line, size, "%s %s", name, value);
    cmdr_push(list, cmdr_text(line, size - 1));
    free(line);
}

static void push_number(cmdr_value *list, const char *name, double x)
{
    cmdr_value v = cmdr_number_value(x);
    push_line(list, name, v.text);
    cmdr_free(&v);
}

/* Open PATH for reading, or fail; the caller holds the lock. */
static sox_format_t *open_input(const char *name, const char *path, char **error)
{
    sox_format_t *ft = sox_open_read(path, NULL, NULL, NULL);
    if (!ft) failed_on(error, name, "cannot read", path);
    return ft;
}

COMMAND(info_command)
{
    if (argc != 1) return cmdr_error(error, "usage: snd-info PATH");
    if (enter("snd-info", error)) return 1;
    sox_format_t *ft = open_input("snd-info", argv[0].text, error);
    if (!ft) {
        leave();
        return 1;
    }
    const sox_signalinfo_t *s = &ft->signal;
    *result = (cmdr_value){.kind = CMDR_LIST};
    push_line(result, "type", ft->filetype);
    push_number(result, "rate", s->rate);
    push_number(result, "channels", s->channels);
    if (s->length && s->length != SOX_UNKNOWN_LEN) {
        push_number(result, "frames", (double)(s->length / s->channels));
        push_number(result, "seconds", (double)(s->length / s->channels) / s->rate);
    }
    push_number(result, "bits", ft->encoding.bits_per_sample);
    push_line(result, "encoding", sox_encodings_info[ft->encoding.encoding].desc);
    sox_close(ft);
    leave();
    return 0;
}

/* Fixed-point seconds, since sox's time arguments reject exponents. */
static void push_segment(cmdr_value *list, sox_uint64_t start, sox_uint64_t end, double rate)
{
    char line[64];
    snprintf(line, sizeof(line), "%.6f %.6f", (double)start / rate, (double)(end - start) / rate);
    cmdr_push(list, cmdr_text(line, strlen(line)));
}

/* snd-segments PATH DB SECONDS: the sounding parts of PATH, split where it
 * stays at or below DB dBFS for at least SECONDS. */
COMMAND(segments_command)
{
    if (argc != 3) return cmdr_error(error, "usage: snd-segments PATH DB SECONDS");
    double db, seconds;
    if (cmdr_number(argv[1].text, &db, error) || cmdr_number(argv[2].text, &seconds, error))
        return 1;
    if (seconds < 0) return cmdr_error(error, "snd-segments: SECONDS must be nonnegative");
    if (enter("snd-segments", error)) return 1;
    sox_format_t *ft = open_input("snd-segments", argv[0].text, error);
    if (!ft) {
        leave();
        return 1;
    }
    unsigned ch = ft->signal.channels;
    double rate = ft->signal.rate, threshold = pow(10, db / 20) * SOX_SAMPLE_MAX;
    sox_uint64_t gap = (sox_uint64_t)(seconds * rate + .5), frame = 0, start = 0, last = 0;
    int sounding = 0;
    size_t size = sox_globals.bufsiz / ch * ch, n;
    sox_sample_t *buf = cmdr_realloc(NULL, size, sizeof(*buf));
    *result = (cmdr_value){.kind = CMDR_LIST};
    while ((n = sox_read(ft, buf, size)) > 0) {
        for (size_t i = 0; i + ch <= n; i += ch, ++frame) {
            double peak = 0;
            for (unsigned c = 0; c < ch; ++c) peak = fmax(peak, fabs((double)buf[i + c]));
            if (peak > threshold) {
                if (!sounding) start = frame, sounding = 1;
                last = frame;
            } else if (sounding && frame - last >= gap) {
                push_segment(result, start, last + 1, rate);
                sounding = 0;
            }
        }
    }
    if (sounding) push_segment(result, start, last + 1, rate);
    int status = 0;
    if (ft->sox_errno) {
        cmdr_free(result);
        status = fail(error, "snd-segments", ft->sox_errstr);
    }
    free(buf);
    sox_close(ft);
    leave();
    return status;
}

COMMAND(effects_command)
{
    if (argc) return cmdr_error(error, "usage: snd-effects");
    if (enter("snd-effects", error)) return 1;
    *result = (cmdr_value){.kind = CMDR_LIST};
    for (const sox_effect_fn_t *fn = sox_get_effect_fns(); *fn; ++fn) {
        const sox_effect_handler_t *h = (*fn)();
        if (h && h->name && !(h->flags & SOX_EFF_INTERNAL))
            cmdr_push(result, cmdr_text(h->name, strlen(h->name)));
    }
    cmdr_push(result, cmdr_text(varispeed_handler.name, strlen(varispeed_handler.name)));
    leave();
    return 0;
}

COMMAND(usage_command)
{
    if (argc != 1) return cmdr_error(error, "usage: snd-usage EFFECT");
    if (enter("snd-usage", error)) return 1;
    const sox_effect_handler_t *h = find_effect(argv[0].text);
    int status = 0;
    if (!h || (h->flags & SOX_EFF_INTERNAL)) {
        status = cmdr_error(error, "snd-usage: unknown effect: %s", argv[0].text);
    } else {
        const char *usage = h->usage ? h->usage : "";
        size_t size = strlen(h->name) + strlen(usage) + 2;
        char *text = cmdr_realloc(NULL, size, 1);
        snprintf(text, size, "%s %s", h->name, usage);
        *result = cmdr_text(text, size - 1);
        free(text);
    }
    leave();
    return status;
}

#define TEXT(n, u, d, f) {.name = n, .usage = u, .docstring = d, .run = f, .text = 1}
static const cmdr_command commands[] = {
    {.name = "snd-run", .usage = "snd-run OUT [[OPTION ...] IN ...] [EFFECT [ARG ...] ...]",
     .docstring = "Run sox's arguments, writing OUT; return OUT", .run = run_command},
    {.name = "snd-report", .usage = "snd-report OUT [[OPTION ...] IN ...] [EFFECT [ARG ...] ...]",
     .docstring = "As snd-run, but return what the effects print, as stats does",
     .run = report_command},
    TEXT("snd-info", "snd-info PATH", "List type, rate, channels, frames, seconds, bits, encoding",
         info_command),
    TEXT("snd-segments", "snd-segments PATH DB SECONDS",
         "List START LENGTH of each part between silences at or below DB dBFS", segments_command),
    TEXT("snd-effects", "snd-effects", "List the effects: libsox's, and varispeed", effects_command),
    TEXT("snd-usage", "snd-usage EFFECT", "Return EFFECT's name and arguments", usage_command),
};
const cmdr cmdr_ext_snd = {commands, sizeof(commands) / sizeof(commands[0]), NULL};
