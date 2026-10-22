/*
    HorizonPadGuiSnapshot
    =====================

    Renders the real editor offscreen to PNG - no host, no display, no audio
    device - the GUI's counterpart to HorizonPadSoundTool. It links the same
    shared plugin code, builds the real processor and editor, renders audio
    through processBlock() (so the pad cards' level glow and the meter show
    what the DSP actually produces), and steps the editor's animation clock
    itself, 60 frames per second of simulated time.

    Use it to check a GUI change at states that are slow to reach by hand:
    every end of the FILTER time-of-day range, the haze at full REVERB, a
    preset title mid-fade, the sunrise on open frame by frame. It prints one
    JSON object to stdout echoing the state it rendered (playbook rule 28).

    Usage examples
    --------------
      # The default patch, settled (the sunrise on open takes ~1.5 s).
      HorizonPadGuiSnapshot --out=/tmp/default.png

      # Night: FILTER at 0%, with a chord held, at 2x (Retina) scale.
      HorizonPadGuiSnapshot --param=filterMacro=0 --notes=48,55,60,64 --scale=2 --out=/tmp/night.png

      # A preset recall at 3 s, captured 0.25 s later (wave and title mid-way).
      HorizonPadGuiSnapshot --recall=Frostklang --recall-at=3 --time=3.25 --out=/tmp/recall.png

      # The sunrise on open, every 0.1 s for 2 s.
      HorizonPadGuiSnapshot --time=2 --frames=/tmp/sunrise --frame-every=0.1 --out=/tmp/last.png

      # The ring knob at rest, hovered, half-dragged and dragged - states an
      # offscreen editor cannot reach, since nothing moves a mouse over it.
      HorizonPadGuiSnapshot --knob-sheet --scale=2 --out=/tmp/knobs.png

    Flags are applied in order: preset, then params.
*/

#include <JuceHeader.h>

#include "PluginProcessor.h"
#include "PluginEditor.h"

#include <iostream>

namespace
{
    struct Options
    {
        juce::File out;
        juce::String preset, recall;
        std::vector<std::pair<juce::String, float>> params;
        std::vector<int> notes;
        double time = 3.0, recallAt = -1.0, frameEvery = 0.0;
        float scale = 1.0f;
        juce::File framesDir;
        bool knobSheet = false;
        bool about = false, licences = false;
    };

    void printUsage()
    {
        std::cout <<
            "HorizonPadGuiSnapshot - renders the real editor offscreen to PNG\n"
            "  --out=<png>             output file (required)\n"
            "  --preset=<name>         start from this factory preset\n"
            "  --param=<id>=<0..1>     set a parameter (repeatable), after --preset\n"
            "  --notes=60,64,67        hold these notes (velocity 100) from the start\n"
            "  --time=<s>              simulated seconds to run before the snapshot (default 3)\n"
            "  --recall=<name>         recall this factory preset at --recall-at seconds\n"
            "  --recall-at=<s>         when to recall it (default: halfway through --time)\n"
            "  --frames=<dir>          also write frame_NNNN.png every --frame-every seconds\n"
            "  --frame-every=<s>       frame interval for --frames (default 0.1)\n"
            "  --scale=<factor>        pixel scale, e.g. 2 for Retina (default 1)\n"
            "  --knob-sheet            instead: the ring knob at rest / hover / drag, per accent colour\n"
            "  --about / --licences    open the About box (diagnostics / licence texts) before the snapshot\n";
    }

