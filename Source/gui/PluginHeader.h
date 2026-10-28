#pragma once

#include "Buttons.h"

class HorizonPadAudioProcessor;

namespace horizon::ui
{

/**
    The PluginHeader card, 48 px of `surface-raised` with a `line` below:
    the name-only Oberli mark (18 px high), a hairline, "Horizon Pad" in
    `plugin-name`; on the right the A/B segmented control and its copy
    button (owner 2026-10-28: A/B stays in the header), the preset pill,
    the version in `meta` / `ink-subtle`, and the ghost menu button.

    The preset pill (`radius-pill`, `line-control` outline, `surface-sunken`
    fill) steps through the presets with its arrows - the factory bank in
    presets.csv's order, then your own, wrapping round - and opens the
    preset drawer when you click the name; its border turns `ink` while the
    drawer is open. An edited preset (isActivePresetModified(), compared,
    not tracked) shows " *" in `accent-ink`.

    A and B each keep a full snapshot of the ten parameters; the copy
    button copies the one you hear onto the other, and the editor confirms
    it in a readout ("Copied A to B"), since nothing else on screen changes.

    No button takes keyboard focus on a click, so the space bar keeps
    reaching the host; Tab reaches every one, with the 2 px `ink` ring.
    refresh() runs every editor frame and repaints only what changed.
*/
class PluginHeader final : public juce::Component
{
public:
    static constexpr int kHeight = 48;

    explicit PluginHeader (HorizonPadAudioProcessor&);
    ~PluginHeader() override;

    std::function<void()> onMenuClicked, onPresetNameClicked;
    std::function<void (const juce::String& message, juce::Rectangle<int> anchorInHeader)> onCopied;

    /** Re-reads the preset, the edited mark and A/B from the processor. */
    void refresh();

    void setDrawerOpen (bool isOpen);
    void setMenuOpen (bool isOpen);

    /** The controls that open the overlays: a click on them toggles their
        overlay rather than counting as a click outside it. */
    juce::Component& getMenuButton() noexcept;
    juce::Component& getPresetNameButton() noexcept;

    void paint (juce::Graphics&) override;
    void paintOverChildren (juce::Graphics&) override;
    void resized() override;

private:
    class SegmentButton;
    class PresetNameButton;

    void stepPreset (int delta);

    HorizonPadAudioProcessor& processor;
    std::unique_ptr<juce::Drawable> mark;

    std::unique_ptr<SegmentButton> slotA, slotB;
    IconButton copyButton { icons::Id::copy, "Copy A to B" };
    IconButton previousButton { icons::Id::chevronLeft, "Previous preset", true };
    IconButton nextButton { icons::Id::chevronRight, "Next preset", true };
    IconButton menuButton { icons::Id::menu, "Menu" };
    std::unique_ptr<PresetNameButton> presetName;

    juce::Rectangle<float> markBounds;
    juce::Rectangle<int> dividerBounds, nameBounds, abBounds, pillBounds, versionBounds;
    bool drawerOpen = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PluginHeader)
};

} // namespace horizon::ui
