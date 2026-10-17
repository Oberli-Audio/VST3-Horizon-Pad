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

    // Ticks on the scale, dBFS. The labels sit left of the track.
    constexpr std::array<float, 5> kTicksDb { 0.0f, -6.0f, -12.0f, -24.0f, -48.0f };
    constexpr int kTickLabelWidth = 26;

    const juce::String minus = juce::String::fromUTF8 ("\xe2\x88\x92");   // a real minus sign

    juce::String dbText (int db)
    {
        if (db == std::numeric_limits<int>::min())
            return minus + juce::String::fromUTF8 ("\xe2\x88\x9e") + " dB";   // "-inf dB"

        return (db < 0 ? minus + juce::String (-db) : juce::String (db)) + " dB";
    }
}

OutputMeter::OutputMeter (HorizonPadAudioProcessor& processorToUse)
    : processor (processorToUse)
{
    setTooltip ("Output level in dBFS - RMS over 20 ms, with a line holding the recent peak. "
                "A reference only, not a control; the output never goes over -0.04 dBFS.");
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
    const auto db = level > 0.0f ? juce::roundToInt (level * -kFloorDb + kFloorDb) : std::numeric_limits<int>::min();

    if (levelMoved || peakMoved)
    {
        shownLevel = level;
        shownPeak = peakLevel;
        shownAt = now;
        repaint (track.getSmallestIntegerContainer().expanded (1));
    }

    if (db != shownDb)
    {
        shownDb = db;
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

    // --- dB scale: short ticks and labels left of the track.
    {
        const auto inner = track.reduced (1.0f);
        g.setFont (labelFont (11.0f));

        for (auto tick : kTicksDb)
        {
            const auto y = inner.getBottom() - inner.getHeight() * (tick - kFloorDb) / -kFloorDb;
            g.setColour (Palette::textDim);
            g.fillRect (juce::Rectangle<float> (track.getX() - 6.0f, y - 0.5f, 4.0f, 1.0f));

            const auto label = juce::Rectangle<float> (track.getX() - 8.0f - (float) kTickLabelWidth,
                                                       y - 7.0f, (float) kTickLabelWidth, 14.0f)
                                   .withY (juce::jlimit (inner.getY() - 3.0f, inner.getBottom() - 11.0f, y - 7.0f));
            g.drawText (tick < 0.0f ? minus + juce::String (juce::roundToInt (-tick)) : juce::String ("0"),
                        label.toNearestInt(), juce::Justification::centredRight);
        }
    }

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
    g.drawText (dbText (shownDb), slots.value, juce::Justification::centred);
}

} // namespace horizon::ui
