#pragma once

#include "HorizonLookAndFeel.h"
#include "Motion.h"

namespace horizon::ui
{

/**
    A ring knob whose drawing is animated while its value never is: the
    slider - and through its attachment, the parameter - always holds the
    real value; only what is painted eases toward it. The sound never waits
    for an animation.

      - A change made with the mouse (drag, wheel) is drawn at once:
        instant under the hand.
      - Any other change (host automation, a preset, an A/B switch) glides -
        briskly by default, more slowly and after a delay once the editor
        starts a wave (beginWave): the left-to-right sweep after a recall,
        and the sweep up from zero when the editor opens.
      - Hover and drag fade in and out: they warm the track, light the
        value ring and swell the dot (drawRingKnob()), and getHighlight()
        tells the parent how lit to draw the readout.

    Once bound to its parameter (bindParameter()) it behaves like every
    DAW knob (UI review 2026-10-17): double-click resets to the parameter's
    default, right-click opens the host's own menu for that parameter
    (automation, MIDI mapping; a plain "Reset" menu in the standalone), and
    it is reachable by Tab with the arrow keys changing it - but a click does
    not take keyboard focus, so the space bar still reaches the host's
    transport. Screen readers read its name and the parameter's own text.

    The component is `margin` px bigger than the ring on every side so the
    dot's swelling glow is never clipped; hitTest() keeps that margin from
    taking clicks. Message thread only.
*/
class AnimatedKnob final : public juce::Slider
{
public:
    explicit AnimatedKnob (int marginToUse);

    /** Call once the attachment exists: default for double-click, the
        accessible name, keyboard focus, and the parameter the host's context
        menu is asked about. */
    void bindParameter (juce::RangedAudioParameter& parameter, const juce::String& accessibleName);

    /** The component bounds for a ring drawn in `ringBounds` (parent coordinates). */
    juce::Rectangle<int> boundsForRing (juce::Rectangle<int> ringBounds) const noexcept { return ringBounds.expanded (margin); }

    /** Where the ring is drawn, in the parent's coordinates. */
    juce::Rectangle<int> getRingBounds() const noexcept { return getBounds().reduced (margin); }

    /** Draws the current value as it is, with no glide. Call once the
        attachment exists: a parameter whose value equals the slider's
        initial 0 never sends the first valueChanged(). */
    void syncDisplay();

    /** Lets the next glide wait until `startTime` (the editor's frame clock,
        seconds) and run at the slower wave speed. With `fromZero` the
        drawing first drops to the bottom of the range (the sunrise on open). */
    void beginWave (double startTime, bool fromZero) noexcept;

    /** Advances the fades and the glide by one frame and repaints the knob
        if anything it draws changed. Returns true in that case, so the parent
        can repaint its readout. */
    bool advance (double now, float dt);

    /** The value as drawn (eased), in the slider's own units. (Not const:
        Slider::proportionOfLengthToValue() isn't.) */
    double getDisplayValue() { return proportionOfLengthToValue ((double) display.value); }

    /** 0..1: how lit the parent should draw this knob's readout. */
    float getHighlight() const noexcept { return juce::jmax (hover.value * 0.55f, drag.value); }

    void paint (juce::Graphics&) override;
    bool hitTest (int x, int y) override;
    void mouseDown (const juce::MouseEvent&) override;
    void focusGained (FocusChangeType) override { repaint(); }
    void focusLost (FocusChangeType) override   { repaint(); }

private:
    void valueChanged() override;
    void showContextMenu();

    juce::RangedAudioParameter* parameter = nullptr;

    const int margin;

    motion::Spring display;            // proportion 0..1 along the sweep
    motion::Follower hover, drag;

    double holdUntil = 0.0;
    bool waveGlide = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AnimatedKnob)
};

/** The ring knobs' shared rotary setup: -135..+135 degrees (a 270 degree
    sweep with the gap centred at the bottom), matching the design handoff's
    ringKnob() geometry, in the given accent colour. */
void setUpRingKnob (juce::Slider& slider, juce::Colour accent);

} // namespace horizon::ui
