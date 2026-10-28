#pragma once

#include "HorizonLookAndFeel.h"
#include "Motion.h"

class HorizonPadAudioProcessor;

namespace horizon::ui
{

/**
    The OUTPUT column, last on the right: the Meter card's look on the same
    measurement as before - the output's RMS over 20 ms in dBFS, a quick-up,
    slow-down bar, and a peak line that holds the recent highest reading
    for 1.2 s and then falls. One 8 px well, 48 px tall with a 1 px `line`
    border, `meter-level` below -6 dBFS and `meter-hot` above, the peak line
    in `ink`; under it "RMS" (what the number is) and the value.

    Beside it the LIMIT LED (the Meter card's clip LED, owner 2026-10-28):
    outlined with `ink-subtle` text while idle, lit in `meter-hot` with
    `ground` text while the output limiter works - a sample passed its knee
    (-1.9 dBFS) in the last 20 ms (isLimiting()) - and for 1 s after. The
    word is always there, so the state never rests on colour alone. Nothing
    clips (the limiter holds the output under -0.04 dBFS), hence LIMIT, not
    CLIP.
*/
class OutputMeter final : public juce::Component,
                          public juce::SettableTooltipClient
{
public:
    static constexpr int kWidth = 93;
    static constexpr int kHeight = 88;

    explicit OutputMeter (HorizonPadAudioProcessor&);

    /** One editor frame (`now` on its clock, `dt` since the last). */
    void advance (double now, float dt);

    void paint (juce::Graphics&) override;

private:
    juce::Rectangle<float> wellArea() const noexcept;
    juce::Rectangle<float> ledArea() const noexcept;
    juce::Rectangle<float> valueBox() const noexcept;

    HorizonPadAudioProcessor& processor;

    motion::Follower displayedLevel;   // 0..1 of the well
    float peakLevel = 0.0f;
    double peakHoldUntil = 0.0;

    float shownLevel = 0.0f, shownPeak = 0.0f;
    int shownDb = std::numeric_limits<int>::min();
    double shownAt = 0.0;

    double limitLitUntil = -1.0;
    bool limitShown = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (OutputMeter)
};

} // namespace horizon::ui
