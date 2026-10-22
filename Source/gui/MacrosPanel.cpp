#include "MacrosPanel.h"
#include "../PluginProcessor.h"
#include "../dsp/EnvelopeMacro.h"

namespace horizon::ui
{

void MacrosPanel::setUpKnob (Knob& knob, const juce::String& caption, const char* paramId,
                             juce::Colour accent, HorizonPadAudioProcessor& processor)
{
    knob.caption = caption;
    knob.accent = accent;
    setUpRingKnob (knob.slider, accent);

    // The table's sentence plus its generated range; ATTACK/RELEASE read as a
    // time, so their percent range is left off (paramgen has no display column
    // yet - docs/exceptions.md EX-005).
    juce::String tooltip (Params::tooltipFor (paramId));
    if (juce::String (paramId) == ParamID::attackMacro || juce::String (paramId) == ParamID::releaseMacro)
        tooltip = tooltip.upToFirstOccurrenceOf (" Range ", false, false);

    knob.slider.setTooltip (tooltip);
    knob.slider.setHelpText (tooltip);
    addAndMakeVisible (knob.slider);

    knob.attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        processor.getAPVTS(), paramId, knob.slider);
    knob.slider.syncDisplay();

    if (auto* parameter = processor.getAPVTS().getParameter (paramId))
        knob.slider.bindParameter (*parameter, caption.substring (0, 1) + caption.substring (1).toLowerCase());

    knob.slider.onValueChange = [this, &knob] { repaint (knob.valueCell); };
}

MacrosPanel::MacrosPanel (HorizonPadAudioProcessor& processor)
{
    // Tooltips come from parameters.csv (Params::tooltipFor, ux-copy / audio-plugins.md 14).
    setUpKnob (knobs[0], "ATTACK",  ParamID::attackMacro,  Palette::macroAccents[0], processor);
    setUpKnob (knobs[1], "RELEASE", ParamID::releaseMacro, Palette::macroAccents[1], processor);
    setUpKnob (knobs[2], "FILTER",  ParamID::filterMacro,  Palette::macroAccents[2], processor);
    setUpKnob (knobs[3], "REVERB",  ParamID::reverbMacro,  Palette::macroAccents[3], processor);
    setUpKnob (knobs[4], "WIDTH",   ParamID::widthMacro,   Palette::macroAccents[4], processor);
    setUpKnob (knobs[5], "DETUNE",  ParamID::detuneMacro,  Palette::macroAccents[5], processor);
}

MacrosPanel::~MacrosPanel() = default;

void MacrosPanel::advance (double now, float dt)
{
    for (auto& knob : knobs)
        if (knob.slider.advance (now, dt))
            repaint (knob.valueCell);
}

void MacrosPanel::beginWave (double startTime, double rowDelay, bool fromZero)
{
    for (size_t i = 0; i < knobs.size(); ++i)
        knobs[i].slider.beginWave (startTime + rowDelay * (double) (i / 2), fromZero);
}

void MacrosPanel::resized()
{
    const auto slots = computeColumnSlots (getLocalBounds());
    const auto m = computeMacroGridLayout (slots.control.getUnion (slots.value));

    for (size_t row = 0; row < m.rows.size(); ++row)
    {
        auto control = m.rows[row].control;
        auto value = m.rows[row].value;
        const auto colWidth = control.getWidth() / 2;
        const auto valueColWidth = value.getWidth() / 2;

        auto& left = knobs[row * 2];
        auto& right = knobs[row * 2 + 1];

        left.slider.setBounds (left.slider.boundsForRing (
            control.removeFromLeft (colWidth).withSizeKeepingCentre (m.knobSize, m.knobSize)));
        right.slider.setBounds (right.slider.boundsForRing (control.withSizeKeepingCentre (m.knobSize, m.knobSize)));

        left.valueCell = value.removeFromLeft (valueColWidth);
        right.valueCell = value;
    }
}

void MacrosPanel::paint (juce::Graphics& g)
{
    drawColumnCard (g, getLocalBounds().toFloat());

    const auto slots = computeColumnSlots (getLocalBounds());

    g.setColour (Palette::iconGlyph);
    g.setFont (juce::Font (juce::FontOptions().withHeight (TypeScale::icon)));
    g.drawText (juce::String::fromUTF8 ("\xe2\x97\x8d"), slots.icon, juce::Justification::centred); // "◍"

    g.setColour (Palette::textKnobLabel);
    g.setFont (labelFont (TypeScale::label, true));
    g.drawText ("MACROS", slots.label, juce::Justification::centred);

    g.setColour (Palette::textDim);
    g.setFont (labelFont (TypeScale::caption).italicised());
    g.drawFittedText ("shape the air", slots.caption, juce::Justification::centred, 2);

    const auto m = computeMacroGridLayout (slots.control.getUnion (slots.value));

    g.setColour (Palette::dividerColor);
    for (auto y : m.dividerYs)
        g.fillRect (juce::Rectangle<int> (m.rows[0].label.getX(), y, m.rows[0].label.getWidth(), 1));

    for (size_t row = 0; row < m.rows.size(); ++row)
    {
        auto labelArea = m.rows[row].label;
        const std::array<juce::Rectangle<int>, 2> labelCols { labelArea.removeFromLeft (labelArea.getWidth() / 2), labelArea };

        for (size_t col = 0; col < 2; ++col)
        {
            auto& knob = knobs[row * 2 + col];

            g.setColour (Palette::macroLabel);
            g.setFont (labelFont (10.0f, true));
            g.drawText (knob.caption, labelCols[col], juce::Justification::centred);

            // The readout counts along with the knob's glide, and lights in
            // the macro's colour while it is hovered or dragged.
            g.setColour (Palette::textDim.interpolatedWith (knob.accent, knob.slider.getHighlight()));
            g.setFont (labelFont (11.0f));
            // ATTACK/RELEASE read as a time (Root's, EnvelopeMacro.h), the
            // others as a percentage - the same text the host shows.
            const auto shown = toUnit ((float) knob.slider.getDisplayValue());
            const auto text = row == 0 ? envelopeMacroText (shown, col == 0 ? kRootAttackSeconds : kRootReleaseSeconds)
                                       : juce::String (juce::roundToInt (shown * 100.0f)) + "%";
            g.drawText (text, knob.valueCell, juce::Justification::centred);
        }
    }
}

} // namespace horizon::ui
