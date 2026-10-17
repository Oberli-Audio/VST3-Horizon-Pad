#pragma once

#include "HorizonLookAndFeel.h"
#include "Motion.h"

#include <limits>

class HorizonPadAudioProcessor;

namespace horizon::ui
{

/** The OUTPUT column: a live level meter (read-only - output level is a
    consequence of everything else, not a control) matching the mockup's
    vertical meter/fader look, with a peak line that holds for a moment and
    then falls. It reads in dBFS - RMS over 20 ms, ticks at 0 / -6 / -12 /
    -24 / -48 and the level as a number - since the UI review of 2026-10-17
    (it showed an unlabelled percentage of its own scale before). There is
    no clip latch: the output limiter's ceiling is -0.04 dBFS, so there is
    nothing to latch. Inherits SettableTooltipClient (plain Component doesn't have
    it) purely so hovering it can explain what it shows, even though it
    isn't interactive. */
class OutputMeter final : public juce::Component,
                          public juce::SettableTooltipClient
{
public:
    explicit OutputMeter (HorizonPadAudioProcessor&);

    /** One editor frame: reads the processor's level and moves the meter. */
    void advance (double now, float dt);

    void paint (juce::Graphics&) override;

private:
    juce::Rectangle<float> trackArea() const;

    HorizonPadAudioProcessor& processor;

    motion::Follower displayedLevel;   // 0..1 of the meter's dB scale, every frame
    float peakLevel = 0.0f;
    double peakHoldUntil = 0.0;

    // What was last drawn: the meter repaints at the ambient rate
    // (motion::kAmbientFrameSeconds), and only for a change of a pixel or more.
    float shownLevel = 0.0f, shownPeak = 0.0f;
    int shownDb = std::numeric_limits<int>::min();   // the readout, whole dB; min() = below the scale
    double shownAt = 0.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (OutputMeter)
};

} // namespace horizon::ui
