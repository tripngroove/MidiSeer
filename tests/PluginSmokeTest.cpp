// Drives the real plugin processor like a host would: notes through a playing transport,
// learning, MPE, saved state and the editor.

#include "PluginProcessor.h"

#include <tng_ui/tng_ui.h>

#include <juce_audio_processors/juce_audio_processors.h>

#include <catch2/catch_test_macros.hpp>

namespace
{
constexpr double sampleRate = 48000.0;
constexpr int blockSize = 480;
constexpr double samplesPerBeat = sampleRate / 2.0; // 120 bpm

class FakePlayHead final : public juce::AudioPlayHead
{
public:
    juce::Optional<PositionInfo> getPosition() const override
    {
        PositionInfo info;
        info.setBpm (120.0);
        info.setIsPlaying (playing);
        info.setPpqPosition (ppq);
        info.setTimeSignature (TimeSignature { 4, 4 });
        return info;
    }

    bool playing = true;
    double ppq = 0.0;
};

struct Host
{
    juce::ScopedJuceInitialiser_GUI juce;
    std::unique_ptr<juce::AudioProcessor> plugin { createPluginFilter() };
    FakePlayHead playHead;
    std::vector<juce::MidiMessage> sent; // with timestamps in samples from the start
    std::int64_t now = 0;

    Host()
    {
        plugin->setPlayHead (&playHead);
        plugin->setPlayConfigDetails (0, 2, sampleRate, blockSize);
        plugin->prepareToPlay (sampleRate, blockSize);
    }

    auto& processor() { return dynamic_cast<midiseer::MidiSeerProcessor&> (*plugin); }

    void setParameter (const juce::String& id, float plain)
    {
        auto* p = processor().parameters.getParameter (id);
        REQUIRE (p != nullptr);
        p->setValueNotifyingHost (p->convertTo0to1 (plain));
    }

    // Plays from beat `from` to beat `to`, with these messages at their beats.
    void play (double from, double to, std::vector<std::pair<double, juce::MidiMessage>> input = {})
    {
        playHead.ppq = from;
        juce::AudioBuffer<float> audio (2, blockSize);
        const auto blocks = static_cast<int> ((to - from) * samplesPerBeat / blockSize);
        for (int b = 0; b < blocks; ++b)
        {
            juce::MidiBuffer midi;
            const double blockStart = playHead.ppq, blockEnd = blockStart + blockSize / samplesPerBeat;
            for (const auto& [beat, message] : input)
                if (beat >= blockStart - 1.0e-9 && beat < blockEnd - 1.0e-9)
                    midi.addEvent (message, static_cast<int> (std::llround ((beat - blockStart) * samplesPerBeat)));

            plugin->processBlock (audio, midi);
            for (const auto m : midi)
            {
                auto message = m.getMessage();
                message.setTimeStamp (static_cast<double> (now + m.samplePosition));
                sent.push_back (message);
            }
            now += blockSize;
            playHead.ppq = blockEnd;
        }
    }

    // The last value sent on this CC before `sample` (from the start of this play), or -1.
    int valueBefore (std::int64_t sample, int cc, int channel = 0) const
    {
        int value = -1;
        for (const auto& m : sent)
            if (m.isController() && m.getControllerNumber() == cc && (channel == 0 || m.getChannel() == channel) && m.getTimeStamp() < static_cast<double> (sample))
                value = m.getControllerValue();
        return value;
    }
};

std::vector<std::pair<double, juce::MidiMessage>> twoBeatNote (double at)
{
    return { { at, juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100) }, { at + 2.0, juce::MidiMessage::noteOff (1, 60) } };
}
} // namespace

TEST_CASE ("notes pass through with a CC ramp, and the second pass lands on time")
{
    Host host;

    host.play (0.0, 4.0, twoBeatNote (0.0));
    int notes = 0;
    for (const auto& m : host.sent)
        notes += m.isNoteOnOrOff() ? 1 : 0;
    CHECK (notes == 2);
    CHECK (host.sent.front().isController()); // the start value, before the note
    CHECK (host.sent.front().getControllerValue() == 0);
    CHECK (host.valueBefore (static_cast<std::int64_t> (samplesPerBeat * 1.1), 1) == 127); // guessed one beat

    host.processor().learnedMap().sync();
    CHECK (host.processor().learnedMap().size() == 1);

    // Back to the start: now it knows the note lasts two beats.
    host.sent.clear();
    const auto passStart = host.now;
    host.play (0.0, 4.0, twoBeatNote (0.0));
    const auto value = host.valueBefore (passStart + static_cast<std::int64_t> (samplesPerBeat), 1);
    CHECK (value >= 60);
    CHECK (value <= 66);
    CHECK (host.valueBefore (passStart + static_cast<std::int64_t> (samplesPerBeat * 2.1), 1) == 127);
}

TEST_CASE ("with MPE on, a plain chord comes out spread over channels")
{
    Host host;
    host.setParameter ("mpe", 1.0f);
    host.setParameter ("cc", 74.0f);
    host.play (0.0, 1.0, { { 0.0, juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100) }, { 0.0, juce::MidiMessage::noteOn (1, 64, (juce::uint8) 100) } });

    std::vector<int> channels;
    for (const auto& m : host.sent)
        if (m.isNoteOn())
            channels.push_back (m.getChannel());
    REQUIRE (channels.size() == 2);
    CHECK (channels[0] >= 2);
    CHECK (channels[1] >= 2);
    CHECK (channels[0] != channels[1]);
    CHECK (host.valueBefore (blockSize * 10, 74, channels[0]) > 0);
    CHECK (host.valueBefore (blockSize * 10, 74, channels[1]) > 0);
}

