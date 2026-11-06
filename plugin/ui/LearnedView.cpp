#include "LearnedView.h"

#include <cmath>

namespace midiseer
{

namespace theme = tng::theme;

namespace
{
const juce::Colour accent = theme::accentColour (0);
constexpr float maxRowHeight = 8.0f;
} // namespace

void LearnedView::setNotes (std::vector<LearnedNote> newNotes)
{
    notes = std::move (newNotes);
    lowPitch = 127;
    highPitch = 0;
    for (const auto& n : notes)
    {
        lowPitch = std::min (lowPitch, static_cast<int> (n.pitch));
        highPitch = std::max (highPitch, static_cast<int> (n.pitch));
    }
    if (notes.empty())
    {
        lowPitch = 60;
        highPitch = 72;
    }
    repaint();
}

void LearnedView::setTiming (double loopBeats, double beatsPerBar)
{
    if (juce::exactlyEqual (loopBeats, loop) && juce::exactlyEqual (beatsPerBar, barBeats))
        return;
    loop = loopBeats;
    barBeats = std::max (beatsPerBar, 0.25);
    repaint();
}

void LearnedView::setPlayhead (double songBeat)
{
    if (songBeat >= 0.0 && loop <= 0.0)
        page = std::floor (songBeat / windowLength()) * windowLength();
    if (std::abs (songBeat - playhead) < 1.0e-4)
        return;
    playhead = songBeat;
    repaint();
}

void LearnedView::setDropHighlight (bool on)
{
    if (on != dropHighlight)
    {
        dropHighlight = on;
        repaint();
    }
}

double LearnedView::windowLength() const
{
    return loop > 0.0 ? loop : barBeats * pageBars;
}

double LearnedView::windowStart() const
{
    return loop > 0.0 ? 0.0 : page;
}

juce::String LearnedView::describeWindow() const
{
    if (loop > 0.0)
        return {};
    const int first = juce::roundToInt (page / barBeats) + 1;
    return "bars " + juce::String (first) + " to " + juce::String (first + pageBars - 1);
}

void LearnedView::paint (juce::Graphics& g)
{
    const auto box = getLocalBounds().toFloat();
    const auto inside = box.reduced (theme::boxPadding);
    g.setColour (dropHighlight ? theme::tint (accent) : theme::panel);
    g.fillRect (box);

    const double length = windowLength();
    const double start = windowStart();
    const auto roll = inside.reduced (0.0f, theme::padding - theme::boxPadding);
    const float pxPerBeat = roll.getWidth() / static_cast<float> (length);

    // Beat lines, stronger on bars. None at the start: the box's edge is there.
    for (int b = 1; b < static_cast<int> (std::ceil (length - 1.0e-6)); ++b)
    {
        const bool bar = std::fmod (b, barBeats) < 1.0e-6;
        g.setColour (bar ? theme::gridLine : theme::gridLine.withAlpha (0.35f));
        g.drawVerticalLine (juce::roundToInt (roll.getX() + pxPerBeat * static_cast<float> (b)), inside.getY(), inside.getBottom());
    }

    if (notes.empty())
    {
        g.setColour (theme::inkMuted);
        g.setFont (theme::font (14.0f));
        g.drawFittedText ("Play the song once and MidiSeer learns how long each note is.\n"
                          "Or drop a MIDI file here to load them all at once.",
                          roll.toNearestInt().reduced (theme::padding, 0), juce::Justification::centred, 2);
    }
    else
    {
        const int rows = highPitch - lowPitch + 1;
        const float rowHeight = std::min (maxRowHeight, roll.getHeight() / static_cast<float> (rows));
        const float bottom = roll.getCentreY() + rowHeight * static_cast<float> (rows) / 2.0f;
        const auto startTick = static_cast<std::int64_t> (std::floor (start * ticksPerBeat));
        const auto endTick = static_cast<std::int64_t> (std::ceil ((start + length) * ticksPerBeat));

        g.setColour (accent);
        for (const auto& n : notes)
        {
            if (n.tick + n.length <= startTick || n.tick >= endTick)
                continue;
            const float x0 = roll.getX() + static_cast<float> ((static_cast<double> (n.tick) / ticksPerBeat - start) * pxPerBeat);
            const float x1 = x0 + static_cast<float> (static_cast<double> (n.length) / ticksPerBeat * pxPerBeat);
            const float y = bottom - static_cast<float> (n.pitch - lowPitch + 1) * rowHeight;
            const auto r = juce::Rectangle<float>::leftTopRightBottom (std::max (x0, roll.getX()), y, std::min (x1 - 1.0f, roll.getRight()), y + std::max (2.0f, rowHeight - 1.0f));
            g.fillRect (r.withX (theme::pixel (r.getX())).withWidth (std::max (1.0f, theme::pixel (r.getWidth()))));
        }
    }

    // The playhead, where the song is in this view.
    if (playhead >= 0.0)
    {
        double at = playhead;
        if (loop > 0.0)
            at = std::fmod (playhead, loop);
        at -= start;
        if (at >= 0.0 && at <= length)
        {
            g.setColour (theme::danger);
            g.fillRect (juce::Rectangle<float> (theme::pixel (roll.getX() + static_cast<float> (at) * pxPerBeat), inside.getY(), 1.0f, inside.getHeight()));
        }
    }

    g.setColour (theme::ink);
    g.drawRect (box, 1.0f);
}

} // namespace midiseer