    std::optional<Options> parse (int argc, char** argv)
    {
        Options o;

        for (int i = 1; i < argc; ++i)
        {
            const auto arg = juce::String::fromUTF8 (argv[i]);   // preset names are not all ASCII
            const auto value = arg.fromFirstOccurrenceOf ("=", false, false);

            if (arg == "--help" || arg == "-h")             return std::nullopt;
            else if (arg.startsWith ("--out="))             o.out = juce::File::getCurrentWorkingDirectory().getChildFile (value);
            else if (arg.startsWith ("--preset="))          o.preset = value;
            else if (arg.startsWith ("--recall="))          o.recall = value;
            else if (arg.startsWith ("--recall-at="))       o.recallAt = value.getDoubleValue();
            else if (arg.startsWith ("--time="))            o.time = value.getDoubleValue();
            else if (arg.startsWith ("--scale="))           o.scale = value.getFloatValue();
            else if (arg.startsWith ("--frames="))          o.framesDir = juce::File::getCurrentWorkingDirectory().getChildFile (value);
            else if (arg.startsWith ("--frame-every="))     o.frameEvery = value.getDoubleValue();
            else if (arg == "--knob-sheet")                 o.knobSheet = true;
            else if (arg == "--about")                      o.about = true;
            else if (arg == "--licences")                   o.licences = o.about = true;
            else if (arg.startsWith ("--param="))
                o.params.emplace_back (value.upToFirstOccurrenceOf ("=", false, false),
                                       value.fromFirstOccurrenceOf ("=", false, false).getFloatValue());
            else if (arg.startsWith ("--notes="))
            {
                for (auto& n : juce::StringArray::fromTokens (value, ",", {}))
                    if (n.trim().isNotEmpty())
                        o.notes.push_back (n.trim().getIntValue());
            }
            else
            {
                std::cerr << "Unknown flag: " << arg << "\n";
                return std::nullopt;
            }
        }

        if (o.out == juce::File())
            return std::nullopt;

        if (o.recall.isNotEmpty() && o.recallAt < 0.0)
            o.recallAt = o.time * 0.5;

        if (o.framesDir != juce::File() && o.frameEvery <= 0.0)
            o.frameEvery = 0.1;

        return o;
    }

    int findPreset (const HorizonPadAudioProcessor& processor, const juce::String& name)
    {
        const auto& presets = processor.getPresets();

        for (size_t i = 0; i < presets.size(); ++i)
            if (presets[i].slug == name || presets[i].name.equalsIgnoreCase (name))   // slug (presets.csv) or display name
                return (int) i;

        return -1;
    }

    bool writePng (const juce::Image& image, const juce::File& file)
    {
        file.getParentDirectory().createDirectory();
        file.deleteFile();
        juce::FileOutputStream stream (file);
        return stream.openedOk() && juce::PNGImageFormat().writeImageToStream (image, stream);
    }

    /** Rows: a pad knob (78 px) and a macro knob (37 px) per accent; columns:
        rest, hover, half-way through a drag, dragged. */
    juce::Image renderKnobSheet (float scale)
    {
        using namespace horizon::ui;
        constexpr int cell = 110;
        const juce::Colour accents[] { Palette::layerAccents[0], Palette::layerAccents[1],
                                       Palette::layerAccents[2], Palette::layerAccents[3] };
        const float states[][2] { { 0.0f, 0.0f }, { 1.0f, 0.0f }, { 1.0f, 0.5f }, { 1.0f, 1.0f } };
        const auto start = juce::MathConstants<float>::pi * 1.25f;
        const auto end = juce::MathConstants<float>::pi * 2.75f;

        const auto width = cell * juce::numElementsInArray (states);
        const auto height = cell * 2 * juce::numElementsInArray (accents);
        juce::Image image (juce::Image::ARGB, juce::roundToInt ((float) width * scale),
                           juce::roundToInt ((float) height * scale), true);
        juce::Graphics g (image);
        g.addTransform (juce::AffineTransform::scale (scale));
        g.fillAll (Palette::cardBg);

        for (int a = 0; a < juce::numElementsInArray (accents); ++a)
            for (int row = 0; row < 2; ++row)
                for (int col = 0; col < juce::numElementsInArray (states); ++col)
                {
                    const auto size = row == 0 ? 78.0f : 37.0f;
                    const auto area = juce::Rectangle<float> ((float) (col * cell), (float) ((a * 2 + row) * cell),
                                                              (float) cell, (float) cell).withSizeKeepingCentre (size, size);
                    drawRingKnob (g, area, 0.62f, start, end, accents[a], Palette::knobTrack,
                                  states[col][0], states[col][1]);
                }

        return image;
    }
}

