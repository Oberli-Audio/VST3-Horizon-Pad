#include "FooterBar.h"
#include "../PluginProcessor.h"

namespace horizon::ui
{

namespace
{
    const juce::String dot = juce::String::fromUTF8 (" \xc2\xb7 ");

    juce::String sizeText (float scale)
    {
        return "SIZE " + juce::String (juce::roundToInt (scale * 100.0f)) + "%";
    }

    juce::String formatName (juce::AudioProcessor::WrapperType type)
    {
        return type == juce::AudioProcessor::wrapperType_Standalone ? "STANDALONE" : "VST3";
    }
}

//==============================================================================
int FooterBar::SizeButton::preferredWidth() const
{
    return juce::roundToInt (juce::GlyphArrangement::getStringWidth (labelFont (12.5f, true), sizeText (1.25f) + " v")) + 6;
}

void FooterBar::SizeButton::paintButton (juce::Graphics& g, bool highlighted, bool down)
{
    // Footer text that answers a click: gold under the mouse or the keyboard.
    const auto lit = highlighted || down || hasKeyboardFocus (false);
    g.setColour (lit ? Palette::gold : Palette::textFooter);
    g.setFont (labelFont (12.5f, true));

    auto r = getLocalBounds();
    const auto caret = r.removeFromRight (10).toFloat().withSizeKeepingCentre (7.0f, 4.0f);
    g.drawText (sizeText (scale), r, juce::Justification::centredRight);

    juce::Path p;
    p.addTriangle (caret.getX(), caret.getY(), caret.getRight(), caret.getY(), caret.getCentreX(), caret.getBottom());
    g.fillPath (p);
}

//==============================================================================
int FooterBar::AboutButton::preferredWidth() const
{
    return juce::roundToInt (juce::GlyphArrangement::getStringWidth (labelFont (12.5f, true), text)) + 4;
}

void FooterBar::AboutButton::paintButton (juce::Graphics& g, bool highlighted, bool down)
{
    g.setColour (highlighted || down || hasKeyboardFocus (false) ? Palette::gold : Palette::textFooter);
    g.setFont (labelFont (12.5f, true));
    g.drawText (text, getLocalBounds(), juce::Justification::centredLeft);
}

//==============================================================================
FooterBar::FooterBar (HorizonPadAudioProcessor& processorToUse)
    : processor (processorToUse)
{
    aboutButton.setText ("HORIZON PAD " JucePlugin_VersionString + dot + formatName (processor.wrapperType));
    aboutButton.setTooltip ("About Horizon Pad: the version, the details to copy into a report, and the licences.");
    aboutButton.setMouseClickGrabsKeyboardFocus (false);
    aboutButton.onClick = [this] { if (onAboutClicked != nullptr) onAboutClicked(); };
    addAndMakeVisible (aboutButton);

    sizeButton.setTooltip ("Window size, 75 to 200% of the designed size; every Horizon Pad on this "
                           "computer opens at the size you chose last.");
    sizeButton.setMouseClickGrabsKeyboardFocus (false);
    sizeButton.onClick = [this] { showSizeMenu(); };
    addAndMakeVisible (sizeButton);
}

void FooterBar::showSizeMenu()
{
    juce::PopupMenu menu;

    for (auto scale : { 0.75f, 1.0f, 1.25f, 1.5f, 2.0f })
    {
        const auto current = std::abs (scale - processor.getEditorScale()) < 0.01f;
        menu.addItem (juce::String (juce::roundToInt (scale * 100.0f)) + "%", true, current,
                      [safe = juce::Component::SafePointer<FooterBar> (this), scale]
                      {
                          if (safe != nullptr && safe->onScaleChosen != nullptr)
                              safe->onScaleChosen (scale);
                      });
    }

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&sizeButton));
}

void FooterBar::refreshFromProcessor()
{
    const auto scale = processor.getEditorScale();

    if (! juce::approximatelyEqual (scale, shownScale))
    {
        shownScale = scale;
        sizeButton.setScale (scale);
    }
}

void FooterBar::resized()
{
    auto r = getLocalBounds().withTrimmedTop (1).reduced (0, 4);
    sizeButton.setBounds (r.removeFromRight (sizeButton.preferredWidth()));
    aboutButton.setBounds (r.removeFromLeft (aboutButton.preferredWidth()));
}

void FooterBar::paint (juce::Graphics& g)
{
    // No background/skyline of its own - the shared panel (HorizonScene)
    // already paints the gradient and mountain silhouette behind the whole
    // window; this strip only draws the top divider and the labels.
    auto bounds = getLocalBounds().toFloat();

    g.setColour (Palette::dividerColor);
    g.fillRect (bounds.removeFromTop (1.0f));

    auto r = bounds.reduced (0.0f, 4.0f).toNearestInt();

    g.setColour (Palette::textFooter);
    g.setFont (labelFont (12.5f, true));

    auto right = r.withTrimmedRight (sizeButton.getWidth() + 2);
    g.drawText ("4 LAYERS" + dot + "6 MACROS" + dot, right, juce::Justification::centredRight);

    // Split after each \xNN escape (as separate literals) so the compiler's
    // greedy hex-escape parsing can't swallow the following letters (e.g.
    // "\xa4be" would otherwise be read as one 4-digit escape, not \xa4 + "be").
    g.drawText (juce::String::fromUTF8 ("W\xc3\xa4" "g zom L\xc3\xa4" "be \xe2\x80\x94 a life of joy"),
                r, juce::Justification::centred);
}

} // namespace horizon::ui
