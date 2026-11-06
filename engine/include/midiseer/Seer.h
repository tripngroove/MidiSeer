#pragma once

#include "midiseer/LengthMap.h"

#include <array>
#include <cstdint>
#include <vector>

namespace midiseer
{

// What the ramps do. Every field can change at any time, mid-ramp included.
struct Settings
{
    int ccNumber = 1;
    int startValue = 0; // sent with each note on
    int endValue = 127; // reached as the note ends, then held
    double curve = 0.0; // -1 to 1: 0 is a straight line, above 0 starts slowly, below 0 starts fast
    double loopBeats = 0.0; // > 0: positions repeat every this many beats (launcher clips)
    bool mpe = false; // a ramp per note, each on its own channel
};

// A short MIDI channel message at a sample of the current block.
struct MidiEvent
{
    int sample = 0;
    std::uint8_t status = 0;
    std::uint8_t data1 = 0;
    std::uint8_t data2 = 0;

    int type() const { return status & 0xf0; }
    int channel() const { return (status & 0x0f) + 1; } // 1 to 16
    bool operator== (const MidiEvent&) const = default;
};

// What the processor sends on: the incoming messages and the ramps' CCs, in time order, and
// the notes learned (for the editor's copy of the map and the saved project).
struct Output
{
    std::vector<MidiEvent> events;
    std::vector<LearnedNote> learned;

    void clear()
    {
        events.clear();
        learned.clear();
    }
};

// The timing of one block, from the host.
struct BlockInfo
{
    int numSamples = 0;
    double samplesPerBeat = 24000.0;
    bool playing = false; // the song plays, and songBeat says where
    double songBeat = 0.0; // the song position at the block's first sample, in beats
};

// For the editor: the ramp that started last.
struct RampStatus
{
    bool active = false; // a ramp has run since the plugin started
    bool moving = false; // still on its way to the end value
    bool known = false; // its length came from the map rather than a guess
    double progress = 0.0; // 0 to 1 along the ramp
    int value = 0; // the last value sent
};

// Where a song position (in beats) is in the map: in ticks, wrapped to the loop if
// loopBeats > 0.
std::int64_t positionKey (double songBeat, double loopBeats);

// MidiSeer's engine: passes notes on and sends a CC ramp for each one, from the start value at
// note on to the end value at note off.
//
// Hosts don't say how long a note will be when it starts, so the engine learns it: each note
// off stores the note's length under its song position and pitch (LengthMap), and the next time
// the song plays that note the ramp knows exactly when to arrive. A note it has not heard yet
// ramps over the length of the last note that ended, and if it ends sooner it glides to the end
// value in glideMs.
//
// MPE off: one ramp for every note, sent on the channel of the note it follows. The newest note
// takes over, and notes that start together follow the longest one.
// MPE on: a ramp per channel. Notes on channel 1 get a channel of their own from 2 to 16 (so a
// plain clip comes out as MPE); notes already on channels 2 to 16 keep theirs.
//
// It runs on the audio thread: it never allocates once Output has room for a block's events.
class Seer
{
public:
    static constexpr double minGapMs = 4.0; // a channel's CC goes out at most this often
    static constexpr double glideMs = 10.0;
    static constexpr double firstGuessBeats = 1.0; // before any note has ended

    void prepare (double sampleRate);

    void setSettings (const Settings& newSettings) { settings = newSettings; }
    const Settings& getSettings() const { return settings; }

    // The map to learn into and look lengths up in, or nullptr for none. The engine does not own it.
    void setMap (LengthMap* newMap) { map = newMap; }

    // Each block: begin, then every incoming channel message in time order, then end.
    void beginBlock (const BlockInfo& info);
    void handle (const MidiEvent& in, Output& out);
    void endBlock (Output& out);

    // Where a song position is in the map, with the current loop length.
    std::int64_t positionKey (double songBeat) const { return midiseer::positionKey (songBeat, settings.loopBeats); }

    RampStatus status() const { return lastStatus; }

private:
    enum class Phase
    {
        idle,
        ramping,
        gliding, // the note ended early: a quick slide to the end value
        holding, // at the end value
    };

    // One ramp, on one channel.
    struct Voice
    {
        Phase phase = Phase::idle;
        int channel = 1; // the channel its CCs go out on
        int ownerChannel = 0, ownerPitch = -1; // the incoming note it follows
        double startBeat = 0.0, lengthBeats = 1.0;
        bool known = false;
        double glideFrom = 0.0;
        std::int64_t glideStart = 0;
        int lastValue = -1;
        std::int64_t nextCheck = 0;
        int heldNotes = 0; // notes held on its channel (MPE)
        std::int64_t lastUsed = 0; // when a note last started or ended on it (MPE)
    };

    // An incoming note being held.
    struct Held
    {
        bool on = false;
        bool positioned = false; // the song was playing when it started
        double onBeat = 0.0; // on the engine's own beat clock
        double songBeat = 0.0;
        std::uint8_t outChannel = 1;
        std::uint8_t voice = 0;
    };

    void noteOn (std::int64_t t, const MidiEvent& in, Output& out);
    void noteOff (std::int64_t t, const MidiEvent& in, Output& out);
    void allNotesOff (std::int64_t t, const MidiEvent& in, Output& out);
    int memberChannelFor (int inChannel) const;
    void release (Voice& v, std::int64_t t, Output& out);

    // Sends every voice's CCs up to `t`.
    void advanceTo (std::int64_t t, Output& out);
    void render (Voice& v, std::int64_t from, std::int64_t to, Output& out);
    double valueAt (const Voice& v, std::int64_t t) const;
    void send (Voice& v, std::int64_t t, int value, Output& out);

    double beatAt (std::int64_t t) const { return blockStartBeat + static_cast<double> (t - blockStartSample) / block.samplesPerBeat; }
    std::int64_t sampleAtBeat (double beat) const;
    std::int64_t sampleOf (int offset) const { return blockStartSample + offset; }
    Held& heldNote (int channel, int pitch) { return held[static_cast<size_t> ((channel - 1) * 128 + pitch)]; }

    Settings settings;
    LengthMap* map = nullptr;

    std::array<Voice, 16> voices {};
    std::array<Held, 16 * 128> held {};
    int newestVoice = -1;
    double guessBeats = firstGuessBeats;

    // Clocks: samples since prepare, and beats on our own clock (it runs with the transport
    // stopped too, at the host's tempo).
    BlockInfo block;
    std::int64_t blockStartSample = 0;
    double blockStartBeat = 0.0;
    std::int64_t renderedTo = 0;
    std::int64_t gapSamples = 192, glideSamples = 480;

    RampStatus lastStatus;
};

// The ramp's shape: how far along the value is (0 to 1) when `p` of the note has passed.
double rampShape (double p, double curve);

} // namespace midiseer
