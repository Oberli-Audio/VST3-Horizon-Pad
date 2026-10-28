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

      # Editor F's overlays and states a mouse would reach: the menu with its
      # GUI scale submenu, the preset drawer, the save card, FILTER being dragged.
      HorizonPadGuiSnapshot --menu --out=/tmp/menu.png
      HorizonPadGuiSnapshot --drawer --out=/tmp/drawer.png
      HorizonPadGuiSnapshot --param=filterMacro=0.64 --drag=filterMacro --out=/tmp/turning.png

      # The Knob card at knob-sm, knob-md and knob-lg: rest, low, high, focused.
      HorizonPadGuiSnapshot --knob-sheet --scale=2 --out=/tmp/knobs.png

    Flags are applied in order: preset, then params.
*/

#include <JuceHeader.h>

#include "PluginProcessor.h"
#include "PluginEditor.h"

#include <iostream>
#include <mach/mach.h>

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
        bool menu = false, drawer = false, saveCard = false, title = false;
        juce::String drag;
        int reopen = 0;
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
            "  --knob-sheet            instead: the knob at 32 / 48 / 64 px, at rest, low, high and focused\n"
            "  --about / --licences    turn the panel to About (details / licence texts) before the snapshot\n"
            "  --menu                  open the menu with its GUI scale submenu\n"
            "  --drawer                open the preset drawer\n"
            "  --save-card             open the Save preset card\n"
            "  --drag=<id>             show this knob as dragged (its readout above the cap)\n"
            "  --title                 show the active preset's title on the sky from the start, at any FILTER\n"
            "  --reopen=<n>            instead: open, play and paint the editor n times; report memory (U.7)\n";
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
            else if (arg == "--menu")                       o.menu = true;
            else if (arg == "--drawer")                     o.drawer = true;
            else if (arg == "--save-card")                  o.saveCard = true;
            else if (arg == "--title")                      o.title = true;
            else if (arg.startsWith ("--drag="))            o.drag = value;
            else if (arg.startsWith ("--reopen="))          o.reopen = arg.fromFirstOccurrenceOf ("=", false, false).getIntValue();
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

    /** Rows: knob-sm, knob-md, knob-lg; columns: at pointer-rest (38
        degrees, how every illustration shows it), low, high, focused - on
        the Charcoal ground. */
    juce::Image renderKnobSheet (float scale)
    {
        using namespace horizon::ui;
        constexpr int cell = 96;
        const float sizes[] { 32.0f, 48.0f, 64.0f };
        const float values[] { (38.0f + 135.0f) / 270.0f, 0.1f, 0.9f, 0.62f };

        const auto width = cell * juce::numElementsInArray (values);
        const auto height = cell * juce::numElementsInArray (sizes);
        juce::Image image (juce::Image::ARGB, juce::roundToInt ((float) width * scale),
                           juce::roundToInt ((float) height * scale), true);
        juce::Graphics g (image);
        g.addTransform (juce::AffineTransform::scale (scale));
        g.fillAll (Tokens::ground);

        for (int row = 0; row < juce::numElementsInArray (sizes); ++row)
            for (int col = 0; col < juce::numElementsInArray (values); ++col)
            {
                const auto area = juce::Rectangle<float> ((float) (col * cell), (float) (row * cell), (float) cell, (float) cell)
                                      .withSizeKeepingCentre (sizes[row], sizes[row]);
                drawKnob (g, area, values[col], col == 3);
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

    // U.7, offline: open the editor, play a second of audio under it with its
    // frame callback running, paint it, close it - n times - and report the
    // resident memory after each round. A leak grows; a steady figure does not.
    if (options->reopen > 0)
    {
        const auto residentMB = []
        {
            mach_task_basic_info info {};
            mach_msg_type_number_t count = MACH_TASK_BASIC_INFO_COUNT;
            task_info (mach_task_self(), MACH_TASK_BASIC_INFO, (task_info_t) &info, &count);
            return (double) info.resident_size / (1024.0 * 1024.0);
        };

        juce::AudioBuffer<float> audio (2, kFrameSamples);
        juce::MidiBuffer midi;
        for (auto note : options->notes)
            midi.addEvent (juce::MidiMessage::noteOn (1, note, (juce::uint8) 100), 0);

        juce::Array<juce::var> rounds;
        double worstFrameMs = 0.0;

        for (int round = 0; round < options->reopen; ++round)
        {
            std::unique_ptr<juce::AudioProcessorEditor> ed (processor.createEditor());
            auto* hp = dynamic_cast<HorizonPadAudioProcessorEditor*> (ed.get());

            for (int frame = 0; frame < juce::roundToInt (1.0 / kFrameSeconds); ++frame)
            {
                audio.clear();
                const auto t0 = juce::Time::getMillisecondCounterHiRes();
                processor.processBlock (audio, midi);
                worstFrameMs = juce::jmax (worstFrameMs, juce::Time::getMillisecondCounterHiRes() - t0);
                midi.clear();

                if (hp != nullptr)
                    hp->advanceFrame ((double) (round * 100 + frame) * kFrameSeconds);
            }

            ed->createComponentSnapshot (ed->getLocalBounds(), true, options->scale);
            ed.reset();
            rounds.add (residentMB());
        }

        auto* json = new juce::DynamicObject();
        json->setProperty ("reopened", options->reopen);
        json->setProperty ("residentMB", rounds);
        json->setProperty ("growthMB", (double) rounds.getLast() - (double) rounds[juce::jmin (2, rounds.size() - 1)]);
        json->setProperty ("worstAudioBlockMs", worstFrameMs);
        std::cout << juce::JSON::toString (juce::var (json), true) << std::endl;
        return 0;
    }

    std::unique_ptr<juce::AudioProcessorEditor> editorHolder (processor.createEditor());
    auto* editor = dynamic_cast<HorizonPadAudioProcessorEditor*> (editorHolder.get());

    if (editor == nullptr)
        return 1;

    if (options->about)
        editor->showAbout (options->licences);

    if (options->title)
        editor->showRecalledPresetTitle();

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

        // Overlays open on the last frame, after any recall, as a hand would.
        if (frame == frames - 1)
        {
            if (options->menu)     editor->showMenu (true);
            if (options->drawer)   editor->showPresetDrawer();
            if (options->saveCard) editor->showSavePresetCard();

            if (options->drag.isNotEmpty() && ! editor->previewDrag (options->drag))
            {
                std::cerr << "Unknown parameter for --drag: " << options->drag << "\n";
                return 2;
            }
        }

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

    auto* ui = new juce::DynamicObject();
    ui->setProperty ("about", options->about);
    ui->setProperty ("licences", options->licences);
    ui->setProperty ("menu", options->menu);
    ui->setProperty ("drawer", options->drawer);
    ui->setProperty ("saveCard", options->saveCard);
    ui->setProperty ("drag", options->drag);
    ui->setProperty ("presetModified", processor.isActivePresetModified());
    ui->setProperty ("limiting", processor.isLimiting());
    json->setProperty ("ui", juce::var (ui));
    json->setProperty ("activePresetName", processor.getActivePresetName());

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
