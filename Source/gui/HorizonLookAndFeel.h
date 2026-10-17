#pragma once

#include <JuceHeader.h>

namespace horizon::ui
{

/** The whole plugin's colour vocabulary in one place - values are exact
    conversions (OKLCH -> linear sRGB -> gamma-encoded sRGB, Bjorn Ottosson's
    OKLab formulas) of the oklch() colours in the authoritative Claude Design
    handoff ("Horizon Pad.dc.html"), so this is a pixel-accurate palette
    match, not an approximation. Names follow the handoff's own style names
    (panelStyle, cardBg, pillBorder, ...) so the two stay easy to cross-check. */
namespace Palette
{
    const juce::Colour background       { 0xff020306 };

    // The one shared panel that is the whole plugin window: a sunset-sky
    // gradient (pale, cool sage at the top of the window warming down to a
    // glowing red-orange right behind the mountains), a thin border, a
    // heavily blurred mountain silhouette along the bottom (soft dark waves
    // rather than a crisp skyline), and a soft warm glow low and centred -
    // like a low sun glowing behind the ridge.
    // Colours are picked off a reference sunset-over-mountains photo the
    // user supplied, darkened where needed (mainly the top stop) so the
    // wordmark/labels drawn directly on the panel stay readable. This is
    // the panel at FILTER 50%; HorizonScene moves it toward dusk or golden
    // hour with the FILTER macro.
    const juce::Colour panelGradientBottom { 0xffcf4a1c };
    const juce::Colour panelGradientLower  { 0xffb35a24 };
    const juce::Colour panelGradientUpper  { 0xff8a6a3c };
    const juce::Colour panelGradientTop    { 0xff4a4f43 };
    const juce::Colour panelBorder         { 0x99232933 };
    const juce::Colour skyline             { 0x8010141b };
    const juce::Colour glow                { 0x38f5b464 };

    // Grid-card chrome (PITCH/MOD/ROOT/CLEARING/EXPANSE/BLOOM/MACROS/OUTPUT).
    const juce::Colour cardBg           { 0xff10141b };
    const juce::Colour cardBorder       { 0x80282e38 };
    const juce::Colour iconGlyph        { 0xfff0bb3b };

    // Pills (preset/user-preset/tab/save buttons).
    const juce::Colour pillBorder       { 0xb22d333d };
    const juce::Colour pillActiveBg     { 0x29f5ae39 };
    const juce::Colour pillInactiveUserBg { 0x08ffffff };
    const juce::Colour saveInputBg      { 0x0affffff };
    const juce::Colour saveConfirmBorder{ 0xff5bbd74 };
    const juce::Colour saveConfirmBg    { 0x2e5bbd74 };

    // Destructive actions (cancel, delete) - a warm red paired the same way
    // saveConfirmBorder/saveConfirmBg pair for the green "Save" confirm.
    const juce::Colour cancelBorder     { 0xffe0645a };
    const juce::Colour cancelBg         { 0x2ee0645a };

    // Text.
    const juce::Colour text             { 0xffebeff5 }; // "hi" - wordmark, active pill text
    const juce::Colour textKnobLabel    { 0xffe4e8ef }; // card labels (PITCH, ROOT, ...)
    const juce::Colour textValue        { 0xffcaced4 }; // card value readouts
    const juce::Colour textDim          { 0xff7f8793 }; // captions, tagline, macro values
    const juce::Colour textFaint        { 0xff79818d }; // delete/copy/cancel/save-open text
    const juce::Colour textFooter       { 0xfff3e6c8 }; // footer strip text - the cream of a recalled
                                                          // preset's title: the strip sits straight on the
                                                          // sunset glow (no card behind it). 0xffc9bfa9
                                                          // measured 2.1-2.2:1 there; this is >= 3:1 in
                                                          // every palette (UI review 2026-10-17)
    const juce::Colour macroLabel       { 0xffb4b8be };

    const juce::Colour dividerColor     { 0x80282e38 };

    const juce::Colour knobTrack        { 0xff2a2e36 }; // unlit portion of a ring knob
    const juce::Colour knobInner        { 0xff12161d }; // ring knob's dark cap
    const juce::Colour dotUnlit         { 0xff2f333b }; // layer status dot when silent

