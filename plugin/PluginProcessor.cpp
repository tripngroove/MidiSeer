#include "PluginProcessor.h"

#include "PluginEditor.h"

namespace midiseer
{

namespace
{
const juce::Identifier stateType { "MidiSeerState" };
constexpr int stateVersion = 1;

juce::String describeLoop (int bars)
{
    if (bars == 0)
        return "Off";
    return juce::String (bars) + (bars == 1 ? " bar" : " bars");
}
} // namespace

juce::AudioProcessor::BusesProperties MidiSeerProcessor::makeBuses()
{
    // A MIDI effect has no audio. The VST3/CLAP build loads as an instrument, and hosts
    // expect an instrument to have an audio output, so it gets a silent stereo one.
    if constexpr (JucePlugin_IsMidiEffect)
        return {};
    else
        return BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true);
}

juce::AudioProcessorValueTreeState::ParameterLayout MidiSeerProcessor::makeLayout()
{
    using Int = juce::AudioParameterInt;
    using Attributes = juce::AudioParameterIntAttributes;

    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    layout.add (std::make_unique<Int> (juce::ParameterID { "cc", 1 }, "CC Number", 0, 127, 1));
    layout.add (std::make_unique<Int> (juce::ParameterID { "start", 1 }, "Start Value", 0, 127, 0));
    layout.add (std::make_unique<Int> (juce::ParameterID { "end", 1 }, "End Value", 0, 127, 127));
    layout.add (std::make_unique<Int> (juce::ParameterID { "curve", 1 }, "Curve", -100, 100, 0,
                                       Attributes()
                                           .withLabel ("%")
                                           .withStringFromValueFunction ([] (int v, int) { return (v > 0 ? "+" : "") + juce::String (v) + "%"; })
                                           .withValueFromStringFunction ([] (const juce::String& text) { return text.getIntValue(); })));
    layout.add (std::make_unique<Int> (juce::ParameterID { "loop", 1 }, "Loop Length", 0, maxLoopBars, 0,
                                       Attributes()
                                           .withStringFromValueFunction ([] (int v, int) { return describeLoop (v); })
                                           .withValueFromStringFunction ([] (const juce::String& text) {
                                               return text.trim().equalsIgnoreCase ("off") ? 0 : text.getIntValue();
                                           })));
    layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { "mpe", 1 }, "MPE", false));
    return layout;
}

MidiSeerProcessor::MidiSeerProcessor()
    : AudioProcessor (makeBuses()), parameters (*this, nullptr, stateType, makeLayout())
{
    ccNumber = parameters.getRawParameterValue ("cc");
    startValue = parameters.getRawParameterValue ("start");
    endValue = parameters.getRawParameterValue ("end");
    curve = parameters.getRawParameterValue ("curve");
    loopBars = parameters.getRawParameterValue ("loop");
    mpe = parameters.getRawParameterValue ("mpe");

    engineOut.events.reserve (4096);
    engineOut.learned.reserve (256);
    startTimerHz (30);
}

MidiSeerProcessor::~MidiSeerProcessor()
{
    stopTimer();
}

bool MidiSeerProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    if constexpr (JucePlugin_IsMidiEffect)
        return true;
    else
        return layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
}

void MidiSeerProcessor::prepareToPlay (double sampleRate, int)
{
    seer.prepare (sampleRate);
    output.ensureSize (8192);
}

double MidiSeerProcessor::getLoopBeats() const
{
    return std::round (loopBars->load()) * beatsPerBar.load (std::memory_order_relaxed);
}

Settings MidiSeerProcessor::readSettings() const
{
    Settings s;
    s.ccNumber = juce::roundToInt (ccNumber->load());
    s.startValue = juce::roundToInt (startValue->load());
    s.endValue = juce::roundToInt (endValue->load());
    s.curve = curve->load() / 100.0;
    s.loopBeats = getLoopBeats();
    s.mpe = mpe->load() >= 0.5f;
    return s;
}

