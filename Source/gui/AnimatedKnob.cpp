#include "AnimatedKnob.h"
#include "ReadoutBubble.h"

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

AnimatedKnob::AnimatedKnob()
    : juce::Slider (juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::NoTextBox)
{
    // knob-sweep: 270 degrees, the gap centred at the bottom.
    setRotaryParameters (juce::MathConstants<float>::pi * 1.25f, juce::MathConstants<float>::pi * 2.75f, true);

    // Kept as an image: a knob that has not changed is copied, not drawn
    // again, when the level bars and the meter beside it repaint.
    setBufferedToImage (true);
}

void AnimatedKnob::bindParameter (juce::RangedAudioParameter& parameterToUse, const juce::String& accessibleName)
{
    parameter = &parameterToUse;

    setDoubleClickReturnValue (true, (double) parameter->convertFrom0to1 (parameter->getDefaultValue()));
    setTitle (accessibleName);
    setWantsKeyboardFocus (true);
    setMouseClickGrabsKeyboardFocus (false);
}

juce::String AnimatedKnob::textFor (double sliderValue) const
{
    if (parameter == nullptr)
        return juce::String (juce::roundToInt (sliderValue));

    return parameter->getText (parameter->convertTo0to1 ((float) sliderValue), 0);
}

juce::Rectangle<float> AnimatedKnob::capBounds() const noexcept
{
    const auto box = getLocalBounds().toFloat();
    const auto size = juce::jmin (box.getWidth(), box.getHeight());
    return juce::Rectangle<float> (0.72f * size, 0.72f * size).withCentre (box.getCentre());
}

void AnimatedKnob::mouseDown (const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu() && parameter != nullptr)
    {
        showContextMenu();
        return;
    }

    juce::Slider::mouseDown (e);
    dragging = true;
    updateReadout (true);
}

void AnimatedKnob::mouseDrag (const juce::MouseEvent& e)
{
    juce::Slider::mouseDrag (e);
    updateReadout (dragging);
}

void AnimatedKnob::mouseUp (const juce::MouseEvent& e)
{
    juce::Slider::mouseUp (e);
    dragging = false;
    updateReadout (dragPreview);
}

void AnimatedKnob::setDragPreview (bool shouldShow)
{
    dragPreview = shouldShow;
    updateReadout (shouldShow);
}

void AnimatedKnob::updateReadout (bool show)
{
    if (auto* host = findParentComponentOfClass<ReadoutHost>())
    {
        if (show)
            host->showDragReadout (*this, capBounds(), textFor (getValue()));
        else
            host->hideDragReadout();
    }
}

void AnimatedKnob::showContextMenu()
{
    if (auto* editor = findParentComponentOfClass<juce::AudioProcessorEditor>())
    {
        if (auto* host = editor->getHostContext())
        {
            if (auto menu = host->getContextMenuForParameter (parameter))
            {
                menu->showNativeMenu (editor->getMouseXYRelative());
                return;
            }
        }
    }

    // No host menu (the standalone, or a host without one): reset is the
    // one thing every knob offers.
    juce::PopupMenu menu;
    menu.addItem ("Reset to default", [safe = juce::Component::SafePointer<AnimatedKnob> (this)]
    {
        if (safe != nullptr)
            safe->setValue (safe->getDoubleClickReturnValue(), juce::sendNotificationSync);
    });
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this));
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

    if (dragging || dragPreview)
        updateReadout (true);
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
    if (now < holdUntil)
        return false;

    const auto moved = display.advance (dt, waveGlide ? kWaveOmega : kFollowOmega, kSettleEpsilon);

    if (display.isSettled())
        waveGlide = false;

    if (moved)
        repaint();

    return moved;
}

void AnimatedKnob::paint (juce::Graphics& g)
{
    drawKnob (g, getLocalBounds().toFloat(), display.value, hasKeyboardFocus (false));
}

} // namespace horizon::ui
