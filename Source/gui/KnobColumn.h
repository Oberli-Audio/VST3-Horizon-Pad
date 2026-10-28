#pragma once

#include "AnimatedKnob.h"

class HorizonPadAudioProcessor;

namespace horizon::ui
{

/**
    One parameter's knob as the Strip shows it: a 64 px column with the
    `knob-md` 48 px knob, its caps `label` (`ink-muted`) 4 px under it and
    its value (`value`, tabular, `ink`) 8 px under that - the parameter's own
    text, so the editor and the host's lane read the same ("48 %", "0.64 s"),
    counting along with the knob's glide.

    A pad's column adds a 32 x 4 px level bar under its value: that layer's
    real output (getLayerLevel(), after VOL, before the reverb), in
    `meter-level` on a `surface-sunken` well. It replaced the pad cards'
    coloured glow (editor F): outside the Scene nothing glows.
*/
class KnobColumn final : public juce::Component
{
public:
    static constexpr int kWidth = 64;               // the column, in the row's layout
    static constexpr int kOverhang = 8;             // labels may spill this far into the gaps (CLEARING at 11 px)
    static constexpr int kBoundsWidth = kWidth + 2 * kOverhang;
    static constexpr int kKnobSize = 48;
    static constexpr int kHeight = 88;              // knob, label, value
    static constexpr int kHeightWithLevel = 96;     // plus the level bar

    KnobColumn (HorizonPadAudioProcessor&, const char* parameterId, juce::String label,
                juce::String accessibleName, bool showsLevel);
    ~KnobColumn() override;

    AnimatedKnob& getKnob() noexcept { return knob; }

    /** One editor frame: the knob's glide and, for a pad, its level bar
        (`layerRms`, linear; ignored without a bar). */
    void advance (double now, float dt, float layerRms = 0.0f);

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    juce::Rectangle<float> labelBox() const noexcept;
    juce::Rectangle<float> valueBox() const noexcept;
    juce::Rectangle<float> levelBox() const noexcept;

    AnimatedKnob knob;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;

    const juce::String label;
    const bool showsLevel;

    motion::Follower level;          // 0..1 of the bar
    float shownLevel = 0.0f;
    double levelShownAt = 0.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (KnobColumn)
};

} // namespace horizon::ui
