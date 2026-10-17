#include "WheelSlider.h"
#include "../PluginProcessor.h"

namespace horizon::ui
{

namespace
{
    juce::String captionFor (WheelSlider::Kind k)  { return k == WheelSlider::Kind::pitch ? "PITCH" : "MOD"; }
    juce::String subtitleFor (WheelSlider::Kind k) { return k == WheelSlider::Kind::pitch ? "bend the sky" : "a soft wind"; }
    juce::String glyphFor (WheelSlider::Kind k)
    {
        // Design handoff's iconRowStyle glyphs: PITCH = up/down arrows, MOD = a wave.
        return k == WheelSlider::Kind::pitch ? juce::String::fromUTF8 ("\xe2\x87\x95")
                                             : juce::String::fromUTF8 ("\xe2\x88\xbf");
    }
}

WheelSlider::WheelSlider (HorizonPadAudioProcessor& processorToUse, Kind kindToUse)
    : processor (processorToUse), kind (kindToUse),
      caption (captionFor (kindToUse)), subtitle (subtitleFor (kindToUse))
{
    slider.setColour (juce::Slider::thumbColourId, Palette::gold);
    slider.setColour (juce::Slider::trackColourId, Palette::cardBg);

    if (kind == Kind::pitch)
        slider.setRange (-horizon::PerformanceState::kPitchBendRangeSemitones,
                         horizon::PerformanceState::kPitchBendRangeSemitones, 0.01);
    else
        slider.setRange (0.0, 1.0, 0.001);

    slider.setDoubleClickReturnValue (true, 0.0);

    // Reachable by Tab and read by screen readers; a click does not take
    // keyboard focus, so the space bar still reaches the host's transport.
    slider.setTitle (kind == Kind::pitch ? "Pitch bend" : "Mod wheel");
    slider.setWantsKeyboardFocus (true);
    slider.setMouseClickGrabsKeyboardFocus (false);

    slider.setTooltip (kind == Kind::pitch
        ? "Pitch bend - drag to bend, springs back to centre on release (also follows an external MIDI pitch wheel)."
        : "Mod wheel - adds extra analog-style pitch drift on top of each layer's own baseline (also follows an external MIDI mod wheel, CC1).");

    slider.onValueChange = [this]
    {
        if (updating)
            return;

        if (kind == Kind::pitch)
            processor.getPerformanceState().setPitchBendSemitones ((float) slider.getValue());
        else
            processor.getPerformanceState().setModAmount ((float) slider.getValue());

        repaint();
    };

    // PITCH is a spring-loaded wheel, like a real keyboard's: it snaps back to
    // centre (0 semitones) the moment you let go, matching the reference
    // design's onPointerUp behaviour. MOD stays wherever it's left.
    if (kind == Kind::pitch)
        slider.onDragEnd = [this] { slider.setValue (0.0, juce::sendNotification); };

    addAndMakeVisible (slider);
    refreshFromProcessor();
}

WheelSlider::~WheelSlider() = default;

void WheelSlider::refreshFromProcessor()
{
    const juce::ScopedValueSetter<bool> guard (updating, true);

    const auto value = kind == Kind::pitch ? processor.getPerformanceState().getPitchBendSemitones()
                                           : processor.getPerformanceState().getModAmount();

    if (std::abs (slider.getValue() - value) > 1.0e-4)
    {
        slider.setValue (value, juce::dontSendNotification);
        repaint();
    }
}

void WheelSlider::resized()
{
    const auto slots = computeColumnSlots (getLocalBounds());
    slider.setBounds (slots.control.withSizeKeepingCentre (24, slots.control.getHeight()));
}

void WheelSlider::paint (juce::Graphics& g)
{
    drawColumnCard (g, getLocalBounds().toFloat());

    const auto slots = computeColumnSlots (getLocalBounds());

    g.setColour (Palette::iconGlyph);
    g.setFont (juce::Font (juce::FontOptions().withHeight (TypeScale::icon)));
    g.drawText (glyphFor (kind), slots.icon, juce::Justification::centred);

    g.setColour (Palette::textKnobLabel);
    g.setFont (labelFont (TypeScale::label, true));
    g.drawText (caption, slots.label, juce::Justification::centred);

    g.setColour (Palette::textDim);
    g.setFont (labelFont (TypeScale::caption).italicised());
    g.drawFittedText (subtitle, slots.caption, juce::Justification::centred, 2);

    juce::String valueText;

    if (kind == Kind::pitch)
        valueText = (slider.getValue() >= 0.0 ? "+" : "") + juce::String (slider.getValue(), 1) + " st";
    else
        valueText = juce::String (juce::roundToInt (slider.getValue() * 100.0)) + "%";

    g.setColour (Palette::textValue);
    g.setFont (labelFont (TypeScale::value, true));
    g.drawText (valueText, slots.value, juce::Justification::centred);
}

} // namespace horizon::ui
