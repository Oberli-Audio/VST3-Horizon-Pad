#pragma once

#include "PluginProcessor.h"

#include "gui/HorizonLookAndFeel.h"
#include "gui/HorizonScene.h"
#include "gui/TitleBanner.h"
#include "gui/PresetBar.h"
#include "gui/PadKnob.h"
#include "gui/WheelSlider.h"
#include "gui/MacrosPanel.h"
#include "gui/OutputMeter.h"
#include "gui/FooterBar.h"
#include "gui/AboutPanel.h"

/**
    The editor: one shared panel (HorizonScene - sky, glow, mountain ridge)
    behind a title banner, a preset row, an 8-column knob grid (PITCH, MOD,
    the four pad knobs, the macros grid, OUTPUT), and a footer strip - a
    pixel-accurate recreation of the Claude Design GUI handoff ("Horizon
    Pad.dc.html"), read directly rather than approximated. There is no
    on-screen keyboard: play from a MIDI keyboard/controller, matching a real
    Launchkey-style workflow (the eight knobs mirror a Launchkey 25's eight
    knobs, and the wheels mirror its pitch/mod wheels).

    The window size and every inset below (kTopBottomPadding etc.) are taken
    straight from the handoff's own CSS (panelStyle's padding, presetsRowStyle's
    margin-top, mainGridStyle's gap, ...), scaled 1:1 - not eyeballed.

    One layout, drawn at a chosen size: every child paints its own precise
    layout in the designed 1080 x 748 coordinates, and the SIZE menu in the
    footer scales the whole editor to 75 / 100 / 125 / 150 / 200% (a transform on
    top of whatever scale the host itself applies - setScaleFactor() keeps
    the two apart). Everything is vector or cached at the screen's pixel
    density, so no size blurs. A machine-local preference every instance
    opens at, never in the session (HorizonPadAudioProcessor::getEditorScale()).

    Motion. Everything moves because the sound changed, and only then:
      - the scene follows FILTER (time of day), REVERB (haze) and WIDTH
        (the glow's spread);
      - knobs glide after a preset recall or A/B switch, in a left-to-right
        wave, while a hand on a knob is followed at once (AnimatedKnob);
      - a recalled preset's name and description fade in under the wordmark;
      - each pad card glows with its layer's real output;
      - the first time the editor opens on an instance, the sky rises from
        night, the logo's sun comes up from behind its peaks and the knobs
        sweep up to their values; reopening the window shows it as it is.
    One frame callback (advanceFrame) drives it all, from the display's
    vertical blank while the editor is on screen, capped near 60 fps; each
    piece repaints only its own changed area, so a quiet editor costs
    nothing. It also replaces the old 15 Hz polling timer.
*/
class HorizonPadAudioProcessorEditor final : public juce::AudioProcessorEditor,
                                             private juce::Timer
{
public:
    explicit HorizonPadAudioProcessorEditor (HorizonPadAudioProcessor&);
    ~HorizonPadAudioProcessorEditor() override;

    /** Opens the About box (footer product label; the snapshot tool uses it too). */
    void showAbout (bool openLicences = false);

    void paint (juce::Graphics&) override;
    void resized() override;

    /** The host's own scale (Windows per-monitor DPI, some Linux hosts); the
        player's SIZE choice is multiplied on top. */
    void setScaleFactor (float newScale) override;

    /** One animation frame at `nowSeconds` (Time::getMillisecondCounterHiRes()
        timebase): polls the processor and advances every animation. Driven by
        the vertical blank; public so tools/gui_snapshot can step it offline
        with a clock of its own. */
    void advanceFrame (double nowSeconds);

private:
    void timerCallback() override;

    void pullSceneTargets();
    void startWave (double startTime, bool fromZero);
    void showRecalledPresetTitle();
    void applyScale();

    static double clockNow() noexcept { return juce::Time::getMillisecondCounterHiRes() * 0.001; }

    // The design's panel is 1080px wide with 36px top/bottom and 40px
    // left/right padding; the window height follows from stacking its own
    // header/divider/presets/grid/footer measurements exactly.
    static constexpr int kWindowWidth = 1080;
    static constexpr int kWindowHeight = 748;
    static constexpr int kSidePadding = 40;
    static constexpr int kTopBottomPadding = 36;

    // AudioProcessorEditor's own `processor` member is the plain
    // AudioProcessor; the frame callback needs the meters and recall counters.
    HorizonPadAudioProcessor& horizonProcessor;
    std::atomic<float>* filterParam = nullptr;
    std::atomic<float>* reverbParam = nullptr;
    std::atomic<float>* widthParam = nullptr;

    juce::Rectangle<int> dividerBounds;
    float presetRowProportion = 0.34f;   // the preset row's centre, as a proportion of the window height

    horizon::ui::HorizonLookAndFeel lookAndFeel;
    horizon::ui::HorizonScene scene;

    horizon::ui::TitleBanner titleBanner;
    horizon::ui::PresetBar presetBar;

    horizon::ui::WheelSlider pitchWheel, modWheel;
    horizon::ui::PadKnob rootKnob, clearingKnob, expanseKnob, bloomKnob;
    horizon::ui::MacrosPanel macrosPanel;
    horizon::ui::OutputMeter outputMeter;

    horizon::ui::FooterBar footerBar;
    horizon::ui::AboutPanel aboutPanel;

    // Every control below sets its own setTooltip() text; without an actual
    // TooltipWindow instance somewhere in the component tree, that text is
    // stored but never shown - this is what makes it appear on hover.
    juce::TooltipWindow tooltipWindow;

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
