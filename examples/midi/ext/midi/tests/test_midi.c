/* Error messages, then midi.play into a virtual input port of this process.
 * Exits 77, which CTest reports as skipped, where no MIDI service runs. */
#define _POSIX_C_SOURCE 200809L
#include "cmdr.h"
#include "ext_check.h"

#include <libremidi/libremidi-c.h>

#include <pthread.h>
#include <time.h>

extern const cmdr cmdr_ext_midi;

#define PORT "cmdr-midi-test"

static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static unsigned char received[64][3];
static size_t count;

static void on_message(void *ctx, libremidi_timestamp t, const libremidi_midi1_symbol *m,
                       size_t n)
{
    (void)ctx, (void)t;
    pthread_mutex_lock(&lock);
    if (n <= 3 && count < 64) memcpy(received[count++], m, n); /* zero-padded */
    pthread_mutex_unlock(&lock);
}

static size_t received_count(void)
{
    pthread_mutex_lock(&lock);
    size_t n = count;
    pthread_mutex_unlock(&lock);
    return n;
}

/* Wait up to a second for N messages. */
static void await(size_t n)
{
    for (int i = 0; i < 100 && received_count() < n; ++i)
        nanosleep(&(struct timespec){0, 10000000}, NULL);
}

static void *interrupt_later(void *data)
{
    (void)data;
    nanosleep(&(struct timespec){0, 200000000}, NULL);
    cmdr_interrupt(1);
    return NULL;
}

