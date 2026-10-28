#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "dsp/EnvelopeMacro.h"

#include <limits>

using namespace horizon;

namespace
{
    juce::String percentText (float value, int)
    {
        return juce::String (juce::roundToInt (value)) + " %";
    }

    float percentValue (const juce::String& text)
    {
        return juce::jlimit (0.0f, kParamScale, text.getFloatValue());
    }

    // The size last chosen on this machine, for every instance: a small file
    // in the settings folder (D.6.4). Earlier builds kept it under the
    // company's earlier name (0.1.0: Quelle Music; the newest found is copied
    // once); before 0.1.0 it sat beside UserPresets.xml in ~/Library/Quelle
    // Music (moved once).
    std::unique_ptr<juce::PropertiesFile> openEditorSettings()
    {
        const auto file = PresetFile::settingsFolder().getChildFile ("EditorSettings.xml");

        for (const auto& folder : PresetFile::previousSettingsFolders())
        {
            if (const auto previous = folder.getChildFile ("EditorSettings.xml"); ! file.exists() && previous.existsAsFile())
                if (file.getParentDirectory().createDirectory())
                    previous.copyFileTo (file);
        }

        if (const auto old = PresetFile::legacyFolder().getChildFile ("EditorSettings.xml"); ! file.exists() && old.existsAsFile())
            if (file.getParentDirectory().createDirectory())
                old.moveFileTo (file);

        juce::PropertiesFile::Options o;
        o.storageFormat = juce::PropertiesFile::storeAsXML;
        return std::make_unique<juce::PropertiesFile> (file, o);
    }

    constexpr float kMinEditorScale = 0.75f;
    constexpr float kMaxEditorScale = 2.0f;

    /** FILTER/DETUNE macro mapping: 0.5 = the sound design's own validated
        value (no change), <0.5 halves it, >0.5 doubles it, on a log2 curve so
        the knob feels even in both directions. */
    float macroMultiplier (float macroValue) noexcept
    {
        return std::pow (2.0f, (juce::jlimit (0.0f, 1.0f, macroValue) - 0.5f) * 2.0f);
    }

    // ATTACK/RELEASE: attackReleaseTimeScale() and its text live in
    // dsp/EnvelopeMacro.h, shared with the editor's readout.

    /** The ten (and only ten) host-automatable parameter IDs, in the fixed
        order used everywhere a "vols/macros" array is needed: Root,
        Clearing, Expanse, Bloom, then Attack, Release, Filter, Reverb,
        Width, Detune. The first eight are ordered so that a controller that
        maps its knobs to a plugin's first N host parameters in order - e.g.
        a Launchkey's 8 knobs in Ableton Live's generic Device-knob mode - lands
        knobs 1-4 on the four pad volumes and knobs 5-8 on ATTACK/RELEASE/
        FILTER/REVERB, matching the physical Launchkey layout this GUI's
        8-column knob row was designed to mirror (see PluginEditor.h).
        WIDTH and DETUNE, macros 5 and 6, have no dedicated hardware knob and
        sit in the params 9-10 tail. */
    const std::array<const char*, (size_t) kNumLayers>& volumeIds()
    {
        static const std::array<const char*, (size_t) kNumLayers> ids {
            ParamID::rootVolume, ParamID::clearingVolume, ParamID::expanseVolume, ParamID::bloomVolume
        };
        return ids;
    }

    const std::array<const char*, (size_t) kNumGlobalParams>& macroIds()
    {
        static const std::array<const char*, (size_t) kNumGlobalParams> ids {
            ParamID::attackMacro, ParamID::releaseMacro, ParamID::filterMacro, ParamID::reverbMacro,
            ParamID::widthMacro, ParamID::detuneMacro
        };
        return ids;
    }
}

//==============================================================================
juce::AudioProcessorValueTreeState::ParameterLayout HorizonPadAudioProcessor::createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    // The ranges, defaults and IDs are parameters.csv's (0..100 since state
    // version 2); test parameter_table_matches_plugin diffs this layout against
    // it. Written by hand rather than Params::createParameterLayout() only
    // because ATTACK/RELEASE show a time, which the table cannot express yet.
    const juce::NormalisableRange<float> hostRange { 0.0f, kParamScale };

    auto addPercent = [&] (const char* id, const char* name, float defaultValue)
    {
        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { id, 1 },
            name,
            hostRange,
            defaultValue,
            juce::AudioParameterFloatAttributes()
                .withStringFromValueFunction (percentText)
                .withValueFromStringFunction (percentValue)
                .withLabel ("%")));
    };

    // ATTACK/RELEASE show Root's time, in the GUI and in the host's lanes
    // (EnvelopeMacro.h) - the same text both places (owner, 2026-10-17).
    auto addEnvelopeTime = [&] (const char* id, const char* name, float defaultValue, float referenceSeconds)
    {
        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { id, 1 },
            name,
            hostRange,
            defaultValue,
            juce::AudioParameterFloatAttributes()
                .withStringFromValueFunction ([referenceSeconds] (float v, int) { return envelopeMacroText (toUnit (v), referenceSeconds); })
                .withValueFromStringFunction ([referenceSeconds] (const juce::String& t) { return envelopeMacroFromText (t, referenceSeconds) * kParamScale; })));
    };

    // The ten (and only ten) host-automatable parameters: four pad volumes,
    // then the six macros. ATTACK/RELEASE/FILTER/REVERB sit right after the
    // volumes (params 5-8) so a hardware controller that maps its knobs to
    // the plugin's first 8 host parameters - e.g. a Launchkey in Ableton Live's
    // generic Device-knob mode - lands its knobs 5-8 on them (see
    // macroIds() above for why the order matters).
    // Volume defaults are the Lagerfeuer balance scaled by the same 0.677 the
    // preset itself took in the 2026-09-23 loudness-matching pass. Before that
    // scaling they were the pre-match values, which left a fresh instance
    // ~3.4 dB hotter than every preset in the bank and peaking above the output
    // limiter's knee - i.e. the very first thing a user heard was the only
    // patch in the product that was being limited. Sound-design v2
    // (2026-09-26) rescaled Lagerfeuer again (net -0.4 dB after the register pass), when WIDTH stopped
    // losing level and REVERB stopped dipping (docs/sound-design-v2.md), and
    // these follow it.
    const auto defaultOf = [] (const char* id)
    {
        for (const auto& spec : kParamSpecs)
            if (juce::String (spec.id) == id)
                return spec.defaultValue;

        jassertfalse;
        return 0.0f;
    };

    addPercent (ParamID::rootVolume,     "Root",     defaultOf (ParamID::rootVolume));
    addPercent (ParamID::clearingVolume, "Clearing", defaultOf (ParamID::clearingVolume));
    addPercent (ParamID::expanseVolume,  "Expanse",  defaultOf (ParamID::expanseVolume));
    addPercent (ParamID::bloomVolume,    "Bloom",    defaultOf (ParamID::bloomVolume));
    // ATTACK/RELEASE: Lagerfeuer's 73.13% on the curve of A-009 (Root
    // attack 0.64 s, release 0.70 s).
    addEnvelopeTime (ParamID::attackMacro,  "Attack",  defaultOf (ParamID::attackMacro), kRootAttackSeconds);
    addEnvelopeTime (ParamID::releaseMacro, "Release", defaultOf (ParamID::releaseMacro), kRootReleaseSeconds);
    addPercent (ParamID::filterMacro,    "Filter",   defaultOf (ParamID::filterMacro));
    addPercent (ParamID::reverbMacro,    "Reverb",   defaultOf (ParamID::reverbMacro));
    addPercent (ParamID::widthMacro,     "Width",    defaultOf (ParamID::widthMacro));
    // 0.0 since 2026-10-07: the owner preferred the default patch at DETUNE 0%
    // over the designed detune (50%) in the blind A/B of 2026-10-06. 50% still
    // reproduces the designed detune exactly; the defaults stay Lagerfeuer's.
    addPercent (ParamID::detuneMacro,    "Detune",   defaultOf (ParamID::detuneMacro));

    return layout;
}

