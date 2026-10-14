#include "PadKnob.h"
#include "../PluginProcessor.h"

namespace horizon::ui
{

namespace
{
    // Layer RMS (after VOL, before the reverb) -> glow 0..1, in dB. Measured
    // across the bank with tools/gui_snapshot (2026-10-07): the leading pad
    // of a preset sits at -24 to -33 dB on one note or a chord, a pad mixed
    // low at -45 to -55 dB. So the leading pad glows strongly, a quiet one
    // faintly, and Bloom's tremolo (+/-2.8 dB) moves the glow about 10%.
    constexpr float kGlowFloorDb = -54.0f;
    constexpr float kGlowCeilingDb = -26.0f;

    // A glow step smaller than this (of its 0..1 range) is not redrawn: at
    // most 0.007 of alpha on the dot's light, below anything visible.
    constexpr float kGlowVisibleStep = 0.015f;

    constexpr float kDotSize = 17.0f;
    constexpr float kDotGlowReach = 11.0f;   // the dot's light at full level, beyond its 6 px resting halo
    constexpr float kRingGlowReach = 13.0f;  // the ring's halo beyond its outer edge
}

PadKnob::PadKnob (HorizonPadAudioProcessor& processorToUse, int layerIndex,
                  juce::String captionToUse, juce::String subtitleToUse,
                  const char* volumeParamId)
    : accent (Palette::layerAccents[juce::jlimit (0, kNumLayers - 1, layerIndex)]),
      caption (std::move (captionToUse)),
      subtitle (std::move (subtitleToUse))
{
    setUpRingKnob (volumeSlider, accent);
    volumeSlider.setTooltip (caption + " volume - how loud this layer sits in the mix.");
    addAndMakeVisible (volumeSlider);
    volumeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        processorToUse.getAPVTS(), volumeParamId, volumeSlider);
    volumeSlider.syncDisplay();
    volumeSlider.onValueChange = [this] { repaint (readoutArea()); repaint (dotArea()); };
}

PadKnob::~PadKnob() = default;

void PadKnob::resized()
{
    const auto slots = computeColumnSlots (getLocalBounds());

    // VOL's value sits right under its own knob instead of in the shared
    // row-aligned value slot, so the group is centred in both slots together.
    const auto c = computePadControlLayout (slots.control.getUnion (slots.value));
    volumeSlider.setBounds (volumeSlider.boundsForRing (c.control.withSizeKeepingCentre (78, 78)));
}

juce::Rectangle<int> PadKnob::dotArea() const
{
    const auto slots = computeColumnSlots (getLocalBounds());
    const auto reach = juce::roundToInt (kDotSize * 0.5f + 6.0f + kDotGlowReach) + 2;
    return juce::Rectangle<int> (2 * reach, 2 * reach).withCentre (slots.icon.getCentre());
}

juce::Rectangle<int> PadKnob::haloArea() const
{
    return volumeSlider.getRingBounds().expanded (juce::roundToInt (kRingGlowReach) + 2);
}

juce::Rectangle<int> PadKnob::readoutArea() const
{
    const auto slots = computeColumnSlots (getLocalBounds());
    return computePadControlLayout (slots.control.getUnion (slots.value)).value;
}

void PadKnob::advance (double now, float dt, float layerRms)
{
    if (volumeSlider.advance (now, dt))
    {
        repaint (readoutArea());
        repaint (dotArea());
    }

    // Quick to rise, a little slower to fall: Bloom's 3.2 Hz tremolo still
    // reads as a pulse, without the flicker of raw 20 ms readings.
    const auto db = juce::Decibels::gainToDecibels (layerRms, -100.0f);
    const auto target = juce::jlimit (0.0f, 1.0f, (db - kGlowFloorDb) / (kGlowCeilingDb - kGlowFloorDb));

    levelGlow.advance (target, dt, 0.03f, 0.14f, 2.0e-3f);

    // The glow follows every frame but is redrawn at the ambient rate, and
    // only for a step you could see - or to land exactly on dark.
    const auto step = std::abs (levelGlow.value - shownGlow);
    const auto visible = step >= kGlowVisibleStep || (step > 0.0f && levelGlow.value <= 0.0f);

    if (visible && now - glowShownAt >= motion::kAmbientFrameSeconds)
    {
        shownGlow = levelGlow.value;
        glowShownAt = now;
        repaint (dotArea());
        repaint (haloArea());
    }
}

