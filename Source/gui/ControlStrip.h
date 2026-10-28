#pragma once

#include "KnobColumn.h"
#include "WheelSlider.h"
#include "OutputMeter.h"

class HorizonPadAudioProcessor;

namespace horizon::ui
{

/**
    The control row under the sky - the Strip of the PluginWindow card, in
    one row: WHEELS (PITCH, MOD) first on the left where a player's hand
    finds them (round 10), then PADS (ROOT, CLEARING, EXPANSE, BLOOM, each
    with its level bar), MACROS (ATTACK, RELEASE, FILTER, REVERB, WIDTH,
    DETUNE) and OUTPUT last on the right. Groups are split by a 1 px `line`
    with 24 px either side; each has its caps `section` title in `ink-muted`
    16 px above its controls. Knobs are `knob-md` in 64 px columns, 16 px
    apart. Signal flow reads left to right, as the sound is made.

    The OUTPUT group is wider than the sketch's 64 px column, by the room
    left at the row's end: the LIMIT LED sits beside the meter.
*/
class ControlStrip final : public juce::Component
{
public:
    static constexpr int kHeight = 14 + 16 + KnobColumn::kHeightWithLevel;

    explicit ControlStrip (HorizonPadAudioProcessor&);
    ~ControlStrip() override;

    /** One editor frame: knob glides, level bars, wheels from MIDI, meter. */
    void advance (double now, float dt);

    /** The recall wave: every knob, left to right, `step` seconds apart,
        from `startTime`. With `fromZero`, from the bottom (sunrise). */
    void beginWave (double startTime, double step, bool fromZero);

    /** The knob for a parameter ID, or nullptr (tools/gui_snapshot). */
    AnimatedKnob* findKnob (const juce::String& parameterId);

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    struct Group
    {
        juce::String title;
        int x = 0, width = 0;
    };

    HorizonPadAudioProcessor& processor;

    WheelSlider pitchWheel, modWheel;
    juce::OwnedArray<KnobColumn> pads, macros;
    juce::StringArray padIds, macroIds;
    OutputMeter outputMeter;

    std::array<Group, 4> groups;
    std::array<int, 3> dividerXs {};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ControlStrip)
};

} // namespace horizon::ui
