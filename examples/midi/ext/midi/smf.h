#ifndef CMDR_MIDI_SMF_H
#define CMDR_MIDI_SMF_H

/* Standard MIDI File encoding and decoding on libremidi's writer and reader,
 * which are C++. File access stays in midi.c. */

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* A channel message, or a meta event as FF TYPE LENGTH DATA, at an absolute
 * tick. */
typedef struct {
    long tick;
    unsigned char size;
    unsigned char bytes[6];
} smf_event;

/* Encode EVENTS, sorted by tick, as a format 0 file with TPQ ticks per quarter
 * note, ending its track at END, at or after the last event. Sets *data,
 * malloc-owned. Returns 0, or -1 if libremidi failed. */
int smf_encode(const smf_event *events, size_t count, unsigned tpq, long end,
               unsigned char **data, size_t *size);

/* Decode a file's channel messages and tempo events from every track, in file
 * order within each track. Sets *events, malloc-owned, *tpq, and *end, the
 * tick of the last event of any kind. Returns 0, or -1 if the data is not a
 * whole MIDI file timed in ticks per beat. */
int smf_decode(const unsigned char *data, size_t size, smf_event **events, size_t *count,
               unsigned *tpq, long *end);

#ifdef __cplusplus
}
#endif

#endif
