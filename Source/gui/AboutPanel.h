#pragma once

#include "Buttons.h"
#include "Motion.h"

namespace horizon::ui
{

/**
    About is the back of the panel (AboutBack card, sketch E): "About
    Horizon Pad" in the menu turns the whole window to a page in the other
    theme - bone behind the charcoal instrument - with a 150 ms cross-fade.
    A bar across the top holds "Back to the panel". Left: the Oberli Audio
    lockup, the version line, the name, the brand line "Turn it and listen.",
    what Horizon Pad is, its own tagline (owner 2026-10-28: "Wäg zom Läbe -
    a life of joy" moved here from the old footer), Copy details and
    Licences, and the trademark sentence. Right: the DETAILS card - the
    diagnostics block (plugin-ui-bob audio-plugins.md 14, ship-bob
    feedback-loop.md 3), on screen so a screenshot carries it - or, after
    Licences, the bundled LICENSE and THIRD_PARTY_NOTICES.md (compiled in,
    never a URL).

    Esc or "Back to the panel" turns it back.
*/
class AboutPanel final : public juce::Component
{
public:
    AboutPanel();
    ~AboutPanel() override;

    std::function<void()> onClosed;

    /** Turns the panel round, showing these diagnostics lines ("Label: value"). */
    void show (const juce::StringArray& diagnostics, bool openLicences = false);
    void hide();

    /** One editor frame: the cross-fade. */
    void advance (float dt);

    void paint (juce::Graphics&) override;
    void paintOverChildren (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;

private:
    void showLicences (bool shouldShow);

    juce::StringArray lines;
    bool licencesShown = false, closing = false;
    motion::Follower opacity;

    std::unique_ptr<juce::Drawable> lockup;

    ActionButton backButton, copyButton, licencesButton;
    juce::TextEditor licencesText;

    juce::Rectangle<int> bar, left, detailsCard, lockupBounds;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AboutPanel)
};

} // namespace horizon::ui
