#pragma once

#include "midiseer/Seer.h"

#include <tng_ui/tng_ui.h>

namespace midiseer
{

// The ramp every note gets: from the start value at note on to the end value at note off, in
// the curve's shape, with a dot following the ramp that is playing.
class RampView final : public juce::Component
{
public:
    void setShape (int startValue, int endValue, double curve);
    void setStatus (const RampStatus& status);
    void paint (juce::Graphics& g) override;

private:
    juce::Point<float> pointAt (juce::Rectangle<float> area, double p) const;

    int start = 0, end = 127;
    double curve = 0.0;
    RampStatus status;
};

} // namespace midiseer
