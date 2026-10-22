#pragma once

#include "HorizonLookAndFeel.h"

namespace horizon::ui
{

/** The About box (ux-ui-design-bob audio-plugins.md 14), opened from the
    footer's product label: the diagnostics block on screen - so a screenshot
    carries it - with one button that copies it as plain text, the Licences
    view with the bundled LICENSE and THIRD_PARTY_NOTICES.md (compiled in, not
    a URL), and the one trademark sentence. An overlay over the dimmed editor;
    Close, Escape or a click beside the card dismisses it. */
class AboutPanel final : public juce::Component
{
public:
    AboutPanel();

    /** Shows the panel with these diagnostics lines ("Label: value"). */
    void show (const juce::StringArray& diagnostics, bool openLicences = false);
    void hide();

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseUp (const juce::MouseEvent&) override;
    bool keyPressed (const juce::KeyPress&) override;

private:
    juce::Rectangle<int> cardBounds() const;
    void showLicences (bool shouldShow);

    juce::StringArray lines;
    bool licencesShown = false;

    juce::TextButton copyButton { "Copy diagnostics" }, licencesButton { "Licences" }, closeButton { "Close" };
    juce::TextEditor licencesText;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AboutPanel)
};

} // namespace horizon::ui
