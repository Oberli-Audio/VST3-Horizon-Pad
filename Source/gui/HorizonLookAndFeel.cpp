#include "HorizonLookAndFeel.h"

namespace horizon::ui
{

HorizonLookAndFeel::HorizonLookAndFeel()
{
    setColour (juce::ResizableWindow::backgroundColourId, Palette::background);
    setColour (juce::Label::textColourId,                 Palette::text);
    setColour (juce::Slider::rotarySliderFillColourId,    Palette::layerAccents[0]);
    setColour (juce::Slider::rotarySliderOutlineColourId, Palette::knobTrack);
    setColour (juce::Slider::trackColourId,               Palette::cardBg);
    setColour (juce::Slider::thumbColourId,               Palette::gold);
    setColour (juce::Slider::textBoxTextColourId,         Palette::textDim);
    setColour (juce::Slider::textBoxOutlineColourId,      juce::Colours::transparentBlack);
    setColour (juce::TextButton::buttonColourId,          juce::Colours::transparentBlack);
    setColour (juce::TextButton::buttonOnColourId,        Palette::pillActiveBg);
    setColour (juce::TextButton::textColourOffId,         Palette::text);
    setColour (juce::TextButton::textColourOnId,          Palette::gold);
    setColour (juce::TooltipWindow::backgroundColourId,   Palette::cardBg);
}

juce::Font HorizonLookAndFeel::getLabelFont (juce::Label& label)
{
    return labelFont ((float) juce::jmax (10, label.getHeight() - 2));
}

juce::Font HorizonLookAndFeel::getTextButtonFont (juce::TextButton&, int buttonHeight)
{
    return labelFont (juce::jlimit (10.0f, 17.0f, (float) buttonHeight * 0.5f), true);
}

void HorizonLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& button,
                                               const juce::Colour& backgroundColour,
                                               bool shouldDrawButtonAsHighlighted,
                                               bool shouldDrawButtonAsDown)
{
    auto bounds = button.getLocalBounds().toFloat().reduced (0.5f);
    const auto corner = bounds.getHeight() * 0.5f; // full pill, matching the design's preset/tab buttons

    auto fill = backgroundColour;

    if (shouldDrawButtonAsDown)
        fill = fill.brighter (0.25f);
    else if (shouldDrawButtonAsHighlighted)
        fill = fill.brighter (0.12f);

    if (fill.getFloatAlpha() > 0.001f)
    {
        g.setColour (fill);
        g.fillRoundedRectangle (bounds, corner);
    }

    const auto& props = button.getProperties();

    // A preset pill whose highlight PresetBar fades in and out: the gold
    // fill and border follow "activeAmount" (0..1) instead of snapping with
    // the toggle state, which stays the truth for accessibility. (PresetBar
    // gives these pills a transparent buttonOnColourId, so the fill above
    // never doubles this one.)
    if (props.contains ("activeAmount"))
    {
        const auto amount = juce::jlimit (0.0f, 1.0f, (float) props["activeAmount"]);

        if (amount > 0.001f)
        {
            // A card-coloured shade first, then the gold tint: the sky behind can be
            // golden-hour bright, and the cream name must stay readable (EX-010).
            g.setColour (Palette::pillActiveShade.withMultipliedAlpha (amount));
            g.fillRoundedRectangle (bounds, corner);

            auto activeFill = Palette::pillActiveBg.withMultipliedAlpha (amount);

            if (shouldDrawButtonAsDown)
                activeFill = activeFill.brighter (0.25f);
            else if (shouldDrawButtonAsHighlighted)
                activeFill = activeFill.brighter (0.12f);

            g.setColour (activeFill);
            g.fillRoundedRectangle (bounds, corner);
        }

        g.setColour (Palette::pillBorder.interpolatedWith (Palette::gold, amount));
        g.drawRoundedRectangle (bounds, corner, 1.0f);
        return;
    }

    if ((bool) props.getWithDefault ("noBorder", false))
        return;

    juce::Colour borderColour = button.getToggleState() ? Palette::gold : Palette::pillBorder;

    if (! button.getToggleState() && props.contains ("borderColour"))
        borderColour = juce::Colour ((juce::uint32) (int) props["borderColour"]);

    if ((bool) props.getWithDefault ("dashedBorder", false) && ! button.getToggleState())
    {
        juce::Path pillPath;
        pillPath.addRoundedRectangle (bounds, corner);

        juce::Path dashed;
        const float dashLengths[] { 4.0f, 3.0f };
        juce::PathStrokeType (1.0f).createDashedStroke (dashed, pillPath, dashLengths, 2);
        g.setColour (borderColour);
        g.fillPath (dashed);
        return;
    }

    g.setColour (borderColour);
    g.drawRoundedRectangle (bounds, corner, 1.0f);
}

void HorizonLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height,
                                           float sliderPos, float rotaryStartAngle, float rotaryEndAngle,
                                           juce::Slider& slider)
{
    // Every knob in the editor is an AnimatedKnob, which paints itself with
    // drawRingKnob() from its eased value; this covers any plain Slider.
    drawRingKnob (g, juce::Rectangle<int> (x, y, width, height).toFloat(), sliderPos,
                  rotaryStartAngle, rotaryEndAngle,
                  slider.findColour (juce::Slider::rotarySliderFillColourId),
                  slider.findColour (juce::Slider::rotarySliderOutlineColourId));
}

void drawRingKnob (juce::Graphics& g, juce::Rectangle<float> area, float proportion,
                   float rotaryStartAngle, float rotaryEndAngle, juce::Colour accent, juce::Colour trackColour,
                   float hover, float drag)
{
    // Ring + dot style, matching the design handoff's ringKnob() helper: a
    // thin dark track ring, an accent-coloured ring lit up to the current
    // value, a dark cap, and a small glowing dot marking the exact value
    // position - not a thick glowing filled arc (an earlier iteration of
    // this look and feel).
    auto bounds = area.reduced (2.0f);
    const auto size = juce::jmin (bounds.getWidth(), bounds.getHeight());
    bounds = bounds.withSizeKeepingCentre (size, size);

    const auto centre = bounds.getCentre();
    const auto arcThickness = juce::jmax (2.0f, size * 0.1f);
    const auto arcRadius = size * 0.5f - arcThickness * 0.5f;
    const auto sliderPos = juce::jlimit (0.0f, 1.0f, proportion);
    const auto angle = rotaryStartAngle + sliderPos * (rotaryEndAngle - rotaryStartAngle);

    const juce::PathStrokeType ringStroke (arcThickness, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);

    // --- Track (the full 270 degree sweep, dark). Hovering warms it a
    // little toward the knob's colour.
    {
        juce::Path track;
        track.addCentredArc (centre.x, centre.y, arcRadius, arcRadius, 0.0f,
                             rotaryStartAngle, rotaryEndAngle, true);
        g.setColour (trackColour.interpolatedWith (accent, 0.14f * hover));
        g.strokePath (track, ringStroke);
    }

    // --- Value ring: lit from the start up to the current value, same
    // thickness as the track (a ring reading as "how full", not a fader).
    // While dragged, a soft wider halo of the same arc lights it up.
    if (sliderPos > 0.001f)
    {
        juce::Path value;
        value.addCentredArc (centre.x, centre.y, arcRadius, arcRadius, 0.0f,
                             rotaryStartAngle, angle, true);

        if (drag > 0.001f)
        {
            g.setColour (accent.withAlpha (0.20f * drag));
            g.strokePath (value, juce::PathStrokeType (arcThickness * 2.4f, juce::PathStrokeType::curved,
                                                       juce::PathStrokeType::rounded));
        }

        g.setColour (accent.brighter (0.12f * juce::jmax (hover, drag)));
        g.strokePath (value, ringStroke);
    }

    // --- Dark cap (mostly empty centre - the design's knobs read as rings,
    // not solid dials).
    const auto capRadius = arcRadius - arcThickness * 1.0f;

    if (capRadius > 2.0f)
    {
        const auto capBounds = juce::Rectangle<float> (capRadius * 2.0f, capRadius * 2.0f)
                                   .withCentre (centre);

        g.setColour (Palette::knobInner);
        g.fillEllipse (capBounds);
    }

    // --- Glowing dot at the current value's position along the ring; its
    // glow brightens on hover and swells while dragged.
    {
        const auto dotCentre = centre.getPointOnCircumference (arcRadius, angle);
        const auto dotRadius = juce::jmax (2.5f, size * 0.057f);
        const auto haloDiameter = dotRadius * (3.2f + 1.4f * drag);

        g.setColour (accent.withAlpha (0.30f + 0.12f * hover + 0.20f * drag));
        g.fillEllipse (juce::Rectangle<float> (haloDiameter, haloDiameter).withCentre (dotCentre));

        g.setColour (accent);
        g.fillEllipse (juce::Rectangle<float> (dotRadius * 2.0f, dotRadius * 2.0f).withCentre (dotCentre));
    }
}

