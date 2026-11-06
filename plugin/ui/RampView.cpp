#include "RampView.h"

namespace midiseer
{

namespace theme = tng::theme;

namespace
{
const juce::Colour accent = theme::accentColour (0);
constexpr float labelHeight = 16.0f;
} // namespace

void RampView::setShape (int startValue, int endValue, double newCurve)
{
    if (startValue == start && endValue == end && juce::exactlyEqual (newCurve, curve))
        return;
    start = startValue;
    end = endValue;
    curve = newCurve;
    repaint();
}

void RampView::setStatus (const RampStatus& newStatus)
{
    if (newStatus.active == status.active && newStatus.moving == status.moving && newStatus.known == status.known
        && std::abs (newStatus.progress - status.progress) < 0.002 && newStatus.value == status.value)
        return;
    status = newStatus;
    repaint();
}

juce::Point<float> RampView::pointAt (juce::Rectangle<float> area, double p) const
{
    const double value = start + (end - start) * rampShape (p, curve);
    return { area.getX() + static_cast<float> (p) * area.getWidth(), area.getBottom() - static_cast<float> (value / 127.0) * area.getHeight() };
}

void RampView::paint (juce::Graphics& g)
{
    const auto box = getLocalBounds().toFloat();
    const auto inside = box.reduced (theme::boxPadding);
    g.setColour (theme::panel);
    g.fillRect (box);

    // The ramp, between "note on" and "note off" labels along the bottom.
    const auto labels = inside.withTop (inside.getBottom() - labelHeight).reduced (theme::padding - theme::boxPadding, 0);
    const auto graph = inside.withTrimmedBottom (labelHeight + 4.0f).reduced (theme::padding, theme::padding);

    g.setColour (theme::separator);
    g.drawHorizontalLine (juce::roundToInt (graph.getY()), inside.getX(), inside.getRight()); // 127
    g.drawHorizontalLine (juce::roundToInt (graph.getBottom()), inside.getX(), inside.getRight()); // 0

    juce::Path line, area;
    constexpr int steps = 96;
    for (int i = 0; i <= steps; ++i)
    {
        const auto pt = pointAt (graph, static_cast<double> (i) / steps);
        if (i == 0)
            line.startNewSubPath (pt);
        else
            line.lineTo (pt);
    }
    area = line;
    area.lineTo (graph.getBottomRight());
    area.lineTo (graph.getBottomLeft());
    area.closeSubPath();

    g.setColour (theme::tint (accent));
    g.fillPath (area);
    g.setColour (accent);
    g.strokePath (line, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    // Where the playing ramp is.
    if (status.active && status.moving)
    {
        const auto pt = pointAt (graph, status.progress);
        g.setColour (theme::danger);
        g.fillRect (juce::Rectangle<float> (theme::pixel (pt.x), inside.getY(), 1.0f, graph.getBottom() - inside.getY()));
        g.setColour (accent);
        g.fillEllipse (juce::Rectangle<float> (9.0f, 9.0f).withCentre (pt));
        g.setColour (theme::panel);
        g.fillEllipse (juce::Rectangle<float> (4.0f, 4.0f).withCentre (pt));
    }

    g.setFont (theme::font (13.0f));
    g.setColour (theme::inkMuted);
    g.drawText ("Note on: " + juce::String (start), labels, juce::Justification::centredLeft);
    g.drawText ("Note off: " + juce::String (end), labels, juce::Justification::centredRight);

    // Whether the ramp playing knew its note's length.
    if (status.active)
    {
        const juce::String text = status.known ? "Length learned" : "First time through: guessing the length";
        g.drawText (text + juce::String (juce::CharPointer_UTF8 ("  \xc2\xb7  CC value ")) + juce::String (status.value), labels,
                    juce::Justification::centred);
    }

    g.setColour (theme::ink);
    g.drawRect (box, 1.0f);
}

} // namespace midiseer