    const juce::Colour logoMountain     { 0xff080b12 };
    const juce::Colour logoGold         { 0xfff0bb3b };
    const juce::Colour wordmarkGoldEnd  { 0xffeabb79 };

    const juce::Colour gold             { 0xfff5ae39 }; // the one warm accent: active borders/text, wheel thumbs
    const juce::Colour presetTitle      { 0xfff3e6c8 }; // a recalled preset's name under the wordmark - a warm cream
                                                          // rather than the wordmark's gold, which falls below 4.5:1 on the
                                                          // golden-hour sky (HorizonScene) at that size
    const juce::Colour thumbGlow        { 0x80f5ae39 };

    // Per-pad accents, in LayerIndex order: Root, Clearing, Expanse, Bloom.
    const juce::Colour layerAccents[] {
        juce::Colour (0xff5bbd74),   // 1. Root (Warm Foundation)     - green,  "life grows"
        juce::Colour (0xfff0bb3b),   // 2. Clearing (Analog Ensemble) - amber,  "light breaks in"
        juce::Colour (0xff2fb5d8),   // 3. Expanse (Airy Choir)       - blue,   "life opens up"
        juce::Colour (0xffed76b3)    // 4. Bloom (Motion Pad)         - pink,   "life blooms"
    };

    // Macro accents, in MacroIndex order. The first four take the design's
    // four macro-slot colours in order (its fourth slot is labelled DRIVE
    // there). WIDTH keeps the neutral tan it had as each pad's own knob, and
    // DETUNE gets a violet no layer or macro uses.
    const juce::Colour macroAccents[] {
        juce::Colour (0xfff5ae39),   // Attack  - gold
        juce::Colour (0xffed76b3),   // Release - pink
        juce::Colour (0xff2fb5d8),   // Filter  - blue
        juce::Colour (0xff5bbd74),   // Reverb  - green
        wordmarkGoldEnd,             // Width   - tan
        juce::Colour (0xffa98bf0)    // Detune  - violet
    };

    // Pitch / mod wheels share one neutral performance-control accent.
    const juce::Colour wheelAccent   { 0xfff5ae39 };

    // Output meter.
    const juce::Colour meterBg       { 0xff040609 };
    const juce::Colour meterFillLow  { 0xffb37903 };
    const juce::Colour meterFillHigh { 0xfffac547 };
}

/** The grid cards' (PITCH/MOD/ROOT/.../MACROS/OUTPUT) shared type sizes, in
    one place so every card stays consistent - point sizes were enlarged
    across the board from the original design handoff for readability. */
namespace TypeScale
{
    constexpr float icon    = 30.0f;
    constexpr float label   = 14.0f;
    constexpr float caption = 12.5f;
    constexpr float value   = 15.0f;
}

/** Shared typography helper; keeps every label on the same handful of type sizes. */
inline juce::Font labelFont (float height, bool bold = false)
{
    return juce::Font (juce::FontOptions()
                           .withHeight (height)
                           .withStyle (bold ? "Bold" : "Regular"));
}

/** The serif italic "Horizon Pad" wordmark font (the design specifies
    Newsreader italic; JUCE's generic serif placeholder - resolved to
    whatever serif the host OS actually has - is used rather than naming a
    specific family like "Georgia": asking for a named font that happens not
    to be installed doesn't just look wrong, it has been observed to crash
    JUCE's text shaper outright on at least one Linux setup, so the generic,
    always-resolvable placeholder is the safe choice here). */
inline juce::Font titleFont (float height)
{
    return juce::Font (juce::FontOptions()
                           .withName (juce::Font::getDefaultSerifFontName())
                           .withHeight (height)
                           .withStyle ("Italic"));
}

/**
    Custom LookAndFeel: a thin ring-and-dot rotary (matching the design's
    conic-gradient ringKnob() helper), a flat track-and-pill-thumb vertical
    slider for the PITCH/MOD wheels, and pill buttons whose border follows a
    small per-Component::Properties convention so one generic
    drawButtonBackground can produce every pill style the design uses:

      - by default: pillBorder when off, gold when toggled on;
      - "noBorder"     (bool)  - draw no border at all (a pill's inner
                                  sub-buttons, whose *parent* paints the
                                  shared border - see PresetBar's UserPresetPill);
      - "dashedBorder" (bool)  - dash the (untoggled) border - the "+ Save
                                  preset" control;
      - "borderColour" (int, packed ARGB) - a fixed border colour overriding
                                  the default/gold choice (e.g. the green
                                  "Save" confirm button);
      - "activeAmount" (float) - a preset pill's highlight, 0..1, faded by
                                  PresetBar: gold fill and border follow it
                                  instead of the toggle state.
*/
class HorizonLookAndFeel final : public juce::LookAndFeel_V4
{
public:
    HorizonLookAndFeel();

