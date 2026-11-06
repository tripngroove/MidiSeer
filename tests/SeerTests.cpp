#include "midiseer/Seer.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <vector>

using namespace midiseer;

namespace
{
// 120 bpm at 48 kHz: a beat is 24000 samples. Blocks of 480 samples (10 ms).
constexpr double sampleRate = 48000.0;
constexpr double samplesPerBeat = 24000.0;
constexpr int blockSize = 480;

struct Sent
{
    std::int64_t time; // samples since the start
    MidiEvent event;
};

// Plays notes through the engine like a host would and collects what comes out.
struct Rig
{
    Seer seer;
    LengthMap map;
    Output out;
    std::int64_t now = 0;
    double songBeat = 0.0;
    bool playing = true;
    std::vector<Sent> sent;

    explicit Rig (Settings settings = {})
    {
        seer.prepare (sampleRate);
        seer.setSettings (settings);
        seer.setMap (&map);
    }

    // Runs until `until` (samples since the start), feeding `events` at their times.
    void run (std::int64_t until, std::vector<std::pair<std::int64_t, MidiEvent>> events = {})
    {
        size_t next = 0;
        while (now < until)
        {
            out.clear();
            seer.beginBlock ({ blockSize, samplesPerBeat, playing, songBeat });
            for (; next < events.size() && events[next].first < now + blockSize; ++next)
            {
                auto e = events[next].second;
                e.sample = static_cast<int> (events[next].first - now);
                seer.handle (e, out);
            }
            seer.endBlock (out);
            for (const auto& e : out.events)
                sent.push_back ({ now + e.sample, e });
            now += blockSize;
            if (playing)
                songBeat += blockSize / samplesPerBeat;
        }
    }

    std::vector<Sent> controllers (int channel = 0) const
    {
        std::vector<Sent> result;
        for (const auto& s : sent)
            if (s.event.type() == 0xb0 && (channel == 0 || s.event.channel() == channel))
                result.push_back (s);
        return result;
    }

    // The last CC value sent at or before `time`.
    int valueAt (std::int64_t time, int channel = 0) const
    {
        int value = -1;
        for (const auto& s : controllers (channel))
            if (s.time <= time)
                value = s.event.data2;
        return value;
    }
};

MidiEvent on (int pitch, int channel = 1) { return { 0, static_cast<std::uint8_t> (0x90 | (channel - 1)), static_cast<std::uint8_t> (pitch), 100 }; }
MidiEvent off (int pitch, int channel = 1) { return { 0, static_cast<std::uint8_t> (0x80 | (channel - 1)), static_cast<std::uint8_t> (pitch), 0 }; }

std::int64_t beats (double b) { return static_cast<std::int64_t> (b * samplesPerBeat); }
} // namespace

TEST_CASE ("the ramp's shape")
{
    CHECK (rampShape (0.0, 0.0) == 0.0);
    CHECK (rampShape (1.0, 0.0) == 1.0);
    CHECK (rampShape (0.5, 0.0) == Catch::Approx (0.5));
    CHECK (rampShape (0.5, 0.5) < 0.5); // starts slowly
    CHECK (rampShape (0.5, -0.5) > 0.5); // starts fast
    CHECK (rampShape (1.0, 0.7) == Catch::Approx (1.0));
    CHECK (rampShape (2.0, 0.0) == 1.0);
}

TEST_CASE ("the start value goes out just before the note")
{
    Rig rig;
    rig.run (beats (0.5), { { 100, on (60) } });

    REQUIRE (rig.sent.size() >= 2);
    CHECK (rig.sent[0].time == 100);
    CHECK (rig.sent[0].event == MidiEvent { 100 % blockSize, 0xb0, 1, 0 });
    CHECK (rig.sent[1].event.type() == 0x90);
    CHECK (rig.sent[1].time == 100);
}

