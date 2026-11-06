#pragma once

#include "midiseer/LengthMap.h"

#include <tng_ui/tng_ui.h>

#include <vector>

namespace midiseer
{

// The learned notes as a small piano roll: the whole loop when there is one, otherwise the
// stretch of song around the playhead. Shows where a dropped MIDI file can go.
class LearnedView final : public juce::Component
{
public:
    void setNotes (std::vector<LearnedNote> newNotes);

    // loopBeats > 0: show the loop. Otherwise pages of `pageBars` bars that follow the playhead.
    void setTiming (double loopBeats, double beatsPerBar);
    void setPlayhead (double songBeat); // -1 while stopped

    void setDropHighlight (bool on);

    void paint (juce::Graphics& g) override;

    // The bars shown, for the heading ("bars 5 to 8"), or empty for a loop.
    juce::String describeWindow() const;

    static constexpr int pageBars = 4;

private:
    double windowStart() const; // in the map's positions, beats
    double windowLength() const;

    std::vector<LearnedNote> notes;
    int lowPitch = 60, highPitch = 72;
    double loop = 0.0, barBeats = 4.0;
    double playhead = -1.0;
    double page = 0.0; // the page start while stopped, so it stays put
    bool dropHighlight = false;
};

} // namespace midiseer
