#include "AboutPanel.h"

#include <DocsData.h>

namespace horizon::ui
{

namespace
{
    constexpr int kCardWidth = 560;
    constexpr int kCardHeight = 470;
    constexpr int kPad = 28;

    /** A document compiled into the plugin (CMakeLists: HorizonPadDocsData), by file name. */
    juce::String embedded (const juce::String& fileName)
    {
        for (int i = 0; i < DocsData::namedResourceListSize; ++i)
        {
            if (fileName == DocsData::getNamedResourceOriginalFilename (DocsData::namedResourceList[i]))
            {
                int size = 0;
                const auto* data = DocsData::getNamedResource (DocsData::namedResourceList[i], size);
                return juce::String::fromUTF8 (data, size);
            }
        }

        return {};
    }

    void styleButton (juce::TextButton& b)
    {
        b.setMouseClickGrabsKeyboardFocus (false);   // the space bar keeps reaching the host
        b.setColour (juce::TextButton::textColourOffId, Palette::text);
    }
}

AboutPanel::AboutPanel()
{
    setWantsKeyboardFocus (true);
    setInterceptsMouseClicks (true, true);

    for (auto* b : { &copyButton, &licencesButton, &closeButton })
    {
        styleButton (*b);
        addAndMakeVisible (*b);
    }

    copyButton.setTooltip ("Copies these lines as plain text, to paste into a message about a problem.");
    licencesButton.setTooltip ("The licence of Horizon Pad and of the parts it is built with.");

    copyButton.onClick = [this]
    {
        juce::SystemClipboard::copyTextToClipboard (lines.joinIntoString ("\n"));
        copyButton.setButtonText ("Copied");

        // Made here, not as an init-capture of the inner lambda: MSVC reads
        // `this` there as the outer lambda (C2440).
        juce::Component::SafePointer<AboutPanel> safe (this);
        juce::Timer::callAfterDelay (1500, [safe]
        {
            if (safe != nullptr)
                safe->copyButton.setButtonText ("Copy diagnostics");
        });
    };

    licencesButton.onClick = [this] { showLicences (! licencesShown); };
    closeButton.onClick = [this] { hide(); };

    licencesText.setMultiLine (true);
    licencesText.setReadOnly (true);
    licencesText.setScrollbarsShown (true);
    licencesText.setCaretVisible (false);
    licencesText.setFont (labelFont (12.0f));
    licencesText.setColour (juce::TextEditor::backgroundColourId, Palette::meterBg);
    licencesText.setColour (juce::TextEditor::textColourId, Palette::textValue);
    licencesText.setColour (juce::TextEditor::outlineColourId, Palette::cardBorder);
    licencesText.setText (embedded ("LICENSE") + "\n\n" + embedded ("THIRD_PARTY_NOTICES.md"), false);
    addChildComponent (licencesText);
}

void AboutPanel::show (const juce::StringArray& diagnostics, bool openLicences)
{
    lines = diagnostics;
    showLicences (openLicences);
    setVisible (true);
    toFront (false);
    grabKeyboardFocus();
    repaint();
}

void AboutPanel::hide()
{
    setVisible (false);
}

void AboutPanel::showLicences (bool shouldShow)
{
    licencesShown = shouldShow;
    licencesText.setVisible (shouldShow);
    licencesButton.setButtonText (shouldShow ? "Diagnostics" : "Licences");
    resized();
    repaint();
}

juce::Rectangle<int> AboutPanel::cardBounds() const
{
    return getLocalBounds().withSizeKeepingCentre (kCardWidth, kCardHeight);
}

void AboutPanel::resized()
{
    auto card = cardBounds().reduced (kPad);
    auto buttons = card.removeFromBottom (32);
    closeButton.setBounds (buttons.removeFromRight (90));
    buttons.removeFromRight (10);
    licencesButton.setBounds (buttons.removeFromRight (110));
    copyButton.setBounds (buttons.removeFromLeft (170));

    card.removeFromTop (64);          // title
    card.removeFromBottom (34);       // trademark line
    licencesText.setBounds (card);
}

void AboutPanel::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colours::black.withAlpha (0.55f));

    const auto card = cardBounds().toFloat();
    g.setColour (Palette::cardBg);
    g.fillRoundedRectangle (card, 12.0f);
    g.setColour (Palette::cardBorder);
    g.drawRoundedRectangle (card.reduced (0.5f), 12.0f, 1.0f);

    auto r = cardBounds().reduced (kPad);
    g.setColour (Palette::gold);
    g.setFont (titleFont (30.0f));
    g.drawText ("Horizon Pad", r.removeFromTop (38), juce::Justification::centredLeft);
    g.setColour (Palette::textDim);
    g.setFont (labelFont (12.5f));
    g.drawText ("Oberli Audio", r.removeFromTop (22), juce::Justification::centredLeft);

    r.removeFromBottom (32);   // buttons
    auto trademark = r.removeFromBottom (34);
    g.setColour (Palette::textDim);
    g.setFont (labelFont (11.0f));
    g.drawText ("VST is a registered trademark of Steinberg Media Technologies GmbH.", trademark,
                juce::Justification::centredLeft);

    if (licencesShown)
        return;

    // The diagnostics: label column in the dim ink, values in the value ink.
    r.removeFromTop (4);
    const auto rowHeight = juce::jmin (21, r.getHeight() / juce::jmax (1, lines.size()));

    for (const auto& line : lines)
    {
        auto row = r.removeFromTop (rowHeight);
        const auto label = line.upToFirstOccurrenceOf (":", true, false);
        const auto value = line.fromFirstOccurrenceOf (":", false, false).trim();
        g.setColour (Palette::textDim);
        g.setFont (labelFont (12.5f, true));
        g.drawText (label, row.removeFromLeft (140), juce::Justification::centredLeft);
        g.setColour (Palette::textValue);
        g.setFont (labelFont (13.0f));
        g.drawText (value, row, juce::Justification::centredLeft, true);
    }
}

void AboutPanel::mouseUp (const juce::MouseEvent& e)
{
    if (! cardBounds().contains (e.getPosition()))
        hide();
}

bool AboutPanel::keyPressed (const juce::KeyPress& key)
{
    if (key == juce::KeyPress::escapeKey)
    {
        hide();
        return true;
    }

    return false;
}

} // namespace horizon::ui
