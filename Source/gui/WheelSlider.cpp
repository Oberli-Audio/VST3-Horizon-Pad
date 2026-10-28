#include "WheelSlider.h"
#include "../PluginProcessor.h"

namespace horizon::ui
{

namespace
{
    constexpr float kTrackHitWidth = 24.0f;   // hit-min
    constexpr float kThumb = 14.0f;

    const juce::String minus = juce::String::fromUTF8 ("\xe2\x88\x92");   // a real minus sign
}

//==============================================================================
WheelSlider::Track::Track (Kind k)
    : juce::Slider (juce::Slider::LinearVertical, juce::Slider::NoTextBox), kind (k)
{
    // A drag moves the wheel from where it is, as a hardware wheel does,
    // instead of jumping to the click.
    setSliderSnapsToMousePosition (false);
}

void WheelSlider::Track::paint (juce::Graphics& g)
{
    const auto area = getLocalBounds().toFloat().reduced ((float) kFocusRoom);
    const auto hit = area.withSizeKeepingCentre (kTrackHitWidth, area.getHeight());
    const auto centreX = hit.getCentreX();
    const auto bar = juce::Rectangle<float> (centreX - 2.0f, hit.getY(), 4.0f, hit.getHeight());

    g.setColour (Tokens::lineStrong);
    g.fillRoundedRectangle (bar, 2.0f);

    const auto travel = hit.getHeight() - kThumb;
    const auto thumbCentre = hit.getY() + kThumb * 0.5f
                           + (1.0f - (float) valueToProportionOfLength (getValue())) * travel;
    const auto middle = hit.getCentreY();

    if (kind == Kind::pitch)
    {
        // The centre detent, then the fill from it to the thumb.
        g.setColour (Tokens::lineControl);
        g.fillRect (juce::Rectangle<float> (centreX - 9.0f, middle - 0.5f, 18.0f, 1.0f));

        g.setColour (Tokens::accent);
        g.fillRect (juce::Rectangle<float> (bar.getX(), juce::jmin (thumbCentre, middle), 4.0f, std::abs (thumbCentre - middle)));
    }
    else
    {
        juce::Path fill;
        const auto fillArea = bar.withTop (thumbCentre);
        fill.addRoundedRectangle (fillArea.getX(), fillArea.getY(), fillArea.getWidth(), fillArea.getHeight(),
                                  2.0f, 2.0f, false, false, true, true);
        g.setColour (Tokens::accent);
        g.fillPath (fill);
    }

    const auto thumb = juce::Rectangle<float> (kThumb, kThumb).withCentre ({ centreX, thumbCentre });
    g.setColour (Tokens::surfacePanel);
    g.fillEllipse (thumb);
    g.setColour (Tokens::lineControl);
    g.drawEllipse (thumb.reduced (0.75f), 1.5f);

    g.setColour (Tokens::accent);
    g.fillRoundedRectangle (juce::Rectangle<float> (centreX - 1.0f, thumbCentre - 5.0f, 2.0f, 5.0f), 1.0f);

    if (hasKeyboardFocus (false))
        drawFocusRing (g, hit, 6.0f);
}

//==============================================================================
WheelSlider::WheelSlider (HorizonPadAudioProcessor& processorToUse, Kind kindToUse)
    : processor (processorToUse), kind (kindToUse), track (kindToUse)
{
    if (kind == Kind::pitch)
        track.setRange (-horizon::PerformanceState::kPitchBendRangeSemitones,
                        horizon::PerformanceState::kPitchBendRangeSemitones, 0.01);
    else
        track.setRange (0.0, 1.0, 0.001);

    track.setDoubleClickReturnValue (true, 0.0);

    // Reachable by Tab and read by screen readers; a click does not take
    // keyboard focus, so the space bar still reaches the host's transport.
    track.setTitle (kind == Kind::pitch ? "Pitch bend" : "Mod wheel");
    track.setWantsKeyboardFocus (true);
    track.setMouseClickGrabsKeyboardFocus (false);

    track.setTooltip (kind == Kind::pitch
        ? "Bends the pitch, and springs back to the centre when you let go. Follows a hardware pitch wheel too."
        : "Adds slow analog-style drift on top of each layer's own. Stays where you leave it; follows a hardware mod wheel (CC1) too.");

    track.onValueChange = [this]
    {
        if (updating)
            return;

        if (kind == Kind::pitch)
            processor.getPerformanceState().setPitchBendSemitones ((float) track.getValue());
        else
            processor.getPerformanceState().setModAmount ((float) track.getValue());

        repaint();
    };

    // PITCH is spring-loaded, like a real keyboard's wheel: it snaps back
    // to the centre the moment you let go. MOD stays where it is left.
    if (kind == Kind::pitch)
        track.onDragEnd = [this] { track.setValue (0.0, juce::sendNotification); };

    addAndMakeVisible (track);
    refreshFromProcessor();
}

WheelSlider::~WheelSlider() = default;

void WheelSlider::refreshFromProcessor()
{
    const juce::ScopedValueSetter<bool> guard (updating, true);

    const auto value = kind == Kind::pitch ? processor.getPerformanceState().getPitchBendSemitones()
                                           : processor.getPerformanceState().getModAmount();

    if (std::abs (track.getValue() - value) > 1.0e-4)
    {
        track.setValue (value, juce::dontSendNotification);
        repaint();
    }
}

juce::String WheelSlider::valueText() const
{
    const auto v = track.getValue();

    if (kind == Kind::mod)
        return juce::String (juce::roundToInt (v * 100.0)) + " %";

    if (std::abs (v) < 0.05)
        return "0 st";

    return (v > 0.0 ? "+" : minus) + juce::String (std::abs (v), 1) + " st";
}

void WheelSlider::resized()
{
    track.setBounds (0, 0, kWidth, kFocusRoom * 2 + kTrackHeight);
}

void WheelSlider::paint (juce::Graphics& g)
{
    const auto labelBox = juce::Rectangle<float> (0.0f, (float) (kFocusRoom + kTrackHeight + 4), (float) kWidth, 12.0f);
    const auto valueBox = labelBox.translated (0.0f, 12.0f + 8.0f).withHeight (16.0f);

    type::draw (g, kind == Kind::pitch ? "PITCH" : "MOD", labelBox, type::label(), Tokens::inkMuted,
                juce::Justification::centred);
    type::draw (g, valueText(), valueBox, type::value(), Tokens::ink, juce::Justification::centred);
}

} // namespace horizon::ui
