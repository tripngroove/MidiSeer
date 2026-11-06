#include "PluginEditor.h"

namespace midiseer
{

namespace theme = tng::theme;

namespace
{
const juce::Colour accent = theme::accentColour (0);
constexpr int topBarHeight = 48;
constexpr int dividerHeight = 20; // the light grey lines between the top bar's items
constexpr int rampHeight = 120;
constexpr int headingHeight = 24;
const juce::String title { "MidiSeer" };

juce::Font titleFont() { return theme::font (22.0f, true); }
juce::Font headingFont() { return theme::font (15.0f, true); }
juce::Font noteFont() { return theme::font (14.0f); }

bool isMidiFile (const juce::String& path)
{
    return path.endsWithIgnoreCase (".mid") || path.endsWithIgnoreCase (".midi");
}

// The default CC with MPE off and on: the mod wheel, and MPE's timbre ("slide") control.
constexpr int plainDefaultCc = 1, mpeDefaultCc = 74;
} // namespace

MidiSeerEditor::MidiSeerEditor (MidiSeerProcessor& p) : AudioProcessorEditor (p), seer (p)
{
    setLookAndFeel (&lookAndFeel);

    ccBox.setRange (0, 127);
    startBox.setRange (0, 127);
    endBox.setRange (0, 127);
    curveBox.setRange (-100, 100);
    curveBox.toText = [] (int v) { return (v > 0 ? "+" : "") + juce::String (v) + "%"; };
    loopBox.setRange (0, MidiSeerProcessor::maxLoopBars);
    loopBox.toText = [] (int v) { return v == 0 ? juce::String ("Off") : juce::String (v) + (v == 1 ? " bar" : " bars"); };
    loopBox.fromText = [] (const juce::String& text) -> std::optional<int> {
        if (text.trim().equalsIgnoreCase ("off"))
            return 0;
        if (! text.containsAnyOf ("0123456789"))
            return std::nullopt;
        return text.getIntValue();
    };

    // Numbers are typed or dragged; only MPE's Off/On is picked from a list.
    for (auto* box : { &ccBox, &startBox, &endBox, &curveBox, &loopBox })
        box->setEntry (tng::ValueBox::Entry::typed);
    mpeBox.setRange (0, 1);
    mpeBox.toText = [] (int v) { return v != 0 ? juce::String ("On") : juce::String ("Off"); };

    bind (ccBox, "cc");
    bind (startBox, "start");
    bind (endBox, "end");
    bind (curveBox, "curve");
    bind (loopBox, "loop");
    bind (mpeBox, "mpe");

    // Turning MPE on moves an untouched CC to MPE's timbre control, and back.
    mpeBox.onChange = [this] (int on) {
        setParameter ("mpe", on);
        const int cc = parameterValue ("cc");
        if (on != 0 && cc == plainDefaultCc)
            setParameter ("cc", mpeDefaultCc);
        else if (on == 0 && cc == mpeDefaultCc)
            setParameter ("cc", plainDefaultCc);
    };

    addAndMakeVisible (ramp);
    addAndMakeVisible (learnedView);

    for (auto* b : { &loadButton, &forgetButton })
    {
        b->setColour (juce::TextButton::buttonColourId, theme::panel);
        b->setColour (juce::TextButton::textColourOffId, theme::ink);
        addAndMakeVisible (b);
    }
    loadButton.setTooltip ("Load a clip's note lengths from a MIDI file (or drop the file on the notes below)");
    forgetButton.setTooltip ("Forget every learned note");
    loadButton.onClick = [this] { chooseFile(); };
    forgetButton.onClick = [this] { forgetClicked(); };

    seer.learnedChanged.addChangeListener (this);
    changeListenerCallback (nullptr);
    timerCallback();

    setResizable (true, true);
    setResizeLimits (640, 360, 1600, 1000);
    setSize (720, 400);
    startTimerHz (30);
}

MidiSeerEditor::~MidiSeerEditor()
{
    seer.learnedChanged.removeChangeListener (this);
    setLookAndFeel (nullptr);
}

void MidiSeerEditor::bind (tng::ValueBox& box, const juce::String& parameterId)
{
    box.setAccent (accent);
    box.onChange = [this, parameterId] (int v) { setParameter (parameterId, v); };
    boxes.emplace_back (&box, parameterId);
    addAndMakeVisible (box);
}

void MidiSeerEditor::setParameter (const juce::String& parameterId, int value)
{
    if (auto* p = seer.parameters.getParameter (parameterId))
    {
        p->beginChangeGesture();
        p->setValueNotifyingHost (p->convertTo0to1 (static_cast<float> (value)));
        p->endChangeGesture();
    }
}

int MidiSeerEditor::parameterValue (const juce::String& parameterId) const
{
    if (const auto* p = seer.parameters.getParameter (parameterId))
        return juce::roundToInt (p->convertFrom0to1 (p->getValue()));
    return 0;
}

juce::Rectangle<int> MidiSeerEditor::titleArea() const
{
    return { theme::spacing, 0, theme::textWidth (titleFont(), title), topBarHeight };
}

juce::Rectangle<int> MidiSeerEditor::headingArea() const
{
    return { theme::spacing, ramp.getBottom() + 16, getWidth() - 2 * theme::spacing, headingHeight };
}

void MidiSeerEditor::paint (juce::Graphics& g)
{
    g.fillAll (theme::background);

    g.setColour (theme::ink);
    g.setFont (titleFont());
    g.drawText (title, titleArea(), juce::Justification::centredLeft);

    // Light dividers between the title and each setting.
    g.setColour (theme::separator);
    const int dividerY = (topBarHeight - dividerHeight) / 2;
    g.fillRect (titleArea().getRight() + theme::spacing, dividerY, 1, dividerHeight);
    for (size_t i = 0; i + 1 < boxes.size(); ++i)
        g.fillRect (boxes[i].first->getRight() + theme::spacing, dividerY, 1, dividerHeight);

    // "Learned notes", how many, and which bars show.
    const auto heading = headingArea().withRight (loadButton.getX() - theme::spacing);
    g.setColour (theme::ink);
    g.setFont (headingFont());
    const juce::String name ("Learned notes");
    g.drawText (name, heading, juce::Justification::centredLeft);

    juce::String detail;
    if (notice.isNotEmpty())
        detail = notice;
    else
    {
        detail = learnedCount == 1 ? juce::String ("1 note") : juce::String (static_cast<int> (learnedCount)) + " notes";
        const double loopBeats = seer.getLoopBeats();
        if (loopBeats > 0.0)
            detail << ", in a " << loopBox.toText (parameterValue ("loop")) << " loop";
        else
            detail << ", showing " << learnedView.describeWindow();
    }
    g.setColour (notice.isNotEmpty() ? theme::danger : theme::inkMuted);
    g.setFont (noteFont());
    g.drawText (detail, heading.withTrimmedLeft (theme::textWidth (headingFont(), name) + theme::spacing), juce::Justification::centredLeft);
}

void MidiSeerEditor::resized()
{
    // Top bar: the title, then each setting, divided by light lines.
    int x = titleArea().getRight() + 2 * theme::spacing + 1;
    for (auto& [box, id] : boxes)
    {
        juce::ignoreUnused (id);
        const int width = box->getPreferredWidth();
        box->setBounds (x, (topBarHeight - 24) / 2, width, 24);
        x += width + 2 * theme::spacing + 1;
    }

    auto area = getLocalBounds().reduced (theme::spacing, 0).withTrimmedTop (topBarHeight).withTrimmedBottom (theme::spacing);
    ramp.setBounds (area.removeFromTop (rampHeight));
    area.removeFromTop (16);

    auto heading = area.removeFromTop (headingHeight);
    const auto buttonWidth = [] (const juce::TextButton& b) { return theme::textWidth (theme::font (15.0f), b.getButtonText()) + 2 * theme::padding; };
    forgetButton.setBounds (heading.removeFromRight (buttonWidth (forgetButton)));
    heading.removeFromRight (theme::spacing);
    loadButton.setBounds (heading.removeFromRight (buttonWidth (loadButton)));

    area.removeFromTop (theme::spacing);
    learnedView.setBounds (area);
}

void MidiSeerEditor::timerCallback()
{
    // Follow the host's parameters (automation, undo, project load).
    bool widthsChanged = false;
    for (auto& [box, id] : boxes)
    {
        const int before = box->getPreferredWidth();
        box->setValue (parameterValue (id));
        widthsChanged |= box->getPreferredWidth() != before;
    }
    if (widthsChanged)
        resized();

    ramp.setShape (parameterValue ("start"), parameterValue ("end"), parameterValue ("curve") / 100.0);
    ramp.setStatus (seer.getRampStatus());

    const auto window = learnedView.describeWindow();
    learnedView.setTiming (seer.getLoopBeats(), seer.getBeatsPerBar());
    learnedView.setPlayhead (seer.getSongPosition());
    if (learnedView.describeWindow() != window)
        repaint (headingArea());

    const auto now = juce::Time::getMillisecondCounter();
    if (notice.isNotEmpty() && now > noticeUntil)
    {
        notice.clear();
        repaint (headingArea());
    }
    if (forgetArmedUntil != 0 && now > forgetArmedUntil)
    {
        forgetArmedUntil = 0;
        forgetButton.setButtonText ("Forget all");
        resized();
    }
}

void MidiSeerEditor::changeListenerCallback (juce::ChangeBroadcaster*)
{
    auto notes = seer.learnedMap().notes();
    learnedCount = notes.size();
    learnedView.setNotes (std::move (notes));
    repaint (headingArea());
}

void MidiSeerEditor::showNotice (const juce::String& text)
{
    notice = text;
    noticeUntil = juce::Time::getMillisecondCounter() + 4000;
    repaint (headingArea());
}

void MidiSeerEditor::loadFile (const juce::File& file)
{
    if (! seer.loadMidiFile (file))
        showNotice ("No notes found in " + file.getFileName());
}

void MidiSeerEditor::chooseFile()
{
    chooser = std::make_unique<juce::FileChooser> ("Load note lengths from a MIDI file", juce::File(), "*.mid;*.midi");
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [this] (const juce::FileChooser& fc) {
        if (const auto file = fc.getResult(); file.existsAsFile())
            loadFile (file);
    });
}

void MidiSeerEditor::forgetClicked()
{
    // Asks once more on the button itself.
    if (forgetArmedUntil == 0)
    {
        forgetArmedUntil = juce::Time::getMillisecondCounter() + 3000;
        forgetButton.setButtonText ("Sure?");
        return;
    }
    forgetArmedUntil = 0;
    forgetButton.setButtonText ("Forget all");
    seer.learnedMap().clear();
    changeListenerCallback (nullptr);
}

bool MidiSeerEditor::isInterestedInFileDrag (const juce::StringArray& files)
{
    for (const auto& f : files)
        if (isMidiFile (f))
            return true;
    return false;
}

void MidiSeerEditor::fileDragEnter (const juce::StringArray&, int, int)
{
    learnedView.setDropHighlight (true);
}

void MidiSeerEditor::fileDragExit (const juce::StringArray&)
{
    learnedView.setDropHighlight (false);
}

void MidiSeerEditor::filesDropped (const juce::StringArray& files, int, int)
{
    learnedView.setDropHighlight (false);
    for (const auto& f : files)
        if (isMidiFile (f))
        {
            loadFile (juce::File (f));
            return;
        }
}

} // namespace midiseer
