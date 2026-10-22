#pragma once

#include "AnimatedKnob.h"
#include "../dsp/HorizonTypes.h"

class HorizonPadAudioProcessor;

namespace horizon::ui
{

/**
    The MACROS block: six small knobs in three rows of two - ATTACK/RELEASE,
    FILTER/REVERB, WIDTH/DETUNE - all genuine host-automatable parameters,
    all acting globally across the four pads (see LayerBase and FxChain for
    what each one actually does to the DSP).
*/
class MacrosPanel final : public juce::Component
{
public:
    explicit MacrosPanel (HorizonPadAudioProcessor&);
    ~MacrosPanel() override;

    /** One editor frame: every knob's glide and fades. */
    void advance (double now, float dt);

    /** Starts the recall wave (or, with fromZero, the sunrise sweep) at
        `startTime`, one row after another, top to bottom. */
    void beginWave (double startTime, double rowDelay, bool fromZero);

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    struct Knob
    {
        AnimatedKnob slider { 5 };
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
        juce::String caption;
        juce::Colour accent;
        juce::Rectangle<int> valueCell;   // where its readout is drawn (set in resized())
    };

    void setUpKnob (Knob& knob, const juce::String& caption, const char* paramId,
                    juce::Colour accent, HorizonPadAudioProcessor& processor);

    std::array<Knob, (size_t) kNumGlobalParams> knobs;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MacrosPanel)
};

} // namespace horizon::ui
