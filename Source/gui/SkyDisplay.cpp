#include "SkyDisplay.h"

namespace horizon::ui
{

namespace
{
    // Preset title: fade in, stay, fade out; a quick fade-out when another
    // recall replaces it before it is gone.
    constexpr double kTitleHoldSeconds = 4.0;
    constexpr float kTitleFadeIn = 0.18f;
    constexpr float kTitleFadeOut = 0.45f;
    constexpr float kTitleSwapOut = 0.06f;
    constexpr float kTitleDrift = 4.0f;       // px it rises while fading in

    constexpr float kTitleLeft = 24.0f, kTitleTop = 20.0f;
    constexpr float kTitleHeight = 28.0f + 4.0f + 19.0f;   // name (title 22/28), gap, description (small 13/19)
    constexpr int kShadowReach = 12;                       // the title's shadow: 10 px blur, 1 px down
}

SkyDisplay::SkyDisplay()
{
    setOpaque (true);
    setInterceptsMouseClicks (false, false);
    setTitle ("Sky");
    setDescription ("Follows FILTER (the time of day), REVERB (the haze) and WIDTH (the glow).");
}

void SkyDisplay::advanceScene (float dt)
{
    switch (scene.advance (dt))
    {
        case HorizonScene::Change::all:   repaint(); break;
        case HorizonScene::Change::lower: repaint (HorizonScene::lowerRegion (getLocalBounds())); break;
        case HorizonScene::Change::none:  break;
    }
}

void SkyDisplay::showPresetTitle (const juce::String& name, const juce::String& description)
{
    if (titleAlpha.value > 0.0f)
    {
        pendingName = name;
        pendingDescription = description;
        hasPending = true;
    }
    else
    {
        titleName = name;
        titleDescription = description;
        hasPending = false;
    }

    startHold = true;
}

juce::Rectangle<int> SkyDisplay::titleArea() const
{
    return juce::Rectangle<float> (kTitleLeft, kTitleTop, (float) getWidth() - 2.0f * kTitleLeft, kTitleHeight + kTitleDrift)
               .getSmallestIntegerContainer().expanded (kShadowReach + 2);
}

void SkyDisplay::renderTitle (float scale)
{
    const auto area = titleArea();
    titleImage = juce::Image (juce::Image::ARGB, juce::roundToInt ((float) area.getWidth() * scale),
                              juce::roundToInt ((float) area.getHeight() * scale), true);
    titleImageScale = scale;
    titleImageText = titleName + "\n" + titleDescription;

    juce::Graphics g (titleImage);
    g.addTransform (juce::AffineTransform::scale (scale).translated (-(float) area.getX() * scale, -(float) area.getY() * scale));

    const auto width = (float) getWidth() - 2.0f * kTitleLeft;
    juce::GlyphArrangement glyphs;
    glyphs.addFittedText (type::title(), titleName, kTitleLeft, kTitleTop, width, 28.0f, juce::Justification::centredLeft, 1);

    if (titleDescription.isNotEmpty())
        glyphs.addFittedText (type::small(), titleDescription, kTitleLeft, kTitleTop + 28.0f + 4.0f, width, 19.0f,
                              juce::Justification::centredLeft, 1);

    juce::Path outline;
    glyphs.createPath (outline);
    juce::DropShadow (Tokens::ground.withAlpha (0.65f), kShadowReach - 2, { 0, 1 }).drawForPath (g, outline);

    g.setColour (Tokens::ink);
    glyphs.draw (g);
}

void SkyDisplay::advanceTitle (double now, float dt)
{
    if (startHold)
    {
        titleHoldUntil = now + kTitleHoldSeconds;
        startHold = false;
    }

    const auto showing = ! hasPending && titleName.isNotEmpty() && now < titleHoldUntil;

    if (titleAlpha.advance (showing ? 1.0f : 0.0f, dt, kTitleFadeIn,
                            hasPending ? kTitleSwapOut : kTitleFadeOut, 2.0e-3f))
        repaint (titleArea());

    if (hasPending && titleAlpha.value <= 0.0f)
    {
        titleName = pendingName;
        titleDescription = pendingDescription;
        hasPending = false;
    }
}

void SkyDisplay::paint (juce::Graphics& g)
{
    scene.paint (g, getLocalBounds().toFloat());

    const auto alpha = titleAlpha.value;

    if (alpha <= 0.002f || titleName.isEmpty())
        return;

    const auto scale = juce::jlimit (1.0f, 4.0f, g.getInternalContext().getPhysicalPixelScaleFactor());

    if (titleImage.isNull() || ! juce::approximatelyEqual (scale, titleImageScale)
        || titleImageText != titleName + "\n" + titleDescription)
        renderTitle (scale);

    // Rises a few pixels as it fades in; whole device pixels, so the copy stays 1:1.
    const auto area = titleArea().toFloat();
    const auto drift = std::round ((1.0f - alpha) * kTitleDrift * scale) / scale;

    juce::Graphics::ScopedSaveState save (g);
    g.setOpacity (alpha);
    g.drawImageTransformed (titleImage, juce::AffineTransform::scale (1.0f / scale).translated (area.getX(), area.getY() + drift));
}

} // namespace horizon::ui
