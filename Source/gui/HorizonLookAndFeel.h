#pragma once

#include <JuceHeader.h>

namespace horizon::ui
{

/** The Oberli Audio design system's tokens (project/tokens.json in the
    "Oberli Audio" design system, v1), Charcoal theme: Horizon Pad is an
    instrument, and its accent is mango. Every colour the editor draws
    outside the Scene comes from here; the Scene (HorizonScene, the sky) is
    the one place gradients and glow are allowed (round 11), and it never
    takes the accent. */
namespace Tokens
{
    const juce::Colour ground         { 0xff1c1b19 };   // charcoal
    const juce::Colour surfaceSunken  { 0xff171614 };   // wells, text fields
    const juce::Colour surfaceRaised  { 0xff232220 };   // header, menu, drawer
    const juce::Colour surfacePanel   { 0xff2a2826 };   // knob cap, hovered and current rows
    const juce::Colour line           { 0xff3a3836 };   // hairlines; never the only cue for a control
    const juce::Colour lineStrong     { 0xff4a4844 };   // unfilled knob and slider track
    const juce::Colour lineControl    { 0xff6b665d };   // outline of an operable control, 3.0:1 on ground
    const juce::Colour ink            { 0xffede8df };   // bone: text, values, focus ring
    const juce::Colour inkMuted       { 0xffbdb6aa };   // labels, section titles
    const juce::Colour inkSubtle      { 0xff9c968b };   // version, captions
    const juce::Colour accent         { 0xfff09300 };   // mango: pointer, value arc, selected segment, lit state
    const juce::Colour accentInk      = accent;         // the accent as text (the edited-preset mark)
    const juce::Colour onAccent       { 0xff1c1b19 };
    const juce::Colour meterLevel     { 0xffede8df };   // meter below -6 dBFS
    const juce::Colour meterHot       { 0xfff6bf26 };   // meter from -6 dBFS, the lit LIMIT LED
    const juce::Colour readout        { 0xffede8df };   // drag readout, tooltip
    const juce::Colour onReadout      { 0xff1c1b19 };

    // The Bone theme, for the back of the panel (AboutBack: the other theme).
    namespace Bone
    {
        const juce::Colour ground        { 0xffede8df };
        const juce::Colour surfaceRaised { 0xfff6f2eb };
        const juce::Colour line          { 0xffd3ccc0 };
        const juce::Colour lineControl   { 0xff77726a };
        const juce::Colour ink           { 0xff1c1b19 };
        const juce::Colour inkMuted      { 0xff45423e };
        const juce::Colour inkSubtle     { 0xff5c574f };
        const juce::Colour surfaceSunken { 0xffe6e0d6 };
    }

    // radius-*, in px at 100 %.
    constexpr float radiusXs = 4.0f, radiusSm = 6.0f, radiusMd = 10.0f, radiusLg = 14.0f;

    // shadow-pop (0 8px 24px rgba(0,0,0,0.45)): only for things that float.
    inline juce::DropShadow popShadow() { return { juce::Colours::black.withAlpha (0.45f), 24, { 0, 8 } }; }
}

/** Rubik, bundled (assets/fonts, SIL OFL 1.1, the static instances Google
    Fonts serves, unmodified) and registered once per process. Rubik's
    default figures are proportional, so every value switches on its `tnum`
    feature (JUCE 8's FontOptions::withFeatureEnabled): digits keep their
    width while a knob turns. Sizes are CSS px (the em, JUCE's point height),
    tracking in em, so the numbers read straight off the design system. */
namespace type
{
    enum class Weight { regular, medium, semibold, bold, extrabold };

    juce::Font font (Weight, float px, float trackingEm = 0.0f, bool tabular = false);

    inline juce::Font pluginName() { return font (Weight::bold, 15.0f); }
    inline juce::Font section()    { return font (Weight::bold, 11.0f, 0.12f); }
    /** 11 px, not the system's 10: the owner's choice of 2026-10-28 (the
        render rules' minimum); the system's `label` token follows. */
    inline juce::Font label()      { return font (Weight::semibold, 11.0f, 0.10f); }
    inline juce::Font value()      { return font (Weight::medium, 12.0f, 0.0f, true); }
    inline juce::Font control()    { return font (Weight::medium, 12.0f); }
    inline juce::Font meta()       { return font (Weight::medium, 11.0f, 0.0f, true); }
    inline juce::Font small()      { return font (Weight::regular, 13.0f); }
    inline juce::Font body()       { return font (Weight::regular, 15.0f); }
    inline juce::Font title()      { return font (Weight::bold, 22.0f); }
    inline juce::Font eyebrow()    { return font (Weight::bold, 12.0f, 0.14f); }
    inline juce::Font display()    { return font (Weight::extrabold, 56.0f, -0.02f); }