void PadKnob::renderGlowSprites (float scale)
{
    glowScale = scale;

    // A radial gradient of the pad's colour at full strength: alpha
    // `centreAlpha` at the centre, then each (radius, alpha) stop, clear at
    // `radius`.
    const auto render = [&] (float radius, float centreAlpha, std::initializer_list<std::pair<float, float>> stops)
    {
        const auto pixels = juce::jmax (2, 2 * juce::roundToInt (radius * scale));
        juce::Image sprite (juce::Image::ARGB, pixels, pixels, true);
        juce::Graphics g (sprite);
        g.addTransform (juce::AffineTransform::scale ((float) pixels / (2.0f * radius)));

        juce::ColourGradient gradient (accent.withAlpha (centreAlpha), radius, radius,
                                       accent.withAlpha (0.0f), 2.0f * radius, radius, true);

        for (const auto& [at, alpha] : stops)
            gradient.addColour (at / radius, accent.withAlpha (alpha));

        g.setGradientFill (gradient);
        g.fillEllipse (0.0f, 0.0f, 2.0f * radius, 2.0f * radius);
        return sprite;
    };

    // The ring's halo: clear inside the ring, brightest just outside its
    // outer edge, gone kRingGlowReach beyond.
    const auto ringOuter = (float) (volumeSlider.getRingBounds().getWidth() - 4) * 0.5f;
    ringGlow = render (ringOuter + kRingGlowReach, 0.0f, { { ringOuter - 4.0f, 0.0f }, { ringOuter + 1.5f, 0.30f } });

    // The dot's light: full from the centre out past the resting halo, then fading.
    dotLight = render (kDotSize * 0.5f + 6.0f + kDotGlowReach, 0.45f, { { kDotSize * 0.5f + 4.0f, 0.45f } });
}

void PadKnob::drawSprite (juce::Graphics& g, const juce::Image& sprite, juce::Point<float> centre, float opacity) const
{
    // Top-left on a whole device pixel, so the copy is 1:1.
    const auto size = (float) sprite.getWidth() / glowScale;
    const auto x = std::round ((centre.x - size * 0.5f) * glowScale) / glowScale;
    const auto y = std::round ((centre.y - size * 0.5f) * glowScale) / glowScale;

    juce::Graphics::ScopedSaveState save (g);
    g.setOpacity (juce::jlimit (0.0f, 1.0f, opacity));
    g.drawImageTransformed (sprite, juce::AffineTransform::scale (1.0f / glowScale).translated (x, y));
}

void PadKnob::paint (juce::Graphics& g)
{
    drawColumnCard (g, getLocalBounds().toFloat());

    const auto slots = computeColumnSlots (getLocalBounds());
    const auto c = computePadControlLayout (slots.control.getUnion (slots.value));
    const auto glow = shownGlow;
    const auto shownVolume = volumeSlider.getDisplayValue();

    if (glow > 0.002f)
    {
        const auto scale = juce::jlimit (1.0f, 4.0f, g.getInternalContext().getPhysicalPixelScaleFactor());

        if (! juce::approximatelyEqual (scale, glowScale))
            renderGlowSprites (scale);
    }

    // --- Status dot: lit (with a glow) once the pad is audible, matching the
    // design's dotStyle threshold (vol > 0.04); the glow grows with the
    // layer's output while it sounds.
    {
        const auto lit = shownVolume > 0.04;
        const auto centre = slots.icon.toFloat().getCentre();
        auto dot = juce::Rectangle<float> (kDotSize, kDotSize).withCentre (centre);

        if (lit)
        {
            // Soft light spreading from the resting halo, as strong as the
            // layer is loud.
            if (glow > 0.002f)
                drawSprite (g, dotLight, centre, glow);

            g.setColour (accent.withAlpha (0.35f));
            g.fillEllipse (dot.expanded (6.0f));
        }

        g.setColour (lit ? accent.brighter (0.2f * glow) : Palette::dotUnlit);
        g.fillEllipse (dot);
    }

    // --- The ring's halo: a soft band of the pad's colour just outside the
    // ring, as bright as the layer is loud. Drawn here, behind the knob.
    if (glow > 0.002f)
        drawSprite (g, ringGlow, volumeSlider.getRingBounds().toFloat().getCentre(), glow);

    g.setColour (Palette::textKnobLabel);
    g.setFont (labelFont (TypeScale::label, true));
    g.drawText (caption, slots.label, juce::Justification::centred);

    g.setColour (Palette::textDim);
    g.setFont (labelFont (TypeScale::caption).italicised());
    g.drawFittedText (subtitle, slots.caption, juce::Justification::centred, 2);

    // --- VOL: label, then (drawn via the slider itself) its knob, then its
    // own value directly beneath it - counting along with the knob's glide,
    // and lit in the pad's colour while the knob is hovered or dragged.
    g.setColour (Palette::macroLabel);
    g.setFont (labelFont (11.0f, true));
    g.drawText ("VOL", c.label, juce::Justification::centred);

    g.setColour (Palette::textValue.interpolatedWith (accent, volumeSlider.getHighlight()));
    g.setFont (labelFont (17.0f, true));
    g.drawText (juce::String (juce::roundToInt (shownVolume * 100.0)) + "%",
               c.value, juce::Justification::centred);
}

} // namespace horizon::ui