TEST_CASE ("learned notes and settings are saved with the project")
{
    juce::MemoryBlock saved;
    {
        Host host;
        host.setParameter ("end", 100.0f);
        host.play (0.0, 4.0, twoBeatNote (0.0));
        host.plugin->getStateInformation (saved);
    }

    Host host;
    host.plugin->setStateInformation (saved.getData(), static_cast<int> (saved.getSize()));
    const auto notes = host.processor().learnedMap().notes();
    REQUIRE (notes.size() == 1);
    CHECK (notes[0] == midiseer::LearnedNote { 0, 2 * midiseer::ticksPerBeat, 60 });

    // The loaded map reaches the audio thread: the first pass already lands on time.
    host.play (0.0, 4.0, twoBeatNote (0.0));
    const auto value = host.valueBefore (static_cast<std::int64_t> (samplesPerBeat), 1);
    CHECK (value >= 47);
    CHECK (value <= 53); // about half of 100
}

TEST_CASE ("a MIDI file pre-loads the note lengths")
{
    Host host;

    juce::MidiMessageSequence track;
    track.addEvent (juce::MidiMessage::noteOn (1, 62, (juce::uint8) 100), 960.0);
    track.addEvent (juce::MidiMessage::noteOff (1, 62), 960.0 + 480.0);
    track.updateMatchedPairs();
    juce::MidiFile file;
    file.setTicksPerQuarterNote (960);
    file.addTrack (track);

    const auto path = juce::File::createTempFile (".mid");
    {
        juce::FileOutputStream out (path);
        REQUIRE (file.writeTo (out));
    }
    CHECK (host.processor().loadMidiFile (path));
    path.deleteFile();

    const auto notes = host.processor().learnedMap().notes();
    REQUIRE (notes.size() == 1);
    CHECK (notes[0] == midiseer::LearnedNote { 960, 480, 62 });

    CHECK_FALSE (host.processor().loadMidiFile (juce::File::createTempFile (".mid"))); // empty: nothing
}

TEST_CASE ("the editor opens")
{
    Host host;

    // Set MIDISEER_STATE to a saved-state .xml file to open the editor on that state.
    if (const auto statePath = juce::SystemStats::getEnvironmentVariable ("MIDISEER_STATE", {}); statePath.isNotEmpty())
    {
        if (const auto xml = juce::parseXML (juce::File (statePath)))
        {
            juce::MemoryBlock state;
            juce::AudioProcessor::copyXmlToBinary (*xml, state);
            host.plugin->setStateInformation (state.getData(), static_cast<int> (state.getSize()));
        }
    }
    else
    {
        // Something to show: a few learned notes, the song part-way through one.
        host.play (0.0, 6.0, { { 0.0, juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100) },
                               { 2.0, juce::MidiMessage::noteOff (1, 60) },
                               { 2.0, juce::MidiMessage::noteOn (1, 64, (juce::uint8) 100) },
                               { 3.0, juce::MidiMessage::noteOff (1, 64) },
                               { 3.0, juce::MidiMessage::noteOn (1, 67, (juce::uint8) 100) },
                               { 5.5, juce::MidiMessage::noteOff (1, 67) } });
        host.play (0.0, 1.0, { { 0.0, juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100) } });
    }

    std::unique_ptr<juce::AudioProcessorEditor> editor (host.plugin->createEditorAndMakeActive());
    REQUIRE (editor != nullptr);
    CHECK (editor->getWidth() > 0);

    // Set MIDISEER_SNAPSHOT to a .png path to save a picture of the editor.
    if (const auto path = juce::SystemStats::getEnvironmentVariable ("MIDISEER_SNAPSHOT", {}); path.isNotEmpty())
    {
        const auto image = editor->createComponentSnapshot (editor->getLocalBounds(), true, 2.0f);
        const juce::File file (path);
        file.deleteFile();
        juce::FileOutputStream out (file);
        juce::PNGImageFormat().writeImageToStream (image, out);
    }

    host.plugin->editorBeingDeleted (editor.get());
}

TEST_CASE ("a CC number is typed in, not picked from a list")
{
    Host host;
    std::unique_ptr<juce::AudioProcessorEditor> editor (host.plugin->createEditorAndMakeActive());

    tng::ValueBox* cc = nullptr;
    for (auto* child : editor->getChildren())
        if (auto* box = dynamic_cast<tng::ValueBox*> (child); box != nullptr && box->getName() == "CC")
            cc = box;
    REQUIRE (cc != nullptr);

    // A click on the value opens a field.
    const juce::Point<float> at { static_cast<float> (cc->getWidth() - 6), cc->getHeight() / 2.0f };
    const auto now = juce::Time::getCurrentTime();
    const juce::MouseEvent click (juce::Desktop::getInstance().getMainMouseSource(), at, {}, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, cc, cc, now, at, now, 1, false);
    cc->mouseDown (click);
    cc->mouseUp (click);

    juce::TextEditor* field = nullptr;
    for (auto* child : cc->getChildren())
        if (auto* f = dynamic_cast<juce::TextEditor*> (child); f != nullptr && f->isVisible())
            field = f;
    REQUIRE (field != nullptr);

    // Typing 74 and Return sets the parameter.
    field->setText ("74");
    field->onReturnKey(); // what Return does (JUCE delivers the key press itself asynchronously)
    CHECK_FALSE (field->isVisible());
    CHECK (juce::roundToInt (host.processor().parameters.getRawParameterValue ("cc")->load()) == 74);

    host.plugin->editorBeingDeleted (editor.get());
}