int main(void)
{
    const cmdr *table = ext_commands("midi", &cmdr_ext_midi);
    expect(table, "midi.key 60", "60");
    expect(table, "midi.key C4", "60");
    expect(table, "midi.key c-1", "0");
    expect(table, "midi.key G9", "127");
    expect(table, "midi.key Bb3", "58");
    expect(table, "midi.key E#4", "65");
    expect_error(table, "midi.key G#9", 1, "midi.key: G#9 is not 0 to 127");
    expect_error(table, "midi.key 128", 1, "midi.key: 128 is not 0 to 127");
    expect_error(table, "midi.key H4", 1, "midi.key: H4 is not");
    expect_error(table, "midi.key C", 1, "midi.key: C is not");
    expect_error(table, "midi.write 'note 0 1 60 100' x.mid 120", 1,
                 "midi.write: item 1: want note START LENGTH KEY VELOCITY CHANNEL");
    expect_error(table, "midi.write 'cc 0 7' x.mid 120", 1,
                 "midi.write: item 1: want cc START CONTROLLER VALUE CHANNEL");
    expect_error(table, "midi.write '0 1 60 100 1' x.mid 120", 1,
                 "midi.write: item 1: want note, rest, cc, program, bend, or tempo, not 0");
    expect_error(table, "list 'note 0 1 60 100 1' 'note -1 1 60 100 1' | midi.write x.mid 120",
                 1, "midi.write: item 2: START -1 is not a number of beats");
    expect_error(table, "midi.write 'note 0 1 60 0 1' x.mid 120", 1,
                 "midi.write: item 1: VELOCITY 0 is not 1 to 127");
    expect_error(table, "midi.write 'note 0 1 60 100 17' x.mid 120", 1,
                 "midi.write: item 1: CHANNEL 17 is not 1 to 16");
    expect_error(table, "midi.write 'note 0 1 60.5 100 1' x.mid 120", 1,
                 "midi.write: item 1: KEY 60.5 is not 0 to 127");
    expect_error(table, "midi.write 'cc 0 128 1 1' x.mid 120", 1,
                 "midi.write: item 1: CONTROLLER 128 is not 0 to 127");
    expect_error(table, "midi.write 'program 0 5 0' x.mid 120", 1,
                 "midi.write: item 1: CHANNEL 0 is not 1 to 16");
    expect_error(table, "midi.write 'bend 0 8192 1' x.mid 120", 1,
                 "midi.write: item 1: VALUE 8192 is not -8192 to 8191");
    expect_error(table, "midi.write 'tempo 0 1' x.mid 120", 1,
                 "midi.write: item 1: BPM 1 is not 3.6 to 60000000");
    expect_error(table, "midi.write 'rest 0 -1' x.mid 120", 1,
                 "midi.write: item 1: LENGTH -1 is not a number of beats");
    expect_error(table, "midi.write 'note 0 1 60 100 1' x.mid 0", 1,
                 "midi.write: BPM 0 is not 3.6 to 60000000");
    expect_error(table, "midi.write 'rest 1e6 1' x.mid 120", 1,
                 "midi.write: the score ends at beat 1e+06, past what a file holds");
    expect_error(table, "midi.write 'note 0 1 60 100 1' /nonexistent/x.mid 120", 1,
                 "midi.write: cannot write /nonexistent/x.mid");
    expect_error(table, "midi.read /nonexistent.mid", 1, "midi.read: cannot read");
    expect_error(table, "midi.read /dev/null", 1, "midi.read: /dev/null is not a MIDI file");
    expect_error(table, "midi.bjorklund 5 4", 1, "midi.bjorklund: K 5 is not 0 to 4");
    expect_error(table, "midi.bjorklund 1 0", 1, "midi.bjorklund: N 0 is not 1 to 4096");
    expect_error(table, "midi.play 'note 0 1 60 100 1' 120 'no such port'", 1,
                 "midi.play: no output port named no such port");

    libremidi_midi_configuration conf;
    libremidi_midi_configuration_init(&conf);
    conf.version = MIDI1;
    conf.on_midi1_message.callback = on_message;
    conf.virtual_port = true;
    conf.port_name = PORT;
    libremidi_api_configuration api;
    libremidi_midi_api_configuration_init(&api);
    api.configuration_type = Input;
    libremidi_midi_in_handle *in;
    if (libremidi_midi_in_new(&conf, &api, &in)) {
        puts("test_midi: no MIDI service; skipping midi.play");
        return 77;
    }

    /* At 600 BPM, 0.1 s per beat; the tempo doubles at beat 1. Channel state
     * goes before notes at equal times, and the rest holds the end. */
    struct timespec t0, t1;
    clock_gettime(CLOCK_MONOTONIC, &t0);
    expect(table,
           "list 'note 0 1 C4 100 1' 'program 0 5 2' 'cc 0 7 90 2' 'note 0.5 0.5 64 90 2'"
           " 'tempo 1 1200' 'bend 1 -8192 2' 'rest 1 4' | midi.play 600 " PORT " | count",
           "7");
    clock_gettime(CLOCK_MONOTONIC, &t1);
    double took = (double)(t1.tv_sec - t0.tv_sec) + (double)(t1.tv_nsec - t0.tv_nsec) / 1e9;
    /* 0.1 s for the first beat, then 4 beats at 0.05 s. */
    CHECK(took > 0.29 && took < 0.5);
    await(7);
    CHECK(received_count() == 7);
    static const unsigned char want[7][3] = {
        {0xC1, 5, 0}, {0xB1, 7, 90}, {0x90, 60, 100}, {0x91, 64, 90},
        {0xE1, 0, 0}, {0x80, 60, 0}, {0x81, 64, 0}};
    CHECK(!memcmp(received, want, sizeof(want)));

    /* Interrupted while a note sounds: it is released, and play fails. */
    count = 0;
    pthread_t thread;
    CHECK(!pthread_create(&thread, NULL, interrupt_later, NULL));
    expect_error(table, "midi.play 'note 0 100 62 100 3' 600 " PORT, 1, "midi.play: interrupted");
    pthread_join(thread, NULL);
    cmdr_interrupt(0);
    await(2);
    CHECK(received_count() == 2);
    static const unsigned char released[2][3] = {{0x92, 62, 100}, {0x82, 62, 0}};
    CHECK(!memcmp(received, released, sizeof(released)));

    libremidi_midi_in_free(in);
    puts("test_midi: ok");
    return 0;
}
