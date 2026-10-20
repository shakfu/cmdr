#include "smf.h"

#include <libremidi/reader.hpp>
#include <libremidi/writer.hpp>

#include <cstdlib>
#include <cstring>
#include <sstream>
#include <string>

// Variable-length quantity, as SMF delta times are written.
static std::string vlq(unsigned long n)
{
    std::string out(1, static_cast<char>(n & 0x7F));
    while (n >>= 7) out.insert(out.begin(), static_cast<char>(0x80 | (n & 0x7F)));
    return out;
}

extern "C" int smf_encode(const smf_event *events, size_t count, unsigned tpq, long end,
                          unsigned char **data, size_t *size)
try {
    libremidi::writer writer;
    writer.ticksPerQuarterNote = static_cast<uint16_t>(tpq);
    // add_track would make two on an empty writer: format 1, not 0.
    writer.tracks.resize(1);
    // The writer takes each event's tick as its delta from the one before.
    long previous = 0;
    for (size_t i = 0; i < count; ++i) {
        const smf_event &e = events[i];
        libremidi::message m;
        m.bytes.assign(e.bytes, e.bytes + e.size);
        writer.add_event(static_cast<int>(e.tick - previous), 0, m);
        previous = e.tick;
    }
    std::ostringstream out;
    writer.write(out);
    std::string bytes = out.str();
    // The writer ends the track at the last event, with 00 FF 2F 00, unless
    // that event's bytes end in FF 2F, as a tempo of 0x??FF2F does. Either
    // way, the track should end at END.
    const std::string eot("\xFF\x2F\x00", 3);
    if (bytes.size() >= 26 && bytes.compare(bytes.size() - 4, 4, std::string(1, '\0') + eot) == 0)
        bytes.resize(bytes.size() - 4);
    bytes += vlq(static_cast<unsigned long>(end - previous)) + eot;
    // One track: its length is at bytes 18 to 21, after MThd's 14 and "MTrk".
    const unsigned long length = bytes.size() - 22;
    for (int i = 0; i < 4; ++i) bytes[18 + i] = static_cast<char>(length >> (24 - 8 * i));
    *size = bytes.size();
    *data = static_cast<unsigned char *>(std::malloc(bytes.size()));
    if (!*data) return -1;
    std::memcpy(*data, bytes.data(), bytes.size());
    return 0;
} catch (...) {
    return -1;
}

static bool wanted(const libremidi::message &m)
{
    if (m.empty()) return false;
    if (m.is_meta_event())
        return m.size() == 6 && m.get_meta_event_type() == libremidi::meta_event_type::TEMPO_CHANGE;
    return m.bytes[0] >= 0x80 && m.bytes[0] < 0xF0 && m.size() <= 3;
}

extern "C" int smf_decode(const unsigned char *data, size_t size, smf_event **events,
                          size_t *count, unsigned *tpq, long *end)
try {
    libremidi::reader reader(true);
    const auto status = reader.parse(data, size);
    if (status != libremidi::reader::complete && status != libremidi::reader::validated)
        return -1;
    if (reader.ticksPerBeat < 1) return -1;
    size_t n = 0;
    long last = 0;
    for (const auto &track : reader.tracks)
        for (const auto &e : track) {
            if (e.tick > last) last = e.tick;
            if (wanted(e.m)) ++n;
        }
    smf_event *out = static_cast<smf_event *>(std::malloc((n ? n : 1) * sizeof(smf_event)));
    if (!out) return -1;
    size_t i = 0;
    for (const auto &track : reader.tracks)
        for (const auto &e : track)
            if (wanted(e.m)) {
                out[i] = smf_event{e.tick, static_cast<unsigned char>(e.m.size()), {}};
                std::memcpy(out[i].bytes, e.m.bytes.data(), e.m.size());
                ++i;
            }
    *events = out;
    *count = n;
    *tpq = static_cast<unsigned>(reader.ticksPerBeat);
    *end = last;
    return 0;
} catch (...) {
    return -1;
}