//==============================================================================
HorizonPadAudioProcessor::HorizonPadAudioProcessor()
    : juce::AudioProcessor (BusesProperties()
                                .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMETERS", createParameterLayout())
{
    layers = { &warmFoundation, &analogEnsemble, &airyChoir, &motionPad };

    for (int i = 0; i < kNumLayers; ++i)
    {
        volumeParams[(size_t) i] = apvts.getRawParameterValue (volumeIds()[(size_t) i]);
        jassert (volumeParams[(size_t) i] != nullptr);
    }

    for (int i = 0; i < kNumGlobalParams; ++i)
    {
        macroParams[(size_t) i] = apvts.getRawParameterValue (macroIds()[(size_t) i]);
        jassert (macroParams[(size_t) i] != nullptr);
    }

    // The parameter defaults above are already Lagerfeuer's values, so there
    // is nothing else to seed at construction. Deliberately no
    // setValueNotifyingHost() here - hosts dislike parameter traffic from a
    // processor constructor.
    //
    // The preset the defaults equal is Init (Lagerfeuer, first in the bank
    // since 2026-10-21; presetlint.py render asserts Init = the table). Point
    // the preset bar at it, so the name shown at startup is the sound heard.
    // Matched by value, not by name: renaming or reordering the bank cannot
    // break it (test startup_program_matches_defaults).
    setActivePreset (PresetKind::none, {}, {});
    {
        const auto& factory = getFactoryPresets();

        for (size_t p = 0; p < factory.size(); ++p)
        {
            bool same = true;

            for (int i = 0; i < kNumLayers; ++i)
                same = same && std::abs (factory[p].volumes[(size_t) i] - volumeParams[(size_t) i]->load()) < 1.0e-2f;

            for (int i = 0; i < kNumGlobalParams; ++i)
                same = same && std::abs (factory[p].macros[(size_t) i] - macroParams[(size_t) i]->load()) < 1.0e-2f;

            if (same)
            {
                setActivePreset (PresetKind::factory, factory[p].slug, factory[p].name);
                break;
            }
        }
    }

    // Both A/B buffers start out identical, mirroring the live defaults - the
    // mockup's buffers also start as two copies of the same starting state.
    for (int i = 0; i < kNumLayers; ++i)
    {
        buffers[0].vols[(size_t) i].store (volumeParams[(size_t) i]->load());
        buffers[1].vols[(size_t) i].store (volumeParams[(size_t) i]->load());
    }

    for (int i = 0; i < kNumGlobalParams; ++i)
    {
        buffers[0].macros[(size_t) i].store (macroParams[(size_t) i]->load());
        buffers[1].macros[(size_t) i].store (macroParams[(size_t) i]->load());
    }

    // Keep the active buffer's snapshot in sync with every parameter change,
    // wherever it comes from (GUI, host automation, or a preset/program
    // recall) - see the class-level thread-safety note in the header.
    for (auto* id : volumeIds())
        apvts.addParameterListener (id, this);

    for (auto* id : macroIds())
        apvts.addParameterListener (id, this);

    // The Factory folder is rewritten on the first run of each version, the
    // User folder only ever read here (D.6.5).
    UserPresetStore::syncFactoryFolder();
    userPresets = UserPresetStore::scan();

    // Every instance opens at the size last chosen on this machine.
    {
        editorScale = juce::jlimit (kMinEditorScale, kMaxEditorScale,
                                    (float) openEditorSettings()->getDoubleValue ("editorScale", 1.0));
    }
}

HorizonPadAudioProcessor::~HorizonPadAudioProcessor()
{
    for (auto* id : volumeIds())
        apvts.removeParameterListener (id, this);

    for (auto* id : macroIds())
        apvts.removeParameterListener (id, this);
}

//==============================================================================
void HorizonPadAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    const auto maxBlock = juce::jmax (1, samplesPerBlock);

    layerBuffer.setSize (2, maxBlock, false, true, false);
    layerBuffer.clear();

    for (auto* layer : layers)
        layer->prepare (sampleRate, maxBlock);

    fxChain.prepare (sampleRate, maxBlock);

    for (auto& g : layerGain)
    {
        g.reset (sampleRate, 0.03);
        g.setCurrentAndTargetValue (0.0f);
    }

    for (int i = 0; i < kNumLayers; ++i)
        layerGain[(size_t) i].setCurrentAndTargetValue (toUnit (volumeParams[(size_t) i]->load()));

    // One-pole DC blocker: y = x - x1 + R*y1, R = 1 - 2*pi*fc/fs.
    dcBlockerCoeff = 1.0f - (juce::MathConstants<float>::twoPi * kDcBlockerHz / (float) sampleRate);
    dcBlockerX1.fill (0.0f);
    dcBlockerY1.fill (0.0f);

    meterWindowSamples = juce::jmax (1, juce::roundToInt (sampleRate * kMeterWindowSeconds));
    meterSampleCount = 0;
    outputSumSquares = 0.0f;
    layerSumSquares.fill (0.0f);
    limitedInWindow = false;
    outputLevel.store (0.0f, std::memory_order_relaxed);
    limiting.store (false, std::memory_order_relaxed);

    for (auto& level : layerLevels)
        level.store (0.0f, std::memory_order_relaxed);

    allNotesOff (true);
}

