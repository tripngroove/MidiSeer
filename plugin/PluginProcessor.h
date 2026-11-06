#pragma once

#include "LearnedMap.h"
#include "midiseer/Seer.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <atomic>

namespace midiseer
{

// Passes MIDI through and adds a CC ramp for each note (see Seer). Everything musical happens
// in the engine; this wraps it for hosts: parameters, timing, saving, and the learned map.
class MidiSeerProcessor final : public juce::AudioProcessor, private juce::Timer
{
public:
    MidiSeerProcessor();
    ~MidiSeerProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;

    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return true; }
    bool isMidiEffect() const override { return JucePlugin_IsMidiEffect; }
    bool supportsMPE() const override { return true; } // notes on many channels, both ways
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    // --- For the editor, on the message thread ---------------------------------------------

    static constexpr int maxLoopBars = 64;

    juce::AudioProcessorValueTreeState parameters;

    LearnedMap& learnedMap() { return learned; }

    // Loads a MIDI file's note lengths in place of everything learned. False if it has none.
    bool loadMidiFile (const juce::File& file);

    // Fires on the message thread when the learned notes change.
    juce::ChangeBroadcaster learnedChanged;

    RampStatus getRampStatus() const;

    // Where the song is, in beats, or -1 while it is stopped.
    double getSongPosition() const { return songPosition.load (std::memory_order_relaxed); }

    // The loop length in beats (0: none), in the host's current time signature.
    double getLoopBeats() const;
    double getBeatsPerBar() const { return beatsPerBar.load (std::memory_order_relaxed); }

private:
    static BusesProperties makeBuses();
    static juce::AudioProcessorValueTreeState::ParameterLayout makeLayout();
    void timerCallback() override; // takes in learned notes

    Settings readSettings() const;

    LearnedMap learned;
    Seer seer;
    Output engineOut;
    juce::MidiBuffer output;

    std::atomic<float>* ccNumber = nullptr;
    std::atomic<float>* startValue = nullptr;
    std::atomic<float>* endValue = nullptr;
    std::atomic<float>* curve = nullptr;
    std::atomic<float>* loopBars = nullptr;
    std::atomic<float>* mpe = nullptr;

    // From the audio thread, for the editor.
    std::atomic<double> songPosition { -1.0 };
    std::atomic<double> beatsPerBar { 4.0 };
    std::atomic<bool> rampActive { false }, rampMoving { false }, rampKnown { false };
    std::atomic<double> rampProgress { 0.0 };
    std::atomic<int> rampValue { 0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MidiSeerProcessor)
};

} // namespace midiseer
