#include "ControlStrip.h"
#include "../PluginProcessor.h"

namespace horizon::ui
{

namespace
{
    constexpr int kTitleHeight = 14;
    constexpr int kTitleGap = 16;               // section title to its controls
    constexpr int kColumnGap = 16;              // knob columns, and the two wheels
    constexpr int kGroupSpace = 24;             // either side of a group's divider
    constexpr int kControlsTop = kTitleHeight + kTitleGap;

    int rowWidth (int columns, int columnWidth)
    {
        return columns * columnWidth + (columns - 1) * kColumnGap;
    }
}

ControlStrip::ControlStrip (HorizonPadAudioProcessor& processorToUse)
    : processor (processorToUse),
      pitchWheel (processorToUse, WheelSlider::Kind::pitch),
      modWheel (processorToUse, WheelSlider::Kind::mod),
      outputMeter (processorToUse)
{
    addAndMakeVisible (pitchWheel);
    addAndMakeVisible (modWheel);

    struct Spec { const char* id; const char* label; const char* name; };

    const Spec padSpecs[] {
        { horizon::ParamID::rootVolume,     "ROOT",     "Root volume" },
        { horizon::ParamID::clearingVolume, "CLEARING", "Clearing volume" },
        { horizon::ParamID::expanseVolume,  "EXPANSE",  "Expanse volume" },
        { horizon::ParamID::bloomVolume,    "BLOOM",    "Bloom volume" },
    };

    const Spec macroSpecs[] {
        { horizon::ParamID::attackMacro,  "ATTACK",  "Attack" },
        { horizon::ParamID::releaseMacro, "RELEASE", "Release" },
        { horizon::ParamID::filterMacro,  "FILTER",  "Filter" },
        { horizon::ParamID::reverbMacro,  "REVERB",  "Reverb" },
        { horizon::ParamID::widthMacro,   "WIDTH",   "Width" },
        { horizon::ParamID::detuneMacro,  "DETUNE",  "Detune" },
    };

    for (const auto& s : padSpecs)
    {
        addAndMakeVisible (pads.add (new KnobColumn (processor, s.id, s.label, s.name, true)));
        padIds.add (s.id);
    }

    for (const auto& s : macroSpecs)
    {
        addAndMakeVisible (macros.add (new KnobColumn (processor, s.id, s.label, s.name, false)));
        macroIds.add (s.id);
    }

    addAndMakeVisible (outputMeter);
}

ControlStrip::~ControlStrip() = default;

AnimatedKnob* ControlStrip::findKnob (const juce::String& parameterId)
{
    if (const auto i = padIds.indexOf (parameterId); i >= 0)
        return &pads[i]->getKnob();

    if (const auto i = macroIds.indexOf (parameterId); i >= 0)
        return &macros[i]->getKnob();

    return nullptr;
}

void ControlStrip::advance (double now, float dt)
{
    pitchWheel.refreshFromProcessor();
    modWheel.refreshFromProcessor();

    for (int i = 0; i < pads.size(); ++i)
        pads[i]->advance (now, dt, processor.getLayerLevel (i));   // ROOT..BLOOM in LayerIndex order

    for (auto* m : macros)
        m->advance (now, dt);

    outputMeter.advance (now, dt);
}

void ControlStrip::beginWave (double startTime, double step, bool fromZero)
{
    auto t = startTime;

    for (auto* p : pads)
    {
        p->getKnob().beginWave (t, fromZero);
        t += step;
    }

    for (auto* m : macros)
    {
        m->getKnob().beginWave (t, fromZero);
        t += step;
    }
}

void ControlStrip::resized()
{
    const auto wheelsWidth = rowWidth (2, WheelSlider::kWidth);
    const auto padsWidth = rowWidth (pads.size(), KnobColumn::kWidth);
    const auto macrosWidth = rowWidth (macros.size(), KnobColumn::kWidth);

    auto x = 0;
    const auto nextGroup = [&] (size_t index, const char* title, int width)
    {
        groups[index] = { title, x, width };
        x += width;

        if (index < dividerXs.size())
        {
            dividerXs[index] = x + kGroupSpace;
            x += 2 * kGroupSpace + 1;
        }
    };

    nextGroup (0, "WHEELS", wheelsWidth);
    nextGroup (1, "PADS", padsWidth);
    nextGroup (2, "MACROS", macrosWidth);
    nextGroup (3, "OUTPUT", juce::jmin (OutputMeter::kWidth, getWidth() - x));

    // The wheels' focus ring reaches kFocusRoom above their track.
    const auto wheelTop = kControlsTop - WheelSlider::kFocusRoom;
    pitchWheel.setBounds (groups[0].x, wheelTop, WheelSlider::kWidth, WheelSlider::kHeight);
    modWheel.setBounds (groups[0].x + WheelSlider::kWidth + kColumnGap, wheelTop, WheelSlider::kWidth, WheelSlider::kHeight);

    for (int i = 0; i < pads.size(); ++i)
        pads[i]->setBounds (groups[1].x - KnobColumn::kOverhang + i * (KnobColumn::kWidth + kColumnGap), kControlsTop,
                            KnobColumn::kBoundsWidth, KnobColumn::kHeightWithLevel);

    for (int i = 0; i < macros.size(); ++i)
        macros[i]->setBounds (groups[2].x - KnobColumn::kOverhang + i * (KnobColumn::kWidth + kColumnGap), kControlsTop,
                              KnobColumn::kBoundsWidth, KnobColumn::kHeight);

    outputMeter.setBounds (groups[3].x, kControlsTop, groups[3].width, OutputMeter::kHeight);
}

void ControlStrip::paint (juce::Graphics& g)
{
    for (const auto& group : groups)
        type::draw (g, group.title, juce::Rectangle<float> ((float) group.x, 0.0f, (float) group.width + 40.0f, (float) kTitleHeight),
                    type::section(), Tokens::inkMuted);

    g.setColour (Tokens::line);

    for (auto x : dividerXs)
        g.fillRect (x, 0, 1, getHeight());
}

} // namespace horizon::ui
