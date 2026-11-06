#pragma once

#include "midiseer/LengthMap.h"
#include "midiseer/Seer.h"

#include <juce_audio_basics/juce_audio_basics.h>

#include <atomic>
#include <memory>
#include <mutex>
#include <optional>
#include <vector>

namespace midiseer
{

// The learned note lengths, shared between the audio thread, which learns them, looks them up
// and forgets the ones a pass no longer plays, and the rest of the plugin, which shows, saves, loads and clears them.
//
// The audio thread owns its map and never waits. What it learns comes back through a queue to
// a copy kept here for the editor and the saved project. A cleared or loaded map is handed to
// the audio thread at its next block; each hand-over has a new number, so notes learned into a
// map that has since been replaced are not mixed into the new one.
class LearnedMap
{
public:
    LearnedMap();
    ~LearnedMap();

    // --- Audio thread ---------------------------------------------------------------------

    // The map to use for this block (a replaced one is picked up here).
    LengthMap& beginBlock();

    // A note the engine just learned into that map, or forgot from it.
    void changed (const MapChange& change);

    // --- Any other thread -----------------------------------------------------------------

    // Takes in what the audio thread has learned. True if anything changed since the last call.
    bool sync();

    // Every learned note, sorted by position.
    std::vector<LearnedNote> notes();
    std::size_t size();

    void replace (const std::vector<LearnedNote>& newNotes);
    void clear() { replace ({}); }

    // For the saved project.
    juce::String toText();
    void fromText (const juce::String& text);

private:
    struct Numbered
    {
        LengthMap map;
        std::uint32_t generation = 0;
    };

    struct Queued
    {
        MapChange change;
        std::uint32_t generation = 0;
    };

    // The audio thread's map, and the next one waiting for it (or the old one, after a swap).
    std::unique_ptr<Numbered> audio;
    std::unique_ptr<Numbered> pending;
    juce::SpinLock pendingLock;
    std::atomic<bool> hasPending { false };

    static constexpr int queueSize = 4096;
    juce::AbstractFifo queue { queueSize };
    std::vector<Queued> queued = std::vector<Queued> (queueSize);

    std::mutex copyLock; // the copy below, and reading the queue
    LengthMap copy;
    std::uint32_t copyGeneration = 0;
    bool copyChanged = false;
};

// The notes of a MIDI file as learned lengths, placed from the song start (or the loop start)
// and wrapped to the loop if loopBeats > 0. Nothing if the file can't be read or has no notes.
std::optional<std::vector<LearnedNote>> readMidiFile (const juce::File& file, double loopBeats);

} // namespace midiseer
