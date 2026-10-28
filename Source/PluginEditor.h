#pragma once

#include "PluginProcessor.h"

#include "gui/HorizonLookAndFeel.h"
#include "gui/PluginHeader.h"
#include "gui/SkyDisplay.h"
#include "gui/ControlStrip.h"
#include "gui/HeaderMenu.h"
#include "gui/PresetDrawer.h"
#include "gui/SavePresetCard.h"
#include "gui/AboutPanel.h"
#include "gui/ReadoutBubble.h"

/**
    The editor, sketch F of the Oberli Audio redraw ("sky over the strip",
    owner's pick 2026-10-27; docs/editor-f-brief.md): the Charcoal theme
    with the mango accent and Rubik, flat chrome, and the knob drawn like the
    O of the logo. Top to bottom, 1160 x 548 at 100 %:

      - the PluginHeader (48 px): the Oberli mark, "Horizon Pad", A/B and
        copy, the preset pill, the version, the menu button;
      - 28 px in, the Scene - today's sky (HorizonScene: FILTER the time of
        day, REVERB the haze, WIDTH the glow) as a 1104 x 294 display, where
        a recalled preset's name fades in at the top-left;
      - 24 px below it, one row of controls (ControlStrip): WHEELS, PADS,
        MACROS, OUTPUT.

    Overlays never open windows: the menu drops from the header
    (HeaderMenu), the presets slide in from the left (PresetDrawer), "Save
    preset…" is a small card (SavePresetCard), About turns the panel round
    (AboutPanel). Esc or a click elsewhere closes the open one. There is no
    on-screen keyboard: play from a MIDI keyboard or controller.

    One layout, drawn at a chosen size: the menu's GUI scale (75, 100, 125,
    150, 200 %) scales the whole editor with a transform on top of the
    host's own scale (setScaleFactor() keeps the two apart; EX-008). A
    machine-local preference every instance opens at, never in the session
    (HorizonPadAudioProcessor::getEditorScale()). Everything is vector, or
    cached at the screen's pixel density, so no size blurs.

    Motion. Things move because the sound changed, and only then:
      - the sky follows FILTER, REVERB and WIDTH;
      - knobs glide after a preset recall or A/B switch, in a left-to-right
        wave, while a hand on a knob is followed at once (AnimatedKnob);
      - a recalled preset's name and description fade in on the sky;
      - each pad's level bar follows its layer's real output;
      - the first time the editor opens on an instance, the sky rises from
        night and the knobs sweep up to their values; reopening the window
        shows it as it is. (The Oberli mark is never animated.)
    One frame callback (advanceFrame) drives it all, from the display's
    vertical blank while the editor is on screen, capped near 60 fps; each
    piece repaints only its own changed area, so a quiet editor costs
    nothing.
*/
class HorizonPadAudioProcessorEditor final : public juce::AudioProcessorEditor,
                                             public juce::FileDragAndDropTarget,
                                             public horizon::ui::ReadoutHost,
                                             private juce::Timer
{
public:
    explicit HorizonPadAudioProcessorEditor (HorizonPadAudioProcessor&);
    ~HorizonPadAudioProcessorEditor() override;

    /** Turns the panel round to About (the menu; the snapshot tool too). */
    void showAbout (bool openLicences = false);

    /** Opens the menu, and with `scaleSubmenu` its GUI scale submenu. */
    void showMenu (bool scaleSubmenu = false);

    void showPresetDrawer();
    void showSavePresetCard();

    /** Fades the active preset's name and description in on the sky, as a
        recall does (and tools/gui_snapshot's --title, at any FILTER). */
    void showRecalledPresetTitle();

    /** Shows a knob's drag readout without a mouse (tools/gui_snapshot).
        Returns false for an unknown parameter ID. */
    bool previewDrag (const juce::String& parameterId);

    // A .oberlipreset (or an older .bobpreset / .quellepreset) dropped on the editor is imported (D.6.5).
    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void filesDropped (const juce::StringArray& files, int x, int y) override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;

    /** The host's own scale (Windows per-monitor DPI, some Linux hosts); the
        player's GUI scale is multiplied on top. */
    void setScaleFactor (float newScale) override;

    /** One animation frame at `nowSeconds` (Time::getMillisecondCounterHiRes()
        timebase): polls the processor and advances every animation. Driven by
        the vertical blank; public so tools/gui_snapshot can step it offline
        with a clock of its own. */
    void advanceFrame (double nowSeconds);

    // ReadoutHost: a knob's value above its cap while it is dragged.
    void showDragReadout (juce::Component& source, juce::Rectangle<float> capInSource, const juce::String& text) override;
    void hideDragReadout() override;

    static constexpr int kWindowWidth = 1160;
    static constexpr int kWindowHeight = 548;

private:
    void timerCallback() override;

    void pullSceneTargets();
    void applyScale();

    void closeOverlays();
    void toggleMenu();
    void togglePresetDrawer();
    void importPresetFile();
    void importPreset (const juce::File&);
    void exportPresetFile();
    void reportProblem();
    void showToast (const juce::String& text, juce::Rectangle<int> anchor);

    static double clockNow() noexcept { return juce::Time::getMillisecondCounterHiRes() * 0.001; }

    static constexpr int kBodyPadding = 28;
    static constexpr int kSkyHeight = 294;
    static constexpr int kSkyToStrip = 24;

    // AudioProcessorEditor's own `processor` member is the plain
    // AudioProcessor; the frame callback needs the meters and recall counters.
    HorizonPadAudioProcessor& horizonProcessor;
    std::atomic<float>* filterParam = nullptr;
    std::atomic<float>* reverbParam = nullptr;
    std::atomic<float>* widthParam = nullptr;

    horizon::ui::HorizonLookAndFeel lookAndFeel;

    horizon::ui::PluginHeader header;
    horizon::ui::SkyDisplay sky;
    horizon::ui::ControlStrip strip;

    horizon::ui::PresetDrawer drawer;
    horizon::ui::HeaderMenu menu;
    horizon::ui::SavePresetCard saveCard;
    horizon::ui::AboutPanel about;
    horizon::ui::ReadoutBubble dragReadout, toast;

    // Without a TooltipWindow somewhere in the tree, setTooltip() text is
    // stored but never shown. 600 ms, as the Tooltip card says.
    juce::TooltipWindow tooltipWindow;

    std::unique_ptr<juce::FileChooser> fileChooser;

    // --- Frame state.
    double lastFrameTime = -1.0;
    double lastVBlankTime = -1.0;
    float sceneTime = 0.0f;
    int seenRecallCount = 0, seenSwitchCount = 0;
    bool sunrisePending = true;
    float hostScale = 1.0f;

    // Last, so it can only call advanceFrame() once everything above exists.
    juce::VBlankAttachment vblank;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (HorizonPadAudioProcessorEditor)
};
