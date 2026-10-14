#pragma once

#include "AnimatedKnob.h"

class HorizonPadAudioProcessor;

namespace horizon::ui
{

/**
    One pad's control - ROOT / CLEARING / EXPANSE / BLOOM - with its small
    coloured status dot, caption, one-line poetic subtitle and its VOLUME
    knob (in the pad's own layer accent colour, its value directly beneath
    it). Volume is the only per-pad control: there is no per-layer tone
    block, each pad's character is fixed by its own DSP, and WIDTH - once a
    second knob on every card - is a macro acting on all four (MacrosPanel).

    The card shows its pad's life: the dot and a halo around the ring glow
    with the layer's real output (HorizonPadAudioProcessor::getLayerLevel()),
    so Bloom pulses with its tremolo, Expanse swells in on its long attack
    and a muted pad stays dark. In silence the card looks as designed.
*/
class PadKnob final : public juce::Component
{
public:
    PadKnob (HorizonPadAudioProcessor& processorToUse, int layerIndex,
            juce::String caption, juce::String subtitle,
            const char* volumeParamId);
    ~PadKnob() override;

    /** One editor frame: the knob's glide and fades, and the level glow
        from `layerRms` (the layer's current RMS, 0..1). */
    void advance (double now, float dt, float layerRms);

    AnimatedKnob& getKnob() noexcept { return volumeSlider; }

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    juce::Rectangle<int> dotArea() const;
    juce::Rectangle<int> haloArea() const;
    juce::Rectangle<int> readoutArea() const;

    void renderGlowSprites (float scale);
    void drawSprite (juce::Graphics&, const juce::Image&, juce::Point<float> centre, float opacity) const;

    const juce::Colour accent;
    const juce::String caption;
    const juce::String subtitle;

    AnimatedKnob volumeSlider { 8 };
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> volumeAttachment;

    motion::Follower levelGlow;   // 0..1, follows the layer every frame (see advance())
    float shownGlow = 0.0f;       // the glow as last drawn
    double glowShownAt = 0.0;

    // The ring's halo and the dot's light, rendered once at full strength at
    // the screen's pixel density and drawn with the level as opacity: a 1:1
    // copy, where a radial gradient per frame was a quarter of the editor's
    // painting while playing.
    juce::Image ringGlow, dotLight;
    float glowScale = 0.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PadKnob)
};

} // namespace horizon::ui