TEST_CASE ("a note heard for the first time ramps over the last note's length, then learns its own")
{
    Rig rig;
    // Nothing has ended yet, so the guess is one beat; the note lasts two.
    rig.run (beats (3), { { 0, on (60) }, { beats (2), off (60) } });

    CHECK (rig.valueAt (beats (0.5)) == Catch::Approx (64).margin (2));
    CHECK (rig.valueAt (beats (1) + 10) == 127); // arrived at one beat
    CHECK (rig.controllers().back().time == beats (1));

    REQUIRE (rig.map.size() == 1);
    CHECK (rig.map.all()[0] == LearnedNote { 0, 2 * ticksPerBeat, 60 });
}

TEST_CASE ("the second time through, the ramp lands as the note ends")
{
    Rig rig;
    rig.map.learn (0, 60, 2 * ticksPerBeat);
    rig.run (beats (3), { { 0, on (60) }, { beats (2), off (60) } });

    CHECK (rig.valueAt (beats (1)) == Catch::Approx (64).margin (2));
    CHECK (rig.valueAt (beats (2) - 200) < 127);
    CHECK (rig.valueAt (beats (2)) == 127);
}

TEST_CASE ("a note that ends before its ramp glides to the end value")
{
    Rig rig;
    rig.map.learn (0, 60, 4 * ticksPerBeat); // it used to be longer
    rig.run (beats (2), { { 0, on (60) }, { beats (1), off (60) } });

    CHECK (rig.valueAt (beats (1)) < 64);
    const auto glide = static_cast<std::int64_t> (sampleRate * Seer::glideMs / 1000.0);
    CHECK (rig.valueAt (beats (1) + glide) == 127);
    CHECK (rig.controllers().back().time <= beats (1) + glide);
    CHECK (rig.map.find (0, 60) == ticksPerBeat); // and it learned the new length
}

TEST_CASE ("CCs go out only when the value changes, and not too often")
{
    SECTION ("a slow ramp sends few messages")
    {
        Settings s;
        s.endValue = 2;
        Rig rig (s);
        rig.map.learn (0, 60, 2 * ticksPerBeat);
        rig.run (beats (3), { { 0, on (60) }, { beats (2), off (60) } });
        CHECK (rig.controllers().size() == 3); // 0, 1, 2
    }

    SECTION ("a fast ramp is thinned out")
    {
        Rig rig;
        rig.map.learn (0, 60, ticksPerBeat / 8); // 62.5 ms for 127 steps
        rig.run (beats (1), { { 0, on (60) }, { beats (0.125), off (60) } });

        const auto ccs = rig.controllers();
        const auto gap = static_cast<std::int64_t> (sampleRate * Seer::minGapMs / 1000.0);
        CHECK (ccs.size() <= 62.5 / Seer::minGapMs + 2);
        for (size_t i = 1; i + 1 < ccs.size(); ++i) // all but the landing on the end value
            CHECK (ccs[i].time - ccs[i - 1].time >= gap);
        CHECK (ccs.back().event.data2 == 127);
    }
}

TEST_CASE ("notes that start together follow the longest; a later note takes over")
{
    Rig rig;
    rig.map.learn (0, 60, ticksPerBeat);
    rig.map.learn (0, 64, 4 * ticksPerBeat);
    rig.map.learn (0, 67, 2 * ticksPerBeat);
    rig.run (beats (5), { { 0, on (60) }, { 0, on (64) }, { 0, on (67) }, { beats (1), off (60) }, { beats (2), off (67) }, { beats (4), off (64) } });

    CHECK (rig.controllers().front().event.data2 == 0);
    CHECK (rig.valueAt (beats (2)) == Catch::Approx (64).margin (2)); // still going: the 4-beat note leads
    CHECK (rig.valueAt (beats (4)) == 127);
    int starts = 0;
    for (const auto& c : rig.controllers())
        starts += c.event.data2 == 0 ? 1 : 0;
    CHECK (starts == 1);

    SECTION ("a note starting later restarts the ramp")
    {
        rig.run (beats (8), { { beats (6), on (60) }, { beats (6.5), on (62) } });
        CHECK (rig.valueAt (beats (6.5)) == 0);
    }
}