void HorizonPadAudioProcessor::releaseResources()
{
    for (auto* layer : layers)
        layer->reset();

    fxChain.reset();

    dcBlockerX1.fill (0.0f);
    dcBlockerY1.fill (0.0f);
}

bool HorizonPadAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    // Stereo out only; no inputs (it is an instrument).
    if (layouts.getMainInputChannels() != 0)
        return false;

    const auto out = layouts.getMainOutputChannelSet();
    return out == juce::AudioChannelSet::stereo() || out == juce::AudioChannelSet::mono();
}

//==============================================================================
// One program for the host (D.5.3): the bank lives in the plugin. With more,
// the VST3 wrapper exports a "Program" parameter that takes one of Ableton Live's
// auto-populated slots and is automatable; Ableton Live shows no program list anyway.
int HorizonPadAudioProcessor::getNumPrograms()                     { return 1; }
int HorizonPadAudioProcessor::getCurrentProgram()                  { return 0; }
void HorizonPadAudioProcessor::setCurrentProgram (int)             {}
void HorizonPadAudioProcessor::changeProgramName (int, const juce::String&) {}

const juce::String HorizonPadAudioProcessor::getProgramName (int)
{
    return getActivePresetName();
}

void HorizonPadAudioProcessor::applyFactoryPreset (int index)
{
    const auto& presets = getFactoryPresets();

    if (! juce::isPositiveAndBelow (index, (int) presets.size()))
        return;

    // The active-preset fields are set before the counter moves, and the
    // counter before any parameter does, so an editor that sees the new
    // count reads the right preset name and catches every knob before it
    // starts to glide (see getPresetRecallCount()).
    setActivePreset (PresetKind::factory, presets[(size_t) index].slug, presets[(size_t) index].name);
    presetRecallCount.fetch_add (1, std::memory_order_release);

    applyPreset (presets[(size_t) index]);

    sendChangeMessage();
}

void HorizonPadAudioProcessor::applyPreset (const Preset& preset)
{
    // See the freezeBrightnessRequested doc comment: an already-sounding
    // note should keep its old FILTER timbre across this jump, not snap to
    // the incoming preset's value.
    freezeBrightnessRequested.store (true, std::memory_order_release);

    auto setParam = [&] (const char* id, float value)
    {
        if (auto* p = apvts.getParameter (id))
            p->setValueNotifyingHost (p->convertTo0to1 (value));
    };

    for (int i = 0; i < kNumLayers; ++i)
        setParam (volumeIds()[(size_t) i], preset.volumes[(size_t) i]);

    for (int i = 0; i < kNumGlobalParams; ++i)
        setParam (macroIds()[(size_t) i], preset.macros[(size_t) i]);

    // Wheels spring back to rest on a preset change, like a real keyboard.
    performanceState.setPitchBendSemitones (0.0f);
    performanceState.setModAmount (0.0f);
}

//==============================================================================
void HorizonPadAudioProcessor::parameterChanged (const juce::String& parameterID, float newValue)
{
    // Keep the *active* buffer's snapshot in sync with whatever just changed -
    // this can be called from the audio thread (host automation) or the
    // message thread (a GUI edit), so only atomics may be touched here.
    const auto bufferIndex = (size_t) juce::jlimit (0, 1, activeBufferIndex.load (std::memory_order_relaxed));
    auto& buffer = buffers[bufferIndex];

    for (int i = 0; i < kNumLayers; ++i)
    {
        if (parameterID == volumeIds()[(size_t) i])
        {
            buffer.vols[(size_t) i].store (newValue, std::memory_order_relaxed);
            return;
        }
    }

    for (int i = 0; i < kNumGlobalParams; ++i)
    {
        if (parameterID == macroIds()[(size_t) i])
        {
            buffer.macros[(size_t) i].store (newValue, std::memory_order_relaxed);
            return;
        }
    }
}

void HorizonPadAudioProcessor::switchBuffer (int index)
{
    index = juce::jlimit (0, 1, index);

    if (index == activeBufferIndex.load (std::memory_order_relaxed))
        return;

    // Same instant full-parameter jump as applyPreset() - see
    // freezeBrightnessRequested's doc comment.
    freezeBrightnessRequested.store (true, std::memory_order_release);

    // Set the new active index *before* pushing parameter changes, so the
    // parameterChanged() callbacks those changes trigger update the buffer we
    // just switched to, not the one we're switching away from.
    activeBufferIndex.store (index, std::memory_order_relaxed);
    bufferSwitchCount.fetch_add (1, std::memory_order_release);

    const auto& snapshot = buffers[(size_t) index];

    for (int i = 0; i < kNumLayers; ++i)
    {
        if (auto* p = apvts.getParameter (volumeIds()[(size_t) i]))
            p->setValueNotifyingHost (p->convertTo0to1 (snapshot.vols[(size_t) i].load (std::memory_order_relaxed)));
    }

    for (int i = 0; i < kNumGlobalParams; ++i)
    {
        if (auto* p = apvts.getParameter (macroIds()[(size_t) i]))
            p->setValueNotifyingHost (p->convertTo0to1 (snapshot.macros[(size_t) i].load (std::memory_order_relaxed)));
    }

    sendChangeMessage();
}

void HorizonPadAudioProcessor::copyActiveBufferToOtherBuffer()
{
    const auto activeIndex = (size_t) juce::jlimit (0, 1, activeBufferIndex.load (std::memory_order_relaxed));
    const auto otherIndex = 1 - activeIndex;

    auto& source = buffers[activeIndex];
    auto& dest = buffers[otherIndex];

    for (int i = 0; i < kNumLayers; ++i)
        dest.vols[(size_t) i].store (source.vols[(size_t) i].load (std::memory_order_relaxed), std::memory_order_relaxed);

    for (int i = 0; i < kNumGlobalParams; ++i)
        dest.macros[(size_t) i].store (source.macros[(size_t) i].load (std::memory_order_relaxed), std::memory_order_relaxed);

    sendChangeMessage();
}