    /** Draws `text` in a box of the style's line height, the way CSS centres
        a line in its line box. */
    void draw (juce::Graphics&, const juce::String& text, juce::Rectangle<float> lineBox,
               const juce::Font&, juce::Colour, juce::Justification = juce::Justification::centredLeft);

    float width (const juce::Font&, const juce::String& text);
}

/** The Icon card's thin-line icons: a 16 px grid, 1.5 px strokes, round
    caps and joins, no fills, drawn in the current colour. */
namespace icons
{
    enum class Id { chevronLeft, chevronRight, menu, copy, load, save, about, close };

    /** Strokes the icon into `area` (its 16-unit grid scaled to fit). */
    void draw (juce::Graphics&, Id, juce::Rectangle<float> area, juce::Colour);
}

/** The Oberli marks (the design system's Logo group), embedded as SVG and
    drawn as vectors, never retyped: the name-only mark on charcoal for the
    header, the full lockup on bone for the back of the panel. */
namespace logo
{
    std::unique_ptr<juce::Drawable> nameOnBone();         // oberli-bone.svg
    std::unique_ptr<juce::Drawable> lockupOnCharcoal();   // oberli-audio-charcoal.svg (for bone)
}

/** The Knob card: cap 72 % of the box with a 1.5 px `line-control` ring,
    track and value arc at 46 % radius (3 px, 2 px at knob-sm), the pointer
    a rounded bar from the 100-unit master (x 43.88 y 19.4 w 12.24 h 24.48
    rx 6.12) turned about the centre, 270 degrees of travel from 7:30.
    `proportion` is 0..1 along the sweep. With `focused`, the 2 px `ink`
    focus ring 3 px off the cap. */
void drawKnob (juce::Graphics&, juce::Rectangle<float> box, float proportion, bool focused);

/** The 2 px `ink` focus ring, 3 px off `controlBounds`, with this corner. */
void drawFocusRing (juce::Graphics&, juce::Rectangle<float> controlBounds, float corner);

/** A floating card: `shadow-pop`, then the fill and a 1 px border. */
void drawPopCard (juce::Graphics&, juce::Rectangle<float> bounds, float corner,
                  juce::Colour fill, juce::Colour border);

/**
    The editor's LookAndFeel: Rubik as the default face (so JUCE's own text
    editors and alert windows use it too), the Tooltip card (the `readout`
    pair, 600 ms delay set by the editor's TooltipWindow, the range line at
    75 %), the TextField look (`surface-sunken`, `line` border,
    `radius-md`), thin scrollbars, and the alert window in the system's
    colours. Every widget of the editor otherwise paints itself.
*/
class HorizonLookAndFeel final : public juce::LookAndFeel_V4
{
public:
    HorizonLookAndFeel();

    juce::Typeface::Ptr getTypefaceForFont (const juce::Font&) override;

    juce::Rectangle<int> getTooltipBounds (const juce::String& tipText, juce::Point<int> screenPos,
                                           juce::Rectangle<int> parentArea) override;
    void drawTooltip (juce::Graphics&, const juce::String& text, int width, int height) override;

    void fillTextEditorBackground (juce::Graphics&, int width, int height, juce::TextEditor&) override;
    void drawTextEditorOutline (juce::Graphics&, int width, int height, juce::TextEditor&) override;

    void drawScrollbar (juce::Graphics&, juce::ScrollBar&, int x, int y, int width, int height,
                        bool isScrollbarVertical, int thumbStartPosition, int thumbSize,
                        bool isMouseOver, bool isMouseDown) override;
    int getDefaultScrollbarWidth() override { return 8; }

private:
    std::unique_ptr<juce::TextLayout> layoutTooltip (const juce::String& text) const;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (HorizonLookAndFeel)
};

} // namespace horizon::ui