void HorizonLookAndFeel::drawLinearSlider (juce::Graphics& g, int x, int y, int width, int height,
                                           float sliderPos, float minSliderPos, float maxSliderPos,
                                           const juce::Slider::SliderStyle style, juce::Slider& slider)
{
    // A flat track with a solid pill thumb - matching the design's
    // pitchTrackStyle/pitchThumbStyle exactly (no fill-from-rest bar).
    juce::ignoreUnused (minSliderPos, maxSliderPos, style);

    auto bounds = juce::Rectangle<int> (x, y, width, height).toFloat();
    const auto trackWidth = juce::jmin (bounds.getWidth(), 24.0f);
    auto track = bounds.withSizeKeepingCentre (trackWidth, bounds.getHeight());

    g.setColour (Palette::cardBg);
    g.fillRoundedRectangle (track, trackWidth * 0.5f);
    g.setColour (Palette::pillBorder);
    g.drawRoundedRectangle (track.reduced (0.5f), trackWidth * 0.5f, 1.0f);

    // Thumb: a rounded pill, sized and positioned exactly as the design's
    // 20x24 thumb inside a 24-wide track (2px margin each side).
    const auto thumbWidth = trackWidth * 0.83f;
    const auto thumbHeight = 24.0f;
    auto thumb = juce::Rectangle<float> (thumbWidth, thumbHeight)
                    .withCentre ({ bounds.getCentreX(), sliderPos });

    const auto accent = slider.findColour (juce::Slider::thumbColourId);

    g.setColour (accent.withAlpha (0.5f));
    g.fillRoundedRectangle (thumb.expanded (3.0f), (thumbHeight + 6.0f) * 0.5f);

    g.setColour (accent);
    g.fillRoundedRectangle (thumb, thumbHeight * 0.28f);
}

void drawPanel (juce::Graphics& g, juce::Rectangle<float> bounds, juce::Colour fill, float corner)
{
    g.setColour (fill);
    g.fillRoundedRectangle (bounds, corner);

    g.setColour (Palette::cardBorder);
    g.drawRoundedRectangle (bounds.reduced (0.5f), corner, 1.0f);
}

ColumnSlots computeColumnSlots (juce::Rectangle<int> cardBounds)
{
    auto r = cardBounds.reduced (12, 16);

    ColumnSlots slots;
    slots.icon = r.removeFromTop (30);
    r.removeFromTop (6);
    slots.label = r.removeFromTop (20);
    r.removeFromTop (6);
    slots.caption = r.removeFromTop (32);
    r.removeFromTop (6);
    slots.value = r.removeFromBottom (22);
    r.removeFromBottom (6);
    slots.control = r;

    return slots;
}

void drawColumnCard (juce::Graphics& g, juce::Rectangle<float> bounds)
{
    g.setColour (Palette::cardBg);
    g.fillRoundedRectangle (bounds, 14.0f);

    g.setColour (Palette::cardBorder);
    g.drawRoundedRectangle (bounds.reduced (0.5f), 14.0f, 1.0f);
}

ControlLayout computePadControlLayout (juce::Rectangle<int> area)
{
    constexpr int kLabel = 14, kGapAbove = 4, kKnob = 78, kGapBelow = 5, kValue = 22;
    constexpr int kGroup = kLabel + kGapAbove + kKnob + kGapBelow + kValue;

    area = area.withSizeKeepingCentre (area.getWidth(), juce::jmin (area.getHeight(), kGroup));

    ControlLayout c;
    c.label = area.removeFromTop (kLabel);
    area.removeFromTop (kGapAbove);
    c.control = area.removeFromTop (kKnob);
    area.removeFromTop (kGapBelow);
    c.value = area.removeFromTop (kValue);
    return c;
}

MacroGridLayout computeMacroGridLayout (juce::Rectangle<int> area)
{
    // In the 214px control area of a 346px grid card this leaves 66px rows
    // and 37px knobs - smaller than the 42px of the old 2x2 grid, the price
    // of six macros in the card the design gave four.
    constexpr int kLabel = 11, kGap = 2, kValue = 14, kDividerGap = 3;

    MacroGridLayout m;
    area.removeFromTop (2);

    const auto rowHeight = (area.getHeight() - 2 * (2 * kDividerGap + 1)) / 3;
    m.knobSize = juce::jlimit (28, 42, rowHeight - kLabel - kValue - 2 * kGap);

    for (size_t i = 0; i < m.rows.size(); ++i)
    {
        auto row = area.removeFromTop (rowHeight);
        auto& r = m.rows[i];
        r.label = row.removeFromTop (kLabel);
        row.removeFromTop (kGap);
        r.value = row.removeFromBottom (kValue);
        row.removeFromBottom (kGap);
        r.control = row;

        if (i + 1 < m.rows.size())
        {
            area.removeFromTop (kDividerGap);
            m.dividerYs[i] = area.getY();
            area.removeFromTop (1 + kDividerGap);
        }
    }

    return m;
}

} // namespace horizon::ui