//==============================================================================
void HorizonPadAudioProcessor::setActivePreset (PresetKind kind, const juce::String& slug, const juce::String& name)
{
    {
        const juce::SpinLock::ScopedLockType lock (presetLock);
        activePresetSlug = kind == PresetKind::none ? juce::String() : slug;
        activePresetName = kind == PresetKind::none ? juce::String() : name;
    }

    activePresetKind.store ((int) kind, std::memory_order_relaxed);
}

juce::String HorizonPadAudioProcessor::getActivePresetSlug() const
{
    const juce::SpinLock::ScopedLockType lock (presetLock);
    return activePresetSlug;
}

juce::String HorizonPadAudioProcessor::getActivePresetName() const
{
    const juce::SpinLock::ScopedLockType lock (presetLock);
    return activePresetName;
}

juce::String HorizonPadAudioProcessor::getStateNotice() const
{
    const juce::SpinLock::ScopedLockType lock (presetLock);
    return stateNotice;
}

int HorizonPadAudioProcessor::getActivePresetIndex() const
{
    const auto slug = getActivePresetSlug();

    switch (getActivePresetKind())
    {
        case PresetKind::factory:
            return findFactoryPreset (slug);

        case PresetKind::user:
            for (size_t i = 0; i < userPresets.size(); ++i)
                if (userPresets[i].slug == slug)
                    return (int) i;
            return -1;

        case PresetKind::none:
            break;
    }

    return -1;
}

bool HorizonPadAudioProcessor::isActivePresetModified() const
{
    const auto index = getActivePresetIndex();

    if (index < 0)
        return false;

    const auto& preset = getActivePresetKind() == PresetKind::factory ? getFactoryPresets()[(size_t) index]
                                                                      : userPresets[(size_t) index];

    // A thousandth of the knob is below anything a hand sets on purpose,
    // and above a float round trip through the host or the state.
    constexpr float kTolerance = 1.0e-3f * kParamScale;

    for (int i = 0; i < kNumLayers; ++i)
        if (std::abs (volumeParams[(size_t) i]->load (std::memory_order_relaxed) - preset.volumes[(size_t) i]) > kTolerance)
            return true;

    for (int i = 0; i < kNumGlobalParams; ++i)
        if (std::abs (macroParams[(size_t) i]->load (std::memory_order_relaxed) - preset.macros[(size_t) i]) > kTolerance)
            return true;

    return false;
}


void HorizonPadAudioProcessor::setEditorScale (float newScale)
{
    editorScale = juce::jlimit (kMinEditorScale, kMaxEditorScale, newScale);

    auto settings = openEditorSettings();
    settings->setValue ("editorScale", (double) editorScale);
    settings->saveIfNeeded();
}

//==============================================================================
void HorizonPadAudioProcessor::refreshUserPresets()
{
    userPresets = UserPresetStore::scan();

    // A user preset whose file went away (deleted in the Finder) leaves the
    // sound as it is and clears the name (D.6.5).
    if (getActivePresetKind() == PresetKind::user && getActivePresetIndex() < 0)
        setActivePreset (PresetKind::none, {}, {});

    sendChangeMessage();
}

void HorizonPadAudioProcessor::saveCurrentAsUserPreset (const juce::String& name)
{
    Preset preset;
    preset.name = name.isNotEmpty() ? name : "Untitled";
    preset.author = juce::SystemStats::getFullUserName().trim().isNotEmpty() ? juce::SystemStats::getFullUserName().trim()
                                                                            : juce::SystemStats::getLogonName();

    for (int i = 0; i < kNumLayers; ++i)
        preset.volumes[(size_t) i] = volumeParams[(size_t) i]->load (std::memory_order_relaxed);

    for (int i = 0; i < kNumGlobalParams; ++i)
        preset.macros[(size_t) i] = macroParams[(size_t) i]->load (std::memory_order_relaxed);

    juce::String error;

    if (auto saved = UserPresetStore::saveNew (preset, error))
    {
        userPresets.push_back (*saved);
        setActivePreset (PresetKind::user, saved->slug, saved->name);
    }

    sendChangeMessage();
}

void HorizonPadAudioProcessor::deleteUserPreset (int index)
{
    if (! juce::isPositiveAndBelow (index, (int) userPresets.size()))
        return;

    const auto deleted = userPresets[(size_t) index];
    UserPresetStore::remove (deleted);
    userPresets.erase (userPresets.begin() + index);

    // Deleting the loaded preset keeps the sound and clears the name (D.6.5).
    if (getActivePresetKind() == PresetKind::user && getActivePresetSlug() == deleted.slug)
        setActivePreset (PresetKind::none, {}, {});

    sendChangeMessage();
}

void HorizonPadAudioProcessor::applyUserPreset (int index)
{
    if (! juce::isPositiveAndBelow (index, (int) userPresets.size()))
        return;

    const auto preset = userPresets[(size_t) index];

    // Same order as applyFactoryPreset(): active-preset fields, then the
    // recall counter, then the parameters.
    setActivePreset (PresetKind::user, preset.slug, preset.name);
    presetRecallCount.fetch_add (1, std::memory_order_release);

    applyPreset (preset);

    sendChangeMessage();
}

juce::String HorizonPadAudioProcessor::importPreset (const juce::File& file)
{
    juce::String error;
    auto preset = PresetFile::read (file, error);

    if (! preset)
        return error;

    // An unchanged re-import of a file already in the User folder recalls it.
    if (file.getParentDirectory() == PresetFile::userFolder())
    {
        refreshUserPresets();

        for (size_t i = 0; i < userPresets.size(); ++i)
        {
            if (userPresets[i].file == file)
            {
                applyUserPreset ((int) i);
                return {};
            }
        }
    }

    auto copy = UserPresetStore::saveNew (*preset, error);

    if (! copy)
        return error;

    userPresets.push_back (*copy);
    applyUserPreset ((int) userPresets.size() - 1);
    return {};
}

