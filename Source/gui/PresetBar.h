#pragma once

#include "HorizonLookAndFeel.h"
#include "Motion.h"

class HorizonPadAudioProcessor;

namespace horizon::ui
{

/**
    The preset row: one pill button per factory preset, a dynamic row of the
    player's own saved presets (each a merged name+delete pill), a
    "+ Save preset" control that expands into an inline name field with
    Save/Cancel buttons, and a real A/B compare pair in its own rounded
    group - switching slots recalls that slot's own independently-stored
    snapshot of all ten parameters, and the swap (⇄) button copies the
    active slot onto the other one. All backed by the processor's real,
    persistent state - see HorizonPadAudioProcessor's class-level
    thread-safety note for how the A/B buffers and the on-disk user-preset
    library work.

    The factory presets and saved pills can grow past the row's width (an
    unbounded, ever-growing user-preset library), so they scroll
    horizontally in their own Viewport with a soft fade at whichever edge
    has more content just out of view; "+ Save preset" and the A/B group
    stay in fixed slots outside that viewport so they're always reachable,
    never pushed off by how many presets exist.

    Since the UI review of 2026-10-17: arrows either side of the row step
    to the previous and next preset (factory, then your own) - the row shows
    six of seventeen at once; the active preset's pill dims to half its gold
    once the sound is edited away from it (isActivePresetModified()), and a
    click on it recalls it again; and no button here takes keyboard focus on
    a click, so the space bar keeps reaching the host's transport (Tab still
    reaches every one).

    Motion (advance(), one call per editor frame): the gold highlight fades
    from the old preset to the new one, the row scrolls the active preset
    into view when it changes from elsewhere (the host, a save), a saved
    preset's pill grows in and a deleted one shrinks out before it is
    removed, and the ⇄ copy flash fades instead of blinking off. Nothing
    repaints while nothing changes.
*/
class PresetBar final : public juce::Component
{
public:
    /** Imports a .bobpreset (Browse > Import, or a file dropped on the editor); shows why if refused. */
    void importPreset (const juce::File& file);

    explicit PresetBar (HorizonPadAudioProcessor&);
    ~PresetBar() override;

    void paint (juce::Graphics&) override;
    void paintOverChildren (juce::Graphics&) override;
    void resized() override;

    /** Re-reads the current program, user presets, and A/B state from the
        processor. Cheap enough to call every frame; repaints only what changed. */
    void refreshFromProcessor();

    /** One editor frame of the row's animations. */
    void advance (double now, float dt);

    /** The sky's colour behind the row (it changes with FILTER, see
        HorizonScene), for the scroll cue's fade-to-background. */
    void setBackdropColour (juce::Colour colour);

private:
    /** One user-saved preset: a single rounded pill containing a clickable
        name (applies the preset) and a small "x" (deletes it) - matching the
        design's up.wrapStyle/up.nameStyle/up.delStyle exactly. */
    class UserPresetPill final : public juce::Component
    {
    public:
        explicit UserPresetPill (juce::String presetName);

        void paint (juce::Graphics&) override;
        void resized() override;

        void setActive (bool shouldBeActive, bool edited) noexcept { active = shouldBeActive; modified = edited; }
        void snapHighlight();

        void startGrowing();
        void startShrinking();
        bool isShrinking() const noexcept { return shrinking; }
        bool hasShrunk() const noexcept { return shrinking && presence.isSettled(); }

        /** Returns true if the pill's width moved (the row needs laying out). */
        bool advance (float dt);

        int preferredWidth() const;
        int currentWidth() const;
        float getPresence() const noexcept { return presence.value; }

        const juce::String name;
        std::function<void()> onApply, onDelete;

    private:
        void applyHighlight();

        // Matches MacrosPanel's secondary (FILTER/REVERB) knob size, so the
        // delete "x" reads as a properly-sized control instead of a cramped
        // corner afterthought.
        static constexpr int kDeleteButtonWidth = 34;

        bool active = false, modified = false, shrinking = false;
        motion::Follower highlight;    // 0..1, the gold
        motion::Spring presence;       // 0..1, width and opacity while growing/shrinking
        juce::TextButton nameButton, deleteButton;
    };

    UserPresetPill* addUserPresetPill (const juce::String& name);
    void syncUserPresetPills();
    void beginSavingNewPreset();
    void commitSavingNewPreset();
    void cancelSavingNewPreset();
    void layOutScrollContent();

    juce::Component* activePresetComponent() const;
    void scrollToShow (juce::Component* target, bool animate);
    void applyFactoryHighlight (int index);
    void applySwapFlash();
    void stepPreset (int delta);

    HorizonPadAudioProcessor& processor;

    // The factory presets and saved-preset pills can outgrow the row's
    // width, so they live inside a horizontal Viewport instead of being laid
    // out directly on PresetBar - unlike them, "+ Save preset" and the A/B
    // group are fixed, always-visible slots outside it (see resized()).
    juce::Viewport presetViewport;
    juce::Component presetScrollContent;

    juce::OwnedArray<juce::TextButton> presetButtons;
    std::vector<motion::Follower> factoryHighlights;
    juce::OwnedArray<UserPresetPill> userPresetPills;

    juce::TextButton saveButton { "+ Save preset" };

    // "Browse": every preset by category, your own apart, import and export of
    // .bobpreset files (D.6.5, audio-plugins.md 8).
    juce::TextButton browseButton { "Browse" };
    std::unique_ptr<juce::FileChooser> fileChooser;
    void showBrowseMenu();
    void importPresetFile();
    void exportPresetFile();
    juce::TextEditor saveNameEditor;
    juce::TextButton saveConfirmButton { "Save" };
    juce::TextButton saveCancelButton { "Cancel" };

    juce::TextButton slotAButton { "A" };
    juce::TextButton slotBButton { "B" };
    juce::TextButton swapButton { juce::String::fromUTF8 ("\xe2\x87\x84") }; // unicode left-right arrows

    juce::TextButton previousButton { juce::String::fromUTF8 ("\xe2\x80\xb9") };   // single angle quotes
    juce::TextButton nextButton { juce::String::fromUTF8 ("\xe2\x80\xba") };
    bool activeModified = false;

    // --- Motion state.
    int shownPresetKind = -1, shownPresetIndex = -1;
    motion::Spring scroll;
    bool scrolling = false, needsInitialScroll = false;
    int lastScrollX = 0;
    motion::Follower swapFlash;
    bool swapFlashTimed = false;
    double swapFlashHoldUntil = 0.0;
    juce::Colour backdrop { 0xff71603f };   // the designed sky at this row (FILTER 50%)

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PresetBar)
};

} // namespace horizon::ui
