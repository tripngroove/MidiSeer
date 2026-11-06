#pragma once

#include "PluginProcessor.h"
#include "ui/LearnedView.h"
#include "ui/RampView.h"

#include <tng_ui/tng_ui.h>

namespace midiseer
{

// One screen: the settings along the top, the ramp's shape, and the notes learned so far.
class MidiSeerEditor final : public juce::AudioProcessorEditor,
                             public juce::FileDragAndDropTarget,
                             private juce::Timer,
                             private juce::ChangeListener
{
public:
    explicit MidiSeerEditor (MidiSeerProcessor& p);
    ~MidiSeerEditor() override;

    void paint (juce::Graphics& g) override;
    void resized() override;

    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void fileDragEnter (const juce::StringArray& files, int x, int y) override;
    void fileDragExit (const juce::StringArray& files) override;
    void filesDropped (const juce::StringArray& files, int x, int y) override;

private:
    void timerCallback() override;
    void changeListenerCallback (juce::ChangeBroadcaster*) override; // the learned notes changed

    // A setting's value box, kept in step with its host parameter.
    void bind (tng::ValueBox& box, const juce::String& parameterId, int step = 1);
    void setParameter (const juce::String& parameterId, int value);
    int parameterValue (const juce::String& parameterId) const;

    void loadFile (const juce::File& file);
    void chooseFile();
    void forgetClicked();
    void showNotice (const juce::String& text); // for a few seconds, in the heading

    juce::Rectangle<int> titleArea() const;
    juce::Rectangle<int> headingArea() const;

    MidiSeerProcessor& seer;

    tng::theme::LookAndFeel lookAndFeel; // before the components that use it, so it outlives them
    juce::TooltipWindow tooltips { this, 500 };
    tng::ValueBox ccBox { "CC" }, startBox { "Start" }, endBox { "End" }, curveBox { "Curve" }, loopBox { "Loop" }, mpeBox { "MPE" };
    std::vector<std::pair<tng::ValueBox*, juce::String>> boxes; // and their parameter IDs
    std::vector<int> steps;

    RampView ramp;
    LearnedView learnedView;
    juce::TextButton loadButton { "Load MIDI file" }, forgetButton { "Forget all" };
    std::unique_ptr<juce::FileChooser> chooser;

    std::size_t learnedCount = 0;
    juce::String notice;
    juce::uint32 noticeUntil = 0, forgetArmedUntil = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MidiSeerEditor)
};

} // namespace midiseer