juce::String HorizonPadAudioProcessor::exportPreset (const juce::File& file) const
{
    Preset preset;
    preset.name = getActivePresetName().isNotEmpty() ? getActivePresetName() : file.getFileNameWithoutExtension();
    preset.author = juce::SystemStats::getFullUserName().trim().isNotEmpty() ? juce::SystemStats::getFullUserName().trim()
                                                                            : juce::SystemStats::getLogonName();

    if (const auto index = getActivePresetIndex(); index >= 0 && getActivePresetKind() == PresetKind::factory)
        preset.category = getFactoryPresets()[(size_t) index].category;

    for (int i = 0; i < kNumLayers; ++i)
        preset.volumes[(size_t) i] = volumeParams[(size_t) i]->load (std::memory_order_relaxed);

    for (int i = 0; i < kNumGlobalParams; ++i)
        preset.macros[(size_t) i] = macroParams[(size_t) i]->load (std::memory_order_relaxed);

    return PresetFile::toXml (preset, false)->writeTo (file) ? juce::String() : "Cannot write " + file.getFullPathName();
}

//==============================================================================
int HorizonPadAudioProcessor::findFreeVoiceSlot()
{
    // 1) A slot that is neither held, sustained nor still sounding.
    for (int i = 0; i < kMaxVoices; ++i)
    {
        if (voiceSlots[(size_t) i].midiNote >= 0)
            continue;

        bool sounding = false;

        for (auto* layer : layers)
            sounding = sounding || layer->isVoiceActive (i);

        if (! sounding)
            return i;
    }

    // Oldest slot matching `eligible`, or -1.
    auto oldest = [this] (auto eligible)
    {
        int best = -1;
        auto bestOrder = std::numeric_limits<juce::uint32>::max();

        for (int i = 0; i < kMaxVoices; ++i)
        {
            const auto& slot = voiceSlots[(size_t) i];

            if (eligible (slot) && slot.order < bestOrder)
            {
                bestOrder = slot.order;
                best = i;
            }
        }

        return best;
    };

    // 2) The oldest slot that has been released but is still ringing out.
    if (const auto s = oldest ([] (const VoiceSlot& v) { return v.midiNote < 0; }); s >= 0)
        return s;

    // 3) The oldest note the sustain pedal is holding after its key came up -
    //    a player notices a held key cutting out far sooner than a pedalled one.
    if (const auto s = oldest ([] (const VoiceSlot& v) { return v.midiNote >= 0 && ! v.keyDown; }); s >= 0)
        return s;

    // 4) Everything is held: steal the oldest.
    return juce::jmax (0, oldest ([] (const VoiceSlot&) { return true; }));
}

void HorizonPadAudioProcessor::releaseSlot (int slot)
{
    voiceSlots[(size_t) slot].midiNote = -1;
    voiceSlots[(size_t) slot].keyDown = false;

    for (auto* layer : layers)
        layer->noteOff (slot);
}

void HorizonPadAudioProcessor::noteOn (int midiNote, float velocity)
{
    // Outside the register (C0-C7) the instrument is silent: no voice is
    // started, nothing is stolen for it. See horizon::kRegisterLowestNote.
    if (! horizon::isInRegister (midiNote))
        return;

    // The same key struck again while its previous note is still held or
    // pedalled: release the old voice into its tail rather than stacking a
    // second copy on top. With the sustain pedal down, repeated notes would
    // otherwise pile up identical voices, eat the voice budget and phase
    // against each other.
    for (int i = 0; i < kMaxVoices; ++i)
        if (voiceSlots[(size_t) i].midiNote == midiNote)
            releaseSlot (i);

    const auto slot = findFreeVoiceSlot();

    voiceSlots[(size_t) slot].midiNote = midiNote;
    voiceSlots[(size_t) slot].keyDown = true;
    voiceSlots[(size_t) slot].order = ++voiceOrderCounter;

    // Ambient pads want a gentle velocity curve, not a linear one.
    const auto shaped = 0.35f + 0.65f * std::sqrt (juce::jlimit (0.0f, 1.0f, velocity));

    // startNote() replaces the old killVoice()+noteOn() pair: if the slot is
    // still sounding (tiers 2-4 of findFreeVoiceSlot()), the layer ramps it
    // out over 5 ms and starts the new note when the ramp lands, instead of
    // cutting it mid-cycle. See LayerBase::startNote().
    for (auto* layer : layers)
        layer->startNote (slot, midiNote, shaped);
}

void HorizonPadAudioProcessor::noteOff (int midiNote)
{
    for (int i = 0; i < kMaxVoices; ++i)
    {
        auto& slot = voiceSlots[(size_t) i];

        if (slot.midiNote != midiNote || ! slot.keyDown)
            continue;

        if (sustainPedalDown)
            slot.keyDown = false;   // the pedal holds it; released when the pedal comes up
        else
            releaseSlot (i);
    }
}

void HorizonPadAudioProcessor::setSustainPedal (bool down)
{
    if (down == sustainPedalDown)
        return;

    sustainPedalDown = down;

    if (! down)
        for (int i = 0; i < kMaxVoices; ++i)
            if (voiceSlots[(size_t) i].midiNote >= 0 && ! voiceSlots[(size_t) i].keyDown)
                releaseSlot (i);
}

void HorizonPadAudioProcessor::allNotesOff (bool immediately)
{
    sustainPedalDown = false;

    for (int i = 0; i < kMaxVoices; ++i)
    {
        voiceSlots[(size_t) i].midiNote = -1;
        voiceSlots[(size_t) i].keyDown = false;

        for (auto* layer : layers)
        {
            if (immediately)
                layer->killVoice (i);
            else
                layer->noteOff (i);
        }
    }
}

void HorizonPadAudioProcessor::handleMidiMessage (const juce::MidiMessage& message)
{
    if (message.isNoteOn())
        noteOn (message.getNoteNumber(), message.getFloatVelocity());
    else if (message.isNoteOff())
        noteOff (message.getNoteNumber());
    else if (message.isAllNotesOff())
        allNotesOff (false);
    else if (message.isAllSoundOff())
        allNotesOff (true);
    else if (message.isSustainPedalOn())
        setSustainPedal (true);
    else if (message.isSustainPedalOff())
        setSustainPedal (false);
    else if (message.isPitchWheel())
    {
        // 14-bit, centred on 8192 -> -1..+1 -> +/- the wheel's semitone range.
        const auto normalised = ((float) message.getPitchWheelValue() - 8192.0f) / 8192.0f;
        performanceState.setPitchBendSemitones (juce::jlimit (-1.0f, 1.0f, normalised)
                                                * horizon::PerformanceState::kPitchBendRangeSemitones);
    }
    else if (message.isController() && message.getControllerNumber() == 1) // mod wheel (CC1)
    {
        performanceState.setModAmount ((float) message.getControllerValue() / 127.0f);
    }
}

