#include "OutputMeter.h"
#include "../PluginProcessor.h"

namespace horizon::ui
{

namespace
{
    // Standard peak/VU-meter convention: map the level in dB, not raw linear
    // amplitude, onto the fill. Ear and eye both track level logarithmically,
    // so a linear mapping crowds this DSP's whole normal RMS range into the
    // meter's bottom sliver (previously patched over with an arbitrary *3.0
    // linear boost that only "worked" for typical playing levels and still
    // pinned to 100% well before actual clipping).
    constexpr float kFloorDb = -48.0f;

    // Ballistics: quick up, slower down. The peak line holds, then falls a
    // third of the meter per second.
    constexpr float kRiseSeconds = 0.04f;
    constexpr float kFallSeconds = 0.30f;
    constexpr double kPeakHoldSeconds = 1.2;
    constexpr float kPeakFallPerSecond = 0.33f;
}

OutputMeter::OutputMeter (HorizonPadAudioProcessor& processorToUse)
    : processor (processorToUse)
{
    setTooltip ("Live output level (RMS) - a reference only, not a control.");
}

juce::Rectangle<float> OutputMeter::trackArea() const
{
    // A slim rounded column, matching meterOuterStyle exactly.
    const auto slots = computeColumnSlots (getLocalBounds());
    return slots.control.withSizeKeepingCentre (26, slots.control.getHeight()).toFloat();
}

void OutputMeter::advance (double now, float dt)
{
    const auto levelDb = juce::Decibels::gainToDecibels (processor.getOutputLevel(), kFloorDb);
    const auto target = juce::jlimit (0.0f, 1.0f, (levelDb - kFloorDb) / -kFloorDb);

    displayedLevel.advance (target, dt, kRiseSeconds, kFallSeconds, 1.0e-3f);
    const auto level = displayedLevel.value;

    if (level >= peakLevel)
    {
        peakLevel = level;
        peakHoldUntil = now + kPeakHoldSeconds;
    }
    else if (now > peakHoldUntil)
    {
        peakLevel = juce::jmax (level, peakLevel - kPeakFallPerSecond * dt);
    }

    // Redrawn at the ambient rate, and only for a change of at least a pixel
    // of the track - or to land exactly on empty, or to show or hide the peak line.
    if (now - shownAt < motion::kAmbientFrameSeconds)
        return;

    const auto track = trackArea();
    const auto pixels = track.getHeight();
    const auto peakVisible = [] (float peak, float lvl) { return peak > 0.01f && peak > lvl + 0.004f; };

    const auto levelMoved = std::abs (level - shownLevel) * pixels >= 1.0f || (level <= 0.0f && shownLevel > 0.0f);
    const auto peakMoved = std::abs (peakLevel - shownPeak) * pixels >= 1.0f
                         || peakVisible (peakLevel, level) != peakVisible (shownPeak, shownLevel);
    const auto percent = juce::roundToInt (level * 100.0f);

    if (levelMoved || peakMoved)
    {
        shownLevel = level;
        shownPeak = peakLevel;
        shownAt = now;
        repaint (track.getSmallestIntegerContainer().expanded (1));
    }

    if (percent != shownPercent)
    {
        shownPercent = percent;
        shownAt = now;
        repaint (computeColumnSlots (getLocalBounds()).value);
    }
}

void OutputMeter::paint (juce::Graphics& g)
{
    drawColumnCard (g, getLocalBounds().toFloat());

    const auto slots = computeColumnSlots (getLocalBounds());

    g.setColour (Palette::iconGlyph);
    g.setFont (juce::Font (juce::FontOptions().withHeight (TypeScale::icon)));
    g.drawText (juce::String::fromUTF8 ("\xe2\x86\x92"), slots.icon, juce::Justification::centred); // "→"

    g.setColour (Palette::textKnobLabel);
    g.setFont (labelFont (TypeScale::label, true));
    g.drawText ("OUTPUT", slots.label, juce::Justification::centred);

    g.setColour (Palette::textDim);
    g.setFont (labelFont (TypeScale::caption).italicised());
    g.drawFittedText ("the valley", slots.caption, juce::Justification::centred, 2);

    const auto track = trackArea();
    g.setColour (Palette::meterBg);
    g.fillRoundedRectangle (track, 11.0f);

    const auto level = shownLevel;
    auto fill = track.reduced (1.0f);
    const auto inner = fill;
    fill.setTop (fill.getBottom() - fill.getHeight() * level);

    if (fill.getHeight() > 0.5f)
    {
        juce::ColourGradient grad (Palette::meterFillLow, fill.getX(), fill.getBottom(),
                                   Palette::meterFillHigh, fill.getX(), fill.getY(), false);
        g.setGradientFill (grad);
        g.fillRoundedRectangle (fill, 10.0f);
    }

    // --- Peak line: where the level was a moment ago.
    if (shownPeak > 0.01f && shownPeak > level + 0.004f)
    {
        const auto y = inner.getBottom() - inner.getHeight() * shownPeak;
        const auto line = juce::Rectangle<float> (inner.getX() + 4.0f, y - 1.0f, inner.getWidth() - 8.0f, 2.0f);
        g.setColour (Palette::meterFillHigh.withAlpha (0.85f));
        g.fillRoundedRectangle (line.withY (juce::jlimit (inner.getY(), inner.getBottom() - 2.0f, line.getY())), 1.0f);
    }

    g.setColour (Palette::textValue);
    g.setFont (labelFont (TypeScale::value, true));
    g.drawText (juce::String (shownPercent) + "%", slots.value, juce::Justification::centred);
}

} // namespace horizon::ui