void MidiSeerProcessor::processBlock (juce::AudioBuffer<float>& audio, juce::MidiBuffer& midi)
{
    const juce::ScopedNoDenormals noDenormals;
    audio.clear();

    // Where the song is.
    double bpm = 120.0;
    bool playing = false;
    double ppq = 0.0;
    if (auto* ph = getPlayHead())
        if (const auto position = ph->getPosition())
        {
            bpm = position->getBpm().orFallback (120.0);
            if (const auto signature = position->getTimeSignature(); signature.hasValue() && signature->numerator > 0 && signature->denominator > 0)
                beatsPerBar.store (signature->numerator * 4.0 / signature->denominator, std::memory_order_relaxed);
            if (const auto songPpq = position->getPpqPosition(); songPpq.hasValue() && position->getIsPlaying())
            {
                playing = true;
                ppq = *songPpq;
            }
        }

    seer.setMap (&learned.beginBlock());
    seer.setSettings (readSettings());
    seer.beginBlock ({ audio.getNumSamples(), getSampleRate() * 60.0 / juce::jmax (1.0, bpm), playing, ppq });

    // Channel messages go through the engine, in order; anything else (SysEx) passes on as it is.
    engineOut.clear();
    output.clear();
    for (const auto metadata : midi)
    {
        const auto* data = metadata.data;
        if (metadata.numBytes >= 1 && metadata.numBytes <= 3 && data[0] >= 0x80 && data[0] < 0xf0)
            seer.handle ({ metadata.samplePosition, data[0], metadata.numBytes > 1 ? data[1] : std::uint8_t {},
                           metadata.numBytes > 2 ? data[2] : std::uint8_t {} },
                         engineOut);
        else
            output.addEvent (data, metadata.numBytes, metadata.samplePosition);
    }
    seer.endBlock (engineOut);

    for (const auto& e : engineOut.events)
    {
        const std::uint8_t bytes[] { e.status, e.data1, e.data2 };
        output.addEvent (bytes, juce::MidiMessage::getMessageLengthFromFirstByte (e.status), e.sample);
    }
    for (const auto& note : engineOut.learned)
        learned.learned (note);

    midi.swapWith (output);

    const auto status = seer.status();
    rampActive.store (status.active, std::memory_order_relaxed);
    rampMoving.store (status.moving, std::memory_order_relaxed);
    rampKnown.store (status.known, std::memory_order_relaxed);
    rampProgress.store (status.progress, std::memory_order_relaxed);
    rampValue.store (status.value, std::memory_order_relaxed);
    songPosition.store (playing ? ppq : -1.0, std::memory_order_relaxed);
}

RampStatus MidiSeerProcessor::getRampStatus() const
{
    RampStatus s;
    s.active = rampActive.load (std::memory_order_relaxed);
    s.moving = rampMoving.load (std::memory_order_relaxed);
    s.known = rampKnown.load (std::memory_order_relaxed);
    s.progress = rampProgress.load (std::memory_order_relaxed);
    s.value = rampValue.load (std::memory_order_relaxed);
    return s;
}

void MidiSeerProcessor::timerCallback()
{
    if (learned.sync())
        learnedChanged.sendChangeMessage();
}

bool MidiSeerProcessor::loadMidiFile (const juce::File& file)
{
    const auto notes = readMidiFile (file, getLoopBeats());
    if (! notes)
        return false;
    learned.replace (*notes);
    learnedChanged.sendChangeMessage();
    return true;
}

juce::AudioProcessorEditor* MidiSeerProcessor::createEditor()
{
    return new MidiSeerEditor (*this);
}

void MidiSeerProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = parameters.copyState();
    state.setProperty ("version", stateVersion, nullptr);
    state.setProperty ("learned", learned.toText(), nullptr);
    if (const auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void MidiSeerProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    const auto xml = getXmlFromBinary (data, sizeInBytes);
    if (xml == nullptr || ! xml->hasTagName (stateType))
        return;

    auto state = juce::ValueTree::fromXml (*xml);
    learned.fromText (state.getProperty ("learned").toString());
    state.removeProperty ("learned", nullptr); // kept in the map, not twice
    parameters.replaceState (state);
    learnedChanged.sendChangeMessage();
}

} // namespace midiseer

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new midiseer::MidiSeerProcessor();
}