//==============================================================================
void HorizonPadAudioProcessor::renderSegment (juce::AudioBuffer<float>& output, int startSample, int numSamples)
{
    // A segment longer than the prepared block (a host block larger than
    // prepareToPlay announced, D.5.1) renders in pieces of the prepared size;
    // it used to be clamped, leaving the rest of the block silent.
    while (numSamples > layerBuffer.getNumSamples())
    {
        const auto piece = layerBuffer.getNumSamples();
        renderSegment (output, startSample, piece);
        startSample += piece;
        numSamples -= piece;
    }

    const auto numChannels = juce::jmin (2, output.getNumChannels());

    if (numSamples <= 0 || numChannels <= 0)
        return;

    auto* outL = output.getWritePointer (0, startSample);
    auto* outR = numChannels > 1 ? output.getWritePointer (1, startSample) : nullptr;

    for (int i = 0; i < kNumLayers; ++i)
    {
        layerBuffer.clear (0, numSamples);
        layers[(size_t) i]->render (layerBuffer, numSamples);

        const auto* srcL = layerBuffer.getReadPointer (0);
        const auto* srcR = layerBuffer.getReadPointer (1);
        auto& gain = layerGain[(size_t) i];
        auto sumSquares = 0.0f;

        for (int n = 0; n < numSamples; ++n)
        {
            const auto g = gain.getNextValue();
            outL[n] += srcL[n] * g;

            if (outR != nullptr)
                outR[n] += srcR[n] * g;

            // The editor's per-pad level glow (see layerLevels); the mix
            // above is written exactly as before, this only reads.
            sumSquares += (srcL[n] * srcL[n] + srcR[n] * srcR[n]) * (g * g);
        }

        layerSumSquares[(size_t) i] += sumSquares;
    }
}

void HorizonPadAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;

    const auto numSamples = buffer.getNumSamples();
    buffer.clear();

    if (numSamples <= 0)
        return;

    // --- Automatable parameters: set smoothing targets and per-block macros.
    for (int i = 0; i < kNumLayers; ++i)
        layerGain[(size_t) i].setTargetValue (toUnit (volumeParams[(size_t) i]->load (std::memory_order_relaxed)));

    const auto macro = [this] (MacroIndex m) { return toUnit (macroParams[m]->load (std::memory_order_relaxed)); };
    const auto attackScale = attackReleaseTimeScale (macro (attackMacroIndex));
    const auto releaseScale = attackReleaseTimeScale (macro (releaseMacroIndex));
    const auto brightnessMul = macroMultiplier (macro (filterMacroIndex));
    const auto reverbSend = macro (reverbMacroIndex);
    const auto width = macro (widthMacroIndex);
    const auto detuneScale = macroMultiplier (macro (detuneMacroIndex));

    // Freeze every layer's currently sounding voices at *last* block's
    // brightness before this block's setMacros() below overwrites it with
    // whatever just got set (a new preset/buffer's FILTER value, if that's
    // what triggered this) - see freezeBrightnessRequested's doc comment.
    if (freezeBrightnessRequested.exchange (false, std::memory_order_acq_rel))
        for (auto* layer : layers)
            layer->freezeBrightnessForActiveVoices();

    for (auto* layer : layers)
    {
        layer->setMacros (attackScale, releaseScale, brightnessMul);
        layer->setWidth (width);   // each layer applies its own width profile
        layer->setDetune (detuneScale);
    }

    fxChain.setParameters (reverbSend);

    // Pushes performanceState's current pitch-bend/mod values into every
    // layer. Called once before the first segment and again after every MIDI
    // message below, so a pitch-wheel or mod-wheel (CC1) message that lands
    // mid-block - handleMidiMessage() updates performanceState, not the
    // layers directly - actually takes effect at that message's segment
    // boundary instead of silently waiting for the next processBlock() call,
    // matching the sample-accuracy the segment-splitting below already gives
    // note-on/note-off.
    auto applyPerformanceState = [&]
    {
        const auto pb = performanceState.getPitchBendSemitones();
        const auto ma = performanceState.getModAmount();

        for (auto* layer : layers)
            layer->setPerformance (pb, ma);
    };

    applyPerformanceState();

    // --- Render, splitting the block at MIDI event boundaries.
    int position = 0;

    for (const auto metadata : midiMessages)
    {
        const auto eventTime = juce::jlimit (0, numSamples, metadata.samplePosition);

        if (eventTime > position)
        {
            renderSegment (buffer, position, eventTime - position);
            position = eventTime;
        }

        handleMidiMessage (metadata.getMessage());
        applyPerformanceState();
    }

    renderSegment (buffer, position, numSamples - position);

    // A host may send a block larger than prepareToPlay announced (D.5.1):
    // the FX chain's buffers hold the prepared size, so it runs in pieces.
    const auto preparedBlock = layerBuffer.getNumSamples();

    for (int start = 0; start < numSamples; start += preparedBlock)
        fxChain.process (buffer, start, juce::jmin (preparedBlock, numSamples - start));

    // --- Output stage: a fixed make-up gain, then a soft limiter.
    //
    // Measured 2026-09-23 with HorizonPadSoundTool (--true-peak, three voicings
    // across the worship-keys register, every factory preset): the previous
    // stage, tanh(x * 0.75), left the instrument peaking between -12.5 and
    // -23.9 dBTP. That is roughly a tenth of the available headroom, and the
    // tanh never engaged at all - a 0.75 trim applied to a worst-case peak of
    // 0.45 sits in tanh's linear region, so the trim cost 2.5 dB and the
    // clipper bought nothing.
    //
    // kOutputGain re-stages that: +11.4 dB relative to the old trim. Set
    // against the loudness-matched bank (17 of 18 presets within +/-1 LU at
    // -18.1 LUFS), it puts the loudest preset at -3.25 dBTP and the rest within
    // ~3.5 dB below it - the spread that is left is crest factor, not loudness,
    // and chasing it would flatten the bank's dynamics for nothing.
    //
    // The limiter is exactly linear up to kKnee and only bends above it,
    // asymptoting to kCeiling, so no factory preset is limited at all: across
    // 63 renders of the seven loudest presets in three voicings, the worst true
    // peak was -2.64 dBTP against a knee at -1.94 dBFS. The pathological case
    // the limiter exists for (all four volumes, WIDTH, FILTER and REVERB at
    // 100%, eight voices at velocity 127) goes in at +2.0 dBTP and comes out
    // at -0.04 dBFS.
    //
    // Antialiasing: none, deliberately (rule 4). The tanh runs at 1x because
    // nothing musical reaches it - no factory preset touches the knee - and
    // where something does, its aliasing is below what can be measured:
    // rendered at 48 and 192 kHz (B.5 rate comparison, 2026-10-17) on the
    // pathological patch above with a C3 and a C5-C7 eight-voice chord (true
    // peak -0.07 / +0.11 dBTP, the limiter working hard), the energy the
    // 48 kHz render has beyond the 192 kHz one is -14.5 / -21.1 dB - and
    // -14.3 / -22.3 dB with every volume 12 dB lower, where the limiter is
    // linear. The difference is the instrument's own rate-dependent drift,
    // not folding. Oversampling would buy nothing measurable.
    //
    // Note the stage is NOT bit-transparent even below the knee: the DC blocker
    // below runs on every sample. It is -0.09 dB at 13.75 Hz and -0.04 dB at
    // 20 Hz, so it is inaudible, but "inaudible" and "bypassed" are different
    // claims and only the first one is true here.
    constexpr float kOutputGain = 2.8f;
    constexpr float kKnee       = 0.80f;  // -1.9 dBFS - linear below this
    constexpr float kCeiling    = 0.995f; // -0.04 dBFS - asymptote, never exceeded
    constexpr float kRange      = kCeiling - kKnee;

    // For the editor's LIMIT LED: did anything reach the knee in this meter
    // window? Read only, the samples themselves are untouched.
    bool passedKnee = false;

    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
    {
        auto* data = buffer.getWritePointer (ch);

        for (int n = 0; n < numSamples; ++n)
        {
            const auto x = data[n] * kOutputGain;
            const auto a = std::abs (x);
            passedKnee = passedKnee || a > kKnee;

            // Continuous and C1 at the knee, since tanh'(0) = 1.
            const auto limited = a <= kKnee
                                   ? x
                                   : std::copysign (kKnee + kRange * std::tanh ((a - kKnee) / kRange), x);

            // DC blocker, after the limiter because the limiter is what
            // creates the offset. See kDcBlockerHz in the header.
            const auto chIndex = (size_t) juce::jmin (ch, 1);
            const auto blocked = limited - dcBlockerX1[chIndex] + dcBlockerCoeff * dcBlockerY1[chIndex];
            dcBlockerX1[chIndex] = limited;
            dcBlockerY1[chIndex] = blocked;

            // Final brickwall. The DC blocker subtracts a slowly-varying local
            // mean, so on asymmetric material it can push a sample that the
            // limiter had just placed at the ceiling back over it - measured at
            // up to +0.5 dBFS at the pathological extreme before this clamp
            // existed. Restoring the hard ceiling matters more than the last
            // fraction of a dB of transparency, and this only ever engages on
            // material the limiter is already working on.
            data[n] = juce::jlimit (-kCeiling, kCeiling, blocked);
        }
    }

    // --- Meters for the editor: accumulate, publish once per window.
    const auto meterChannels = juce::jmin (2, buffer.getNumChannels());

    for (int ch = 0; ch < meterChannels; ++ch)
    {
        const auto* data = buffer.getReadPointer (ch);

        for (int n = 0; n < numSamples; ++n)
            outputSumSquares += data[n] * data[n];
    }

    meterSampleCount += numSamples;
    limitedInWindow = limitedInWindow || passedKnee;

    if (meterSampleCount >= meterWindowSamples)
    {
        limiting.store (limitedInWindow, std::memory_order_relaxed);
        limitedInWindow = false;

        // Layers are always rendered in stereo; the output may be mono.
        const auto perLayerSample = 1.0f / (2.0f * (float) meterSampleCount);
        const auto perOutputSample = 1.0f / ((float) juce::jmax (1, meterChannels) * (float) meterSampleCount);

        outputLevel.store (std::sqrt (outputSumSquares * perOutputSample), std::memory_order_relaxed);

        for (size_t i = 0; i < layerLevels.size(); ++i)
        {
            layerLevels[i].store (std::sqrt (layerSumSquares[i] * perLayerSample), std::memory_order_relaxed);
            layerSumSquares[i] = 0.0f;
        }

        outputSumSquares = 0.0f;
        meterSampleCount = 0;
    }
}