int main (int argc, char** argv)
{
    const auto options = parse (argc, argv);

    if (! options)
    {
        printUsage();
        return 2;
    }

    juce::ScopedJuceInitialiser_GUI gui;

    if (options->knobSheet)
        return writePng (renderKnobSheet (options->scale), options->out) ? 0 : 1;

    constexpr double kSampleRate = 48000.0;
    constexpr int kFrameSamples = 800;      // one 60 fps frame at 48 kHz
    constexpr double kFrameSeconds = (double) kFrameSamples / kSampleRate;

    HorizonPadAudioProcessor processor;
    processor.setDeterministicSeed (1);
    processor.setPlayConfigDetails (0, 2, kSampleRate, kFrameSamples);
    processor.prepareToPlay (kSampleRate, kFrameSamples);

    if (options->preset.isNotEmpty())
    {
        const auto index = findPreset (processor, options->preset);

        if (index < 0)
        {
            std::cerr << "Unknown preset: " << options->preset << "\n";
            return 2;
        }

        processor.applyFactoryPreset (index);
    }

    for (const auto& [id, value] : options->params)
    {
        auto* param = processor.getAPVTS().getParameter (id);

        if (param == nullptr)
        {
            std::cerr << "Unknown parameter: " << id << "\n";
            return 2;
        }

        param->setValueNotifyingHost (value);   // normalised 0..1, like the sound tool
    }

    const auto recallIndex = options->recall.isNotEmpty() ? findPreset (processor, options->recall) : -1;

    if (options->recall.isNotEmpty() && recallIndex < 0)
    {
        std::cerr << "Unknown preset: " << options->recall << "\n";
        return 2;
    }

    std::unique_ptr<juce::AudioProcessorEditor> editorHolder (processor.createEditor());
    auto* editor = dynamic_cast<HorizonPadAudioProcessorEditor*> (editorHolder.get());

    if (editor == nullptr)
        return 1;

    if (options->about)
        editor->showAbout (options->licences);

    juce::AudioBuffer<float> audio (2, kFrameSamples);
    juce::MidiBuffer midi;

    for (auto note : options->notes)
        midi.addEvent (juce::MidiMessage::noteOn (1, note, (juce::uint8) 100), 0);

    const auto frames = juce::jmax (1, juce::roundToInt (options->time / kFrameSeconds));
    const auto framesPerDump = options->frameEvery > 0.0 ? juce::jmax (1, juce::roundToInt (options->frameEvery / kFrameSeconds)) : 0;
    bool recalled = false;
    int dumped = 0;

    for (int frame = 0; frame < frames; ++frame)
    {
        const auto now = (double) frame * kFrameSeconds;

        if (recallIndex >= 0 && ! recalled && now >= options->recallAt)
        {
            processor.applyFactoryPreset (recallIndex);
            recalled = true;
        }

        audio.clear();
        processor.processBlock (audio, midi);
        midi.clear();

        editor->advanceFrame (now);

        if (framesPerDump > 0 && frame % framesPerDump == 0)
            writePng (editor->createComponentSnapshot (editor->getLocalBounds(), true, options->scale),
                      options->framesDir.getChildFile ("frame_" + juce::String (dumped++).paddedLeft ('0', 4) + ".png"));
    }

    const auto ok = writePng (editor->createComponentSnapshot (editor->getLocalBounds(), true, options->scale), options->out);

    // Echo the state that was rendered.
    auto* json = new juce::DynamicObject();
    json->setProperty ("out", options->out.getFullPathName());
    json->setProperty ("simulatedSeconds", (double) frames * kFrameSeconds);
    json->setProperty ("scale", options->scale);

    auto* params = new juce::DynamicObject();
    for (auto* p : processor.getParameters())
        if (auto* withId = dynamic_cast<juce::AudioProcessorParameterWithID*> (p))
            params->setProperty (withId->getParameterID(), withId->getValue());
    json->setProperty ("activeParams", juce::var (params));

    json->setProperty ("activePresetIndex", processor.getActivePresetIndex());
    json->setProperty ("notes", [&] { juce::Array<juce::var> a; for (auto n : options->notes) a.add (n); return a; }());

    juce::Array<juce::var> levels;
    for (int i = 0; i < horizon::kNumLayers; ++i)
        levels.add (juce::Decibels::gainToDecibels (processor.getLayerLevel (i), -100.0f));
    json->setProperty ("layerLevelsDb", levels);
    json->setProperty ("outputLevelDb", juce::Decibels::gainToDecibels (processor.getOutputLevel(), -100.0f));

    std::cout << juce::JSON::toString (juce::var (json), true) << std::endl;

    editorHolder.reset();
    return ok ? 0 : 1;
}
