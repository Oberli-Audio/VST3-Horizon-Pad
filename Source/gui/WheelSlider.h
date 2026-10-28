#pragma once

#include "HorizonLookAndFeel.h"

class HorizonPadAudioProcessor;

namespace horizon::ui
{

/**
    PITCH or MOD as the Slider card's upright wheel (the WHEELS group, always
    first on the left - design system round 10): a 4 px `line-strong` track
    as tall as the knobs beside it (48 px), an `accent` fill, and a 14 px
    thumb - `surface-panel` with a 1.5 px `line-control` ring and a short
    `accent` pointer, the knob in miniature. PITCH fills from its centre
    detent and springs back when let go; MOD fills from the bottom and holds.
    Label and value sit under the track, as under a knob.

    Not a host-automatable parameter (horizon::PerformanceState): a drag
    writes straight into the processor's lock-free performance state, and
    incoming MIDI (a hardware pitch or mod wheel) moves it on screen through
    refreshFromProcessor(), called every editor frame. Double-click resets,
    Tab reaches it, the arrow keys move it, a click does not take keyboard
    focus.
*/
class WheelSlider final : public juce::Component
{
public:
    enum class Kind { pitch, mod };

    static constexpr int kWidth = 40;
    static constexpr int kTrackHeight = 48;
    static constexpr int kFocusRoom = 5;    // the focus ring's reach outside the track
    static constexpr int kHeight = kFocusRoom + kTrackHeight + 4 + 12 + 8 + 16;

    WheelSlider (HorizonPadAudioProcessor& processorToUse, Kind kind);
    ~WheelSlider() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    void refreshFromProcessor();

private:
    class Track final : public juce::Slider
    {
    public:
        explicit Track (Kind k);
        void paint (juce::Graphics&) override;
        void focusGained (FocusChangeType) override { repaint(); }
        void focusLost (FocusChangeType) override   { repaint(); }

    private:
        const Kind kind;
    };

    juce::String valueText() const;

    HorizonPadAudioProcessor& processor;
    const Kind kind;
    Track track;

    bool updating = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (WheelSlider)
};

} // namespace horizon::ui