//==============================================================================
/*  The session state (playbook D.5.3), format PresetFile::kStateVersion = 2:

      <HorizonPadState version="2" dspVersion="1" pluginVersion="0.1.0">
        <PARAMETERS> <PARAM id="rootVolume" value="47.7"/> ... </PARAMETERS>
        <Extra activeBuffer="0" presetKind="factory" preset="init"/>
        <BufferA rootVolume="47.7" ... detuneMacro="0"/>  <BufferB .../>
      </HorizonPadState>

    Every parameter is written, by ID, in 0..100; the A/B snapshots by ID too;
    <Extra> holds what is not a parameter. Version 1 (before 0.1.0) stored
    0..1 values, the buffers as v0..v3/m0..m5, program/activePresetKind/
    activePresetIndex/editorScale as attributes, or was a bare APVTS tree: it
    is migrated on load. A newer version loads as far as this one understands
    and says so (getStateNotice()); anything else is refused and the sound
    stays as it was. The editor size is machine-local, never in the state. */

namespace
{
    const char* const kBufferNames[2] { "BufferA", "BufferB" };

    // The bank's order in version 1, to name the preset a v1 session was on.
    const char* const kV1FactorySlugs[] { "alpengluehen", "init", "talwind", "frostklang", "funkenflug",
                                          "steinerne-ruhe", "bergecho", "kupferglanz", "silberpfad",
                                          "mitternachtsblau", "ruhepuls", "sturmfront", "glasperle",
                                          "sternenzelt", "daemmerlicht", "goldstaub", "klarheit" };

    /** One value from an attribute, in today's 0..100: missing or broken
        takes `fallback`, everything is clamped. */
    float readValue (const juce::XmlElement& element, const juce::String& attribute, float scale, float fallback)
    {
        const auto text = element.getStringAttribute (attribute).trim();

        if (! (text.containsOnly ("0123456789.-+eE") && text.containsAnyOf ("0123456789")))
            return fallback;

        const auto v = (float) text.getDoubleValue() * scale;
        return std::isfinite (v) ? juce::jlimit (0.0f, kParamScale, v) : fallback;
    }
}

void HorizonPadAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    juce::XmlElement root ("HorizonPadState");
    root.setAttribute ("version", PresetFile::kStateVersion);
    root.setAttribute ("dspVersion", PresetFile::kDspVersion);
    root.setAttribute ("pluginVersion", JucePlugin_VersionString);

    auto* parameters = root.createNewChildElement ("PARAMETERS");

    for (const auto& spec : kParamSpecs)
    {
        auto* param = parameters->createNewChildElement ("PARAM");
        param->setAttribute ("id", spec.id);
        param->setAttribute ("value", PresetFile::formatValue (apvts.getRawParameterValue (spec.id)->load()));
    }

    auto* extra = root.createNewChildElement ("Extra");
    extra->setAttribute ("activeBuffer", activeBufferIndex.load (std::memory_order_relaxed));

    switch (getActivePresetKind())
    {
        case PresetKind::factory: extra->setAttribute ("presetKind", "factory"); break;
        case PresetKind::user:    extra->setAttribute ("presetKind", "user"); break;
        case PresetKind::none:    extra->setAttribute ("presetKind", "none"); break;
    }

    extra->setAttribute ("preset", getActivePresetSlug());
    extra->setAttribute ("presetName", getActivePresetName());

    for (size_t b = 0; b < 2; ++b)
    {
        auto* buffer = root.createNewChildElement (kBufferNames[b]);

        for (size_t i = 0; i < kParamSpecs.size(); ++i)
        {
            const auto& slot = i < (size_t) kNumLayers ? buffers[b].vols[i] : buffers[b].macros[i - (size_t) kNumLayers];
            buffer->setAttribute (kParamSpecs[i].id, PresetFile::formatValue (slot.load (std::memory_order_relaxed)));
        }
    }

    copyXmlToBinary (root, destData);
}

void HorizonPadAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    // JUCE's XML-in-binary header carries the text's length: fewer bytes than
    // that is a truncated blob, which the lenient XML parser would otherwise
    // half-read.
    const auto truncated = sizeInBytes >= 8
                        && juce::ByteOrder::littleEndianInt (data) == 0x21324356
                        && (juce::int64) juce::ByteOrder::littleEndianInt (juce::addBytesToPointer (data, 4)) > (juce::int64) sizeInBytes - 8;

    const auto xml = truncated ? nullptr : getXmlFromBinary (data, sizeInBytes);
    const auto bareTree = xml != nullptr && xml->hasTagName (apvts.state.getType().toString());

    if (xml == nullptr || ! (bareTree || xml->hasTagName ("HorizonPadState")))
    {
        const juce::SpinLock::ScopedLockType lock (presetLock);
        stateNotice = "refused: not a Horizon Pad state; the sound was left as it was";
        return;
    }

    const auto version = bareTree ? 1 : xml->getIntAttribute ("version", 1);
    const auto scale = version < 2 ? kParamScale : 1.0f;

    // Parameters: every one, migrated and sanitised, into a fresh tree.
    Preset values;
    PresetFile::readParameters (bareTree ? xml.get() : xml->getChildByName ("PARAMETERS"), version, values);

    juce::ValueTree params (apvts.state.getType());

    for (size_t i = 0; i < kParamSpecs.size(); ++i)
        params.appendChild (juce::ValueTree ("PARAM", { { "id", kParamSpecs[i].id }, { "value", values.valueAt (i) } }), nullptr);

    apvts.replaceState (params);

    // Non-parameter state.
    int bufferIndex = 0;
    auto kind = PresetKind::none;
    juce::String slug, name;

    if (version >= 2)
    {
        if (const auto* extra = xml->getChildByName ("Extra"))
        {
            bufferIndex = extra->getIntAttribute ("activeBuffer", 0);
            const auto k = extra->getStringAttribute ("presetKind");
            kind = k == "factory" ? PresetKind::factory : k == "user" ? PresetKind::user : PresetKind::none;
            slug = extra->getStringAttribute ("preset");
            name = extra->getStringAttribute ("presetName");
        }
    }
    else if (! bareTree)
    {
        bufferIndex = xml->getIntAttribute ("activeBuffer", 0);
        const auto v1Index = xml->getIntAttribute ("activePresetIndex", 0);

        // Version 1 kinds: 0 none, 1 factory, 2 user. User presets were
        // renumbered by the move to files, so only a factory name survives.
        if (xml->getIntAttribute ("activePresetKind", 1) == 1
            && juce::isPositiveAndBelow (v1Index, (int) juce::numElementsInArray (kV1FactorySlugs)))
        {
            kind = PresetKind::factory;
            slug = kV1FactorySlugs[v1Index];
        }
    }

    if (kind == PresetKind::factory)
    {
        const auto index = findFactoryPreset (slug);
        kind = index >= 0 ? kind : PresetKind::none;
        name = index >= 0 ? getFactoryPresets()[(size_t) index].name : juce::String();
    }

    setActivePreset (kind, slug, name);
    activeBufferIndex.store (juce::jlimit (0, 1, bufferIndex), std::memory_order_relaxed);

    // A/B snapshots: by ID since version 2, v0..v3/m0..m5 in 0..1 before.
    for (size_t b = 0; b < 2; ++b)
    {
        const auto* bufferXml = bareTree ? nullptr : xml->getChildByName (kBufferNames[b]);

        for (size_t i = 0; i < kParamSpecs.size(); ++i)
        {
            const auto live = values.valueAt (i);
            const auto attribute = version >= 2 ? juce::String (kParamSpecs[i].id)
                                 : i < (size_t) kNumLayers ? "v" + juce::String ((int) i)
                                                           : "m" + juce::String ((int) i - kNumLayers);
            const auto v = bufferXml != nullptr ? readValue (*bufferXml, attribute, scale, live) : live;
            auto& slot = i < (size_t) kNumLayers ? buffers[b].vols[i] : buffers[b].macros[i - (size_t) kNumLayers];
            slot.store (v, std::memory_order_relaxed);
        }
    }

    {
        const juce::SpinLock::ScopedLockType lock (presetLock);
        stateNotice = version > PresetFile::kStateVersion
                        ? "Saved by a newer Horizon Pad (" + xml->getStringAttribute ("pluginVersion", "format " + juce::String (version))
                              + "); loaded what this version understands."
                        : juce::String();
    }

    sendChangeMessage();
}

//==============================================================================
juce::AudioProcessorEditor* HorizonPadAudioProcessor::createEditor()
{
    return new HorizonPadAudioProcessorEditor (*this);
}

//==============================================================================
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new HorizonPadAudioProcessor();
}