    void drawRotarySlider (juce::Graphics&, int x, int y, int width, int height,
                           float sliderPosProportional, float rotaryStartAngle,
                           float rotaryEndAngle, juce::Slider&) override;

    void drawLinearSlider (juce::Graphics&, int x, int y, int width, int height,
                           float sliderPos, float minSliderPos, float maxSliderPos,
                           const juce::Slider::SliderStyle style, juce::Slider&) override;

    juce::Font getLabelFont (juce::Label&) override;
    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour& backgroundColour,
                               bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown) override;
    juce::Font getTextButtonFont (juce::TextButton&, int buttonHeight) override;

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (HorizonLookAndFeel)
};

/** Rounded panel with a 1px border, used by preset pills and the header field. */
void drawPanel (juce::Graphics& g, juce::Rectangle<float> bounds,
                juce::Colour fill = Palette::cardBg, float corner = 10.0f);

/**
    The ring knob, matching the design handoff's ringKnob() helper: a dark
    track, the value ring in the accent colour, a dark cap and a glowing dot
    at the value. `proportion` is 0..1 along the sweep. `hover` and `drag`
    (0..1) are AnimatedKnob's highlight fades: hover warms the track and
    brightens the dot's glow, drag lights the value ring and swells the dot.
    Draws inside `area` reduced by 2 px.
*/
void drawRingKnob (juce::Graphics& g, juce::Rectangle<float> area, float proportion,
                   float rotaryStartAngle, float rotaryEndAngle, juce::Colour accent, juce::Colour trackColour,
                   float hover = 0.0f, float drag = 0.0f);

/**
    The fixed vertical slot layout every grid card (PITCH, MOD, ROOT,
    CLEARING, EXPANSE, BLOOM, MACROS, OUTPUT) shares: 16px/12px padding, then
    icon row (30px), label (20px), caption (32px), a flexible control area,
    and a value readout (22px), each separated by a 6px gap. Enlarged from
    the original design handoff's tighter slots (18px/12px padding, 28/16/30/
    18px rows, 7px gaps) to fit the bigger TypeScale type sizes above.
*/
struct ColumnSlots
{
    juce::Rectangle<int> icon, label, caption, control, value;
};

ColumnSlots computeColumnSlots (juce::Rectangle<int> cardBounds);

/** Fills+strokes one grid card's rounded-rect chrome (cardBg/cardBorder, 14px corner). */
void drawColumnCard (juce::Graphics& g, juce::Rectangle<float> bounds);

/**
    One labelled control with its value beneath it: label, knob, value.
    Both PadKnob (VOL, one per card) and MacrosPanel (three rows of two) are
    built from it. Takes a card's full control area (control slot + value
    slot combined, since neither card type uses the shared row-aligned value
    slot - each control has its own value right beneath it instead).
*/
struct ControlLayout
{
    juce::Rectangle<int> label, control, value;
};

/** PadKnob's single VOL group (14px label, 78px knob, 22px value), centred
    vertically in `area` now that the pad cards have no second knob. */
ControlLayout computePadControlLayout (juce::Rectangle<int> area);

/** MacrosPanel's three equal rows (ATTACK/RELEASE, FILTER/REVERB,
    WIDTH/DETUNE), each 11px label, knob, 14px value, with a divider line
    between rows at `dividerYs`. Each row's rectangles span both columns. */
struct MacroGridLayout
{
    std::array<ControlLayout, 3> rows;
    std::array<int, 2> dividerYs {};
    int knobSize = 0;
};

MacroGridLayout computeMacroGridLayout (juce::Rectangle<int> area);

} // namespace horizon::ui
