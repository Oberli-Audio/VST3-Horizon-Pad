#include "OutputMeter.h"
#include "../PluginProcessor.h"

namespace horizon::ui
{

namespace
{
    // The level in dB onto the well: ear and eye both track level
    // logarithmically, so a linear fill would crowd the instrument's whole
    // normal range into the bottom sliver.
    constexpr float kFloorDb = -48.0f;
    constexpr float kHotDb = -6.0f;

    // Ballistics: quick up, slower down. The peak line holds, then falls a
    // third of the meter per second.
    constexpr float kRiseSeconds = 0.04f;
    constexpr float kFallSeconds = 0.30f;
    constexpr double kPeakHoldSeconds = 1.2;
    constexpr float kPeakFallPerSecond = 0.33f;

    // The LIMIT LED stays lit this long after the limiter last worked.
    constexpr double kLimitHoldSeconds = 1.0;

    constexpr float kWellWidth = 8.0f;
    constexpr float kWellHeight = 48.0f;
    constexpr float kColumnWidth = 64.0f;

    const juce::String minus = juce::String::fromUTF8 ("\xe2\x88\x92");   // a real minus sign

    juce::String dbText (int db)
    {
        if (db == std::numeric_limits<int>::min())
            return minus + juce::String::fromUTF8 ("\xe2\x88\x9e") + " dB";   // "-inf dB"

        return (db < 0 ? minus + juce::String (-db) : juce::String (db)) + " dB";
    }

    float proportionFor (float db) { return juce::jlimit (0.0f, 1.0f, (db - kFloorDb) / -kFloorDb); }
}

OutputMeter::OutputMeter (HorizonPadAudioProcessor& processorToUse)
    : processor (processorToUse)
{
    setTooltip ("The output level in dBFS: RMS over 20 ms, with a line holding the recent peak. "
                "LIMIT lights while the output limiter holds the sound under 0 dBFS; no preset reaches it.");
    setTitle ("Output level");
    setInterceptsMouseClicks (true, false);
}

juce::Rectangle<float> OutputMeter::wellArea() const noexcept
{
    return { (kColumnWidth - kWellWidth) * 0.5f, 0.0f, kWellWidth, kWellHeight };
}

juce::Rectangle<float> OutputMeter::ledArea() const noexcept
{
    const auto w = std::ceil (type::width (type::label(), "LIMIT")) + 10.0f;
    return { (float) kWidth - w, 0.0f, w, 16.0f };
}

juce::Rectangle<float> OutputMeter::valueBox() const noexcept
{
    return { 0.0f, kWellHeight + 4.0f + 12.0f + 8.0f, kColumnWidth, 16.0f };
}

void OutputMeter::advance (double now, float dt)
{
    // --- LIMIT: lit while the limiter works, and a second after.
    if (processor.isLimiting())
        limitLitUntil = now + kLimitHoldSeconds;

    const auto lit = now < limitLitUntil;

    if (lit != limitShown)
    {
        limitShown = lit;
        repaint (ledArea().getSmallestIntegerContainer());
    }

    // --- The level and its peak line.
    const auto levelDb = juce::Decibels::gainToDecibels (processor.getOutputLevel(), kFloorDb);
    displayedLevel.advance (proportionFor (levelDb), dt, kRiseSeconds, kFallSeconds, 1.0e-3f);
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
    // of the well - or to land exactly on empty, or to show or hide the peak line.
    if (now - shownAt < motion::kAmbientFrameSeconds)
        return;

    const auto pixels = kWellHeight - 2.0f;
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
        repaint (wellArea().getSmallestIntegerContainer().expanded (1));
    }

    if (db != shownDb)
    {
        shownDb = db;
        shownAt = now;
        repaint (valueBox().getSmallestIntegerContainer());
    }
}

void OutputMeter::paint (juce::Graphics& g)
{
    // --- The well.
    const auto well = wellArea();
    g.setColour (Tokens::surfaceSunken);
    g.fillRoundedRectangle (well, Tokens::radiusXs);

    const auto inner = well.reduced (1.0f);

    if (shownLevel > 0.0f)
    {
        juce::Graphics::ScopedSaveState save (g);
        juce::Path clip;
        clip.addRoundedRectangle (inner, Tokens::radiusXs - 1.0f);
        g.reduceClipRegion (clip);

        const auto top = inner.getBottom() - inner.getHeight() * shownLevel;
        const auto hotLine = inner.getBottom() - inner.getHeight() * proportionFor (kHotDb);

        g.setColour (Tokens::meterLevel);
        g.fillRect (juce::Rectangle<float>::leftTopRightBottom (inner.getX(), juce::jmax (top, hotLine),
                                                                inner.getRight(), inner.getBottom()));

        if (top < hotLine)
        {
            g.setColour (Tokens::meterHot);
            g.fillRect (juce::Rectangle<float>::leftTopRightBottom (inner.getX(), top, inner.getRight(), hotLine));
        }
    }

    if (shownPeak > 0.01f && shownPeak > shownLevel + 0.004f)
    {
        const auto y = inner.getBottom() - inner.getHeight() * shownPeak;
        g.setColour (Tokens::ink);
        g.fillRect (juce::Rectangle<float> (inner.getX(), juce::jlimit (inner.getY(), inner.getBottom() - 1.0f, y - 0.5f),
                                            inner.getWidth(), 1.0f));
    }

    g.setColour (Tokens::line);
    g.drawRoundedRectangle (well.reduced (0.5f), Tokens::radiusXs - 0.5f, 1.0f);

    // --- LIMIT.
    {
        const auto led = ledArea();

        if (limitShown)
        {
            g.setColour (Tokens::meterHot);
            g.fillRoundedRectangle (led, Tokens::radiusXs);
        }
        else
        {
            g.setColour (Tokens::lineControl);
            g.drawRoundedRectangle (led.reduced (0.5f), Tokens::radiusXs - 0.5f, 1.0f);
        }

        type::draw (g, "LIMIT", led, type::label(), limitShown ? Tokens::ground : Tokens::inkSubtle,
                    juce::Justification::centred);
    }

    // --- What the number is, and the number.
    const auto labelBox = juce::Rectangle<float> (0.0f, kWellHeight + 4.0f, kColumnWidth, 12.0f);
    type::draw (g, "RMS", labelBox, type::label(), Tokens::inkMuted, juce::Justification::centred);
    type::draw (g, dbText (shownDb), valueBox(), type::value(), Tokens::ink, juce::Justification::centred);
}

} // namespace horizon::ui