TEST_CASE ("positions repeat with the loop length")
{
    Settings s;
    s.loopBeats = 16.0;
    Rig rig (s);
    rig.songBeat = 17.0;
    rig.run (beats (2), { { 0, on (60) }, { beats (1.5), off (60) } });

    CHECK (rig.map.find (ticksPerBeat, 60) == ticksPerBeat * 3 / 2);
    CHECK (rig.seer.positionKey (33.0) == ticksPerBeat);
    CHECK (rig.seer.positionKey (16.0 - 0.000001) == 0);
}

TEST_CASE ("notes played with the song stopped are not learned, but set the guess")
{
    Rig rig;
    rig.playing = false;
    rig.run (beats (2), { { 0, on (60) }, { beats (0.5), off (60) } });
    CHECK (rig.map.empty());

    rig.sent.clear();
    rig.run (beats (4), { { beats (2), on (62) } });
    CHECK (rig.valueAt (beats (2.5)) == 127); // ramped over half a beat
    CHECK (rig.valueAt (beats (2.25)) == Catch::Approx (64).margin (2));
}

TEST_CASE ("with MPE on, every note gets its own channel and ramp")
{
    Settings s;
    s.mpe = true;
    s.ccNumber = 74;
    Rig rig (s);
    rig.map.learn (0, 60, ticksPerBeat);
    rig.map.learn (0, 64, 2 * ticksPerBeat);
    rig.run (beats (3), { { 0, on (60) }, { 0, on (64) }, { beats (1), off (60) }, { beats (2), off (64) } });

    std::vector<int> noteChannels, offChannels;
    for (const auto& s : rig.sent)
    {
        if (s.event.type() == 0x90)
            noteChannels.push_back (s.event.channel());
        if (s.event.type() == 0x80)
            offChannels.push_back (s.event.channel());
    }
    REQUIRE (noteChannels.size() == 2);
    CHECK (noteChannels[0] != noteChannels[1]);
    CHECK (noteChannels[0] >= 2);
    CHECK (offChannels == noteChannels);

    // Each lands at its own note's end.
    CHECK (rig.valueAt (beats (1), noteChannels[0]) == 127);
    CHECK (rig.valueAt (beats (1), noteChannels[1]) == Catch::Approx (64).margin (2));
    CHECK (rig.valueAt (beats (2), noteChannels[1]) == 127);
    for (const auto& c : rig.controllers())
        CHECK (c.event.data1 == 74);

    SECTION ("a free channel is reused last, so a note's release isn't cut by the next")
    {
        rig.run (beats (4), { { beats (3), on (67) } });
        int lastOn = 0;
        for (const auto& s : rig.sent)
            if (s.event.type() == 0x90)
                lastOn = s.event.channel();
        CHECK (lastOn != noteChannels[0]);
        CHECK (lastOn != noteChannels[1]);
    }

    SECTION ("all notes off ends notes on the channels they moved to")
    {
        rig.run (beats (4), { { beats (3), on (67) }, { beats (3.5), { 0, 0xb0, 123, 0 } } });
        int offs = 0;
        for (const auto& s : rig.sent)
            if (s.time >= beats (3.5) && s.event.type() == 0x80)
                ++offs;
        CHECK (offs == 1);
    }
}

TEST_CASE ("with MPE on, notes already on their own channels keep them")
{
    Settings s;
    s.mpe = true;
    Rig rig (s);
    rig.run (beats (1), { { 0, on (60, 5) }, { 0, on (64, 9) } });

    REQUIRE (rig.sent.size() >= 4);
    std::vector<int> channels;
    for (size_t i = 0; i < 4; ++i)
        channels.push_back (rig.sent[i].event.channel());
    CHECK (channels == std::vector<int> { 5, 5, 9, 9 }); // the start CC, then the note, for each
    for (const auto& s : rig.controllers())
        CHECK ((s.event.channel() == 5 || s.event.channel() == 9));
}

TEST_CASE ("other messages pass straight through")
{
    Rig rig;
    rig.run (beats (0.1), { { 10, { 0, 0xe0, 0, 64 } }, { 20, { 0, 0xb0, 7, 100 } } });
    REQUIRE (rig.sent.size() == 2);
    CHECK (rig.sent[0].event.status == 0xe0);
    CHECK (rig.sent[1].event == MidiEvent { 20, 0xb0, 7, 100 });
}
