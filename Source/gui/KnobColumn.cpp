#include "KnobColumn.h"
#include "../PluginProcessor.h"

namespace horizon::ui
{

namespace
{
    // Layer RMS (after VOL, before the reverb) -> bar 0..1, in dB. Measured
    // across the bank with tools/gui_snapshot (2026-10-07): the leading pad
    // of a preset sits at -24 to -33 dB on one note or a chord, a pad mixed
    // low at -45 to -55 dB. So the leading pad's bar runs long, a quiet
    // one's short, and Bloom's tremolo (+/-2.8 dB) moves its bar about 10%.
    constexpr float kLevelFloorDb = -54.0f;
    constexpr float kLevelCeilingDb = -26.0f;

    constexpr float kBarWidth = 32.0f;
    constexpr float kBarHeight = 4.0f;
}

KnobColumn::KnobColumn (HorizonPadAudioProcessor& processor, const char* parameterId, juce::String labelToUse,
                        juce::String accessibleName, bool showsLevelToUse)
    : label (std::move (labelToUse)), showsLevel (showsLevelToUse)
{
    // Tooltips come from parameters.csv (Params::tooltipFor). ATTACK and
    // RELEASE read as a time, so their percent range is left off (paramgen
    // has no display column yet - docs/exceptions.md EX-005).
    juce::String tooltip (Params::tooltipFor (parameterId));

    if (juce::String (parameterId) == ParamID::attackMacro || juce::String (parameterId) == ParamID::releaseMacro)
        tooltip = tooltip.upToFirstOccurrenceOf (" Range ", false, false);

    knob.setTooltip (tooltip);
    knob.setHelpText (tooltip);
    addAndMakeVisible (knob);

    attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        processor.getAPVTS(), parameterId, knob);
    knob.syncDisplay();

    if (auto* parameter = processor.getAPVTS().getParameter (parameterId))
        knob.bindParameter (*parameter, accessibleName);

    knob.onValueChange = [this] { repaint (valueBox().getSmallestIntegerContainer()); };

    setInterceptsMouseClicks (false, true);
}

KnobColumn::~KnobColumn() = default;

juce::Rectangle<float> KnobColumn::labelBox() const noexcept
{
    return { 0.0f, (float) kKnobSize + 4.0f, (float) kBoundsWidth, 12.0f };
}

juce::Rectangle<float> KnobColumn::valueBox() const noexcept
{
    return { 0.0f, labelBox().getBottom() + 8.0f, (float) kBoundsWidth, 16.0f };
}

juce::Rectangle<float> KnobColumn::levelBox() const noexcept
{
    return { ((float) kBoundsWidth - kBarWidth) * 0.5f, valueBox().getBottom() + 4.0f, kBarWidth, kBarHeight };
}

void KnobColumn::resized()
{
    knob.setBounds ((kBoundsWidth - kKnobSize) / 2, 0, kKnobSize, kKnobSize);
}

void KnobColumn::advance (double now, float dt, float layerRms)
{
    if (knob.advance (now, dt))
        repaint (valueBox().getSmallestIntegerContainer());

    if (! showsLevel)
        return;

    // Quick to rise, a little slower to fall: Bloom's 3.2 Hz tremolo still
    // reads as a pulse, without the flicker of raw 20 ms readings.
    const auto db = juce::Decibels::gainToDecibels (layerRms, -100.0f);
    const auto target = juce::jlimit (0.0f, 1.0f, (db - kLevelFloorDb) / (kLevelCeilingDb - kLevelFloorDb));

    level.advance (target, dt, 0.03f, 0.14f, 2.0e-3f);

    // Redrawn at the ambient rate, and only for a step of a quarter pixel
    // or more - or to land exactly on empty.
    const auto step = std::abs (level.value - shownLevel) * kBarWidth;
    const auto visible = step >= 0.25f || (step > 0.0f && level.value <= 0.0f);

    if (visible && now - levelShownAt >= motion::kAmbientFrameSeconds)
    {
        shownLevel = level.value;
        levelShownAt = now;
        repaint (levelBox().getSmallestIntegerContainer());
    }
}

void KnobColumn::paint (juce::Graphics& g)
{
    type::draw (g, label, labelBox(), type::label(), Tokens::inkMuted, juce::Justification::centred);
    type::draw (g, knob.textFor (knob.getDisplayValue()), valueBox(), type::value(), Tokens::ink,
                juce::Justification::centred);

    if (showsLevel)
    {
        const auto well = levelBox();
        g.setColour (Tokens::surfaceSunken);
        g.fillRoundedRectangle (well, 2.0f);

        if (shownLevel > 0.0f)
        {
            juce::Graphics::ScopedSaveState save (g);
            juce::Path clip;
            clip.addRoundedRectangle (well, 2.0f);
            g.reduceClipRegion (clip);
            g.setColour (Tokens::meterLevel);
            g.fillRect (well.withWidth (well.getWidth() * shownLevel));
        }
    }
}

} // namespace horizon::ui
