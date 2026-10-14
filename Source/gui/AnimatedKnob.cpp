#include "AnimatedKnob.h"

namespace horizon::ui
{

namespace
{
    // motion::Spring speeds: following automation (~0.15 s, close enough to
    // the sound that the knob never looks late) and the slower wave after a
    // preset recall or on open (~0.45 s per knob).
    constexpr float kFollowOmega = 30.0f;
    constexpr float kWaveOmega = 10.5f;

    // Under 1/2000 of the sweep (0.14 degrees) is below anything visible.
    constexpr float kSettleEpsilon = 5.0e-4f;
}

void setUpRingKnob (juce::Slider& slider, juce::Colour accent)
{
    slider.setColour (juce::Slider::rotarySliderFillColourId, accent);
    slider.setColour (juce::Slider::rotarySliderOutlineColourId, Palette::knobTrack);
    slider.setRotaryParameters (juce::MathConstants<float>::pi * 1.25f,
                                juce::MathConstants<float>::pi * 2.75f,
                                true);
}

AnimatedKnob::AnimatedKnob (int marginToUse)
    : juce::Slider (juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::NoTextBox),
      margin (marginToUse)
{
    // Kept as an image: the pad cards repaint the glow around their knob up
    // to 30 times a second while a layer sounds, and a knob that has not
    // changed is then copied instead of drawn again (its arcs were a third
    // of the editor's painting while playing).
    setBufferedToImage (true);
}

void AnimatedKnob::syncDisplay()
{
    display.snapTo ((float) valueToProportionOfLength (getValue()));
    waveGlide = false;
    holdUntil = 0.0;
    repaint();
}

void AnimatedKnob::valueChanged()
{
    // Anything done by hand (a drag, or the wheel while hovering) is drawn
    // at once; everything else glides from where the drawing is now.
    if (isMouseButtonDown() || isMouseOver())
        syncDisplay();
    else
        display.target = (float) valueToProportionOfLength (getValue());
}

void AnimatedKnob::beginWave (double startTime, bool fromZero) noexcept
{
    if (fromZero)
    {
        display.value = 0.0f;
        display.velocity = 0.0f;
    }

    display.target = (float) valueToProportionOfLength (getValue());
    holdUntil = startTime;
    waveGlide = true;
}

bool AnimatedKnob::advance (double now, float dt)
{
    // Fades: in quickly, out a little slower, so a pass of the mouse leaves
    // a short afterglow instead of a flicker.
    const bool hoverMoved = hover.advance (isMouseOverOrDragging() ? 1.0f : 0.0f, dt, 0.06f, 0.22f);
    const bool dragMoved = drag.advance (isMouseButtonDown() ? 1.0f : 0.0f, dt, 0.05f, 0.30f);
    bool glideMoved = false;

    if (now >= holdUntil)
    {
        glideMoved = display.advance (dt, waveGlide ? kWaveOmega : kFollowOmega, kSettleEpsilon);

        if (display.isSettled())
            waveGlide = false;
    }

    const auto changed = hoverMoved || dragMoved || glideMoved;

    if (changed)
        repaint();

    return changed;
}

void AnimatedKnob::paint (juce::Graphics& g)
{
    const auto rotary = getRotaryParameters();

    drawRingKnob (g, getLocalBounds().reduced (margin).toFloat(), display.value,
                  rotary.startAngleRadians, rotary.endAngleRadians,
                  findColour (juce::Slider::rotarySliderFillColourId),
                  findColour (juce::Slider::rotarySliderOutlineColourId),
                  hover.value, drag.value);
}

bool AnimatedKnob::hitTest (int x, int y)
{
    return getLocalBounds().reduced (margin).contains (x, y);
}

} // namespace horizon::ui
