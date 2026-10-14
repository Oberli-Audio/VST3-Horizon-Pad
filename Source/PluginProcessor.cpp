#include "PluginProcessor.h"
#include "PluginEditor.h"

#include <limits>

using namespace horizon;

namespace
{
    juce::String percentText (float value, int)
    {
        return juce::String (juce::roundToInt (value * 100.0f)) + " %";
    }

    float percentValue (const juce::String& text)
    {
        return juce::jlimit (0.0f, 1.0f, text.getFloatValue() * 0.01f);
    }

    /** FILTER/DETUNE macro mapping: 0.5 = the sound design's own validated
        value (no change), <0.5 halves it, >0.5 doubles it, on a log2 curve so
        the knob feels even in both directions. */
    float macroMultiplier (float macroValue) noexcept
    {
        return std::pow (2.0f, (juce::jlimit (0.0f, 1.0f, macroValue) - 0.5f) * 2.0f);
    }

    /** ATTACK/RELEASE macro mapping (brief A-009, 2026-10-14): one
        exponential curve from 1% of each layer's designed time at 0% to the
        designed time itself at 100%, so every 10% of travel multiplies the
        time by the same 1.58 and the short times worship playing lives in get
        most of the knob - 0-50% leads and plucks (Root attack 22-220 ms),
        50-80% in-between (0.2-0.9 s), 80-100% pads (0.9-2.2 s).

        0% keeps the old floor: each layer's attack/release in the ~10-25 ms
        range, short and percussive but clear of the ~1-5 ms where an
        envelope ramp starts audibly clicking; LayerBase::beginNote()'s
        jmax(0.001f, ...) floor remains the hard backstop underneath.
        Written as kMinScale^(1 - x) so that 100% is exactly 1.0 - the
        designed envelope, bit for bit (the G5 null test runs there).

        Until A-009, 50% was the designed time and 100% double it, with
        everything below 0.6x squeezed under 15% of the knob; the owner's
        listening test found the pads cinematic, and longer times unneeded. */
    float attackReleaseTimeScale (float macroValue) noexcept
    {
        constexpr float kMinScale = 0.01f;
        return std::pow (kMinScale, 1.0f - juce::jlimit (0.0f, 1.0f, macroValue));
    }

    /** The ten (and only ten) host-automatable parameter IDs, in the fixed
        order used everywhere a "vols/macros" array is needed: Root,
        Clearing, Expanse, Bloom, then Attack, Release, Filter, Reverb,
        Width, Detune. The first eight are ordered so that a controller that
        maps its knobs to a plugin's first N host parameters in order - e.g.
        a Launchkey's 8 knobs in Live's generic Device-knob mode - lands
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

    const juce::NormalisableRange<float> unitRange { 0.0f, 1.0f };

    auto addPercent = [&] (const char* id, const char* name, float defaultValue)
    {
        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { id, 1 },
            name,
            unitRange,
            defaultValue,
            juce::AudioParameterFloatAttributes()
                .withStringFromValueFunction (percentText)
                .withValueFromStringFunction (percentValue)
                .withLabel ("%")));
    };

    // The ten (and only ten) host-automatable parameters: four pad volumes,
    // then the six macros. ATTACK/RELEASE/FILTER/REVERB sit right after the
    // volumes (params 5-8) so a hardware controller that maps its knobs to
    // the plugin's first 8 host parameters - e.g. a Launchkey in Live's
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
    addPercent (ParamID::rootVolume,     "Root",     0.477f);
    addPercent (ParamID::clearingVolume, "Clearing", 0.191f);
    addPercent (ParamID::expanseVolume,  "Expanse",  0.096f);
    addPercent (ParamID::bloomVolume,    "Bloom",    0.159f);
    // ATTACK/RELEASE: Lagerfeuer's 73.13% on the curve of A-009 (Root
    // attack 0.64 s, release 0.70 s).
    addPercent (ParamID::attackMacro,    "Attack",   0.7313f);
    addPercent (ParamID::releaseMacro,   "Release",  0.7313f);
    addPercent (ParamID::filterMacro,    "Filter",   0.30f);
    addPercent (ParamID::reverbMacro,    "Reverb",   0.20f);
    addPercent (ParamID::widthMacro,     "Width",    0.40f);
    // 0.0 since 2026-10-07: the owner preferred the default patch at DETUNE 0%
    // over the designed detune (50%) in the blind A/B of 2026-10-06. 50% still
    // reproduces the designed detune exactly; the defaults stay Lagerfeuer's.
    addPercent (ParamID::detuneMacro,    "Detune",   0.00f);

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
    // The bank is ordered by family and envelope length (2026-10-07), so the
    // preset the defaults equal is not necessarily program 0. Point the
    // program index and the preset bar at it, so the name shown at startup is
    // the sound heard. Matched by value, not by name: renaming or reordering
    // the bank cannot break it (test startup_program_matches_defaults).
    {
        const auto& factory = getFactoryPresets();

        for (size_t p = 0; p < factory.size(); ++p)
        {
            bool same = true;

            for (int i = 0; i < kNumLayers; ++i)
                same = same && std::abs (factory[p].volumes[(size_t) i] - volumeParams[(size_t) i]->load()) < 1.0e-4f;

            for (int i = 0; i < kNumGlobalParams; ++i)
                same = same && std::abs (factory[p].macros[(size_t) i] - macroParams[(size_t) i]->load()) < 1.0e-4f;

            if (same)
            {
                currentProgram.store ((int) p);
                activePresetIndex.store ((int) p);
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

    userPresets = horizon::UserPresetStore::load();
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
        layerGain[(size_t) i].setCurrentAndTargetValue (volumeParams[(size_t) i]->load());

    // One-pole DC blocker: y = x - x1 + R*y1, R = 1 - 2*pi*fc/fs.
    dcBlockerCoeff = 1.0f - (juce::MathConstants<float>::twoPi * kDcBlockerHz / (float) sampleRate);
    dcBlockerX1.fill (0.0f);
    dcBlockerY1.fill (0.0f);

    meterWindowSamples = juce::jmax (1, juce::roundToInt (sampleRate * kMeterWindowSeconds));
    meterSampleCount = 0;
    outputSumSquares = 0.0f;
    layerSumSquares.fill (0.0f);
    outputLevel.store (0.0f, std::memory_order_relaxed);

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
int HorizonPadAudioProcessor::getNumPrograms()   { return getNumFactoryPresets(); }
int HorizonPadAudioProcessor::getCurrentProgram() { return currentProgram.load(); }

const juce::String HorizonPadAudioProcessor::getProgramName (int index)
{
    const auto& presets = getFactoryPresets();

    if (! juce::isPositiveAndBelow (index, (int) presets.size()))
        return {};

    return presets[(size_t) index].name;
}

void HorizonPadAudioProcessor::changeProgramName (int, const juce::String&)
{
    // Factory programs are read-only; user variations live in the plugin state,
    // which the host saves separately.
}

void HorizonPadAudioProcessor::setCurrentProgram (int index)
{
    const auto& presets = getFactoryPresets();

    if (! juce::isPositiveAndBelow (index, (int) presets.size()))
        return;

    currentProgram.store (index);

    // The active-preset fields are set before the counter moves, and the
    // counter before any parameter does, so an editor that sees the new
    // count reads the right preset name and catches every knob before it
    // starts to glide (see getPresetRecallCount()).
    activePresetKind.store ((int) PresetKind::factory, std::memory_order_relaxed);
    activePresetIndex.store (index, std::memory_order_relaxed);
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
void HorizonPadAudioProcessor::saveCurrentAsUserPreset (const juce::String& name)
{
    UserPreset preset;
    preset.name = name.isNotEmpty() ? name : "Untitled";

    for (int i = 0; i < kNumLayers; ++i)
        preset.vols[(size_t) i] = volumeParams[(size_t) i]->load (std::memory_order_relaxed);

    for (int i = 0; i < kNumGlobalParams; ++i)
        preset.macros[(size_t) i] = macroParams[(size_t) i]->load (std::memory_order_relaxed);

    userPresets.push_back (preset);
    UserPresetStore::save (userPresets);

    activePresetKind.store ((int) PresetKind::user, std::memory_order_relaxed);
    activePresetIndex.store ((int) userPresets.size() - 1, std::memory_order_relaxed);

    sendChangeMessage();
}

void HorizonPadAudioProcessor::deleteUserPreset (int index)
{
    if (! juce::isPositiveAndBelow (index, (int) userPresets.size()))
        return;

    userPresets.erase (userPresets.begin() + index);
    UserPresetStore::save (userPresets);

    const auto activeKind = getActivePresetKind();
    const auto activeIndex = getActivePresetIndex();

    if (activeKind == PresetKind::user)
    {
        if (activeIndex == index)
            activePresetKind.store ((int) PresetKind::none, std::memory_order_relaxed);
        else if (activeIndex > index)
            activePresetIndex.store (activeIndex - 1, std::memory_order_relaxed);
    }

    sendChangeMessage();
}

void HorizonPadAudioProcessor::applyUserPreset (int index)
{
    if (! juce::isPositiveAndBelow (index, (int) userPresets.size()))
        return;

    const auto& preset = userPresets[(size_t) index];

    // Same instant full-parameter jump as applyPreset() - see
    // freezeBrightnessRequested's doc comment.
    freezeBrightnessRequested.store (true, std::memory_order_release);

    // Same order as setCurrentProgram(): active-preset fields, then the
    // recall counter, then the parameters.
    activePresetKind.store ((int) PresetKind::user, std::memory_order_relaxed);
    activePresetIndex.store (index, std::memory_order_relaxed);
    presetRecallCount.fetch_add (1, std::memory_order_release);

    auto setParam = [&] (const char* id, float value)
    {
        if (auto* p = apvts.getParameter (id))
            p->setValueNotifyingHost (p->convertTo0to1 (value));
    };

    for (int i = 0; i < kNumLayers; ++i)
        setParam (volumeIds()[(size_t) i], preset.vols[(size_t) i]);

    for (int i = 0; i < kNumGlobalParams; ++i)
        setParam (macroIds()[(size_t) i], preset.macros[(size_t) i]);

    // Wheels spring back to rest on a preset change, like a real keyboard.
    performanceState.setPitchBendSemitones (0.0f);
    performanceState.setModAmount (0.0f);

    sendChangeMessage();
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
    const auto numChannels = juce::jmin (2, output.getNumChannels());
    numSamples = juce::jmin (numSamples, layerBuffer.getNumSamples());

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
        layerGain[(size_t) i].setTargetValue (volumeParams[(size_t) i]->load (std::memory_order_relaxed));

    const auto attackScale = attackReleaseTimeScale (macroParams[attackMacroIndex]->load (std::memory_order_relaxed));
    const auto releaseScale = attackReleaseTimeScale (macroParams[releaseMacroIndex]->load (std::memory_order_relaxed));
    const auto brightnessMul = macroMultiplier (macroParams[filterMacroIndex]->load (std::memory_order_relaxed));
    const auto reverbSend = macroParams[reverbMacroIndex]->load (std::memory_order_relaxed);
    const auto width = macroParams[widthMacroIndex]->load (std::memory_order_relaxed);
    const auto detuneScale = macroMultiplier (macroParams[detuneMacroIndex]->load (std::memory_order_relaxed));

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

    fxChain.process (buffer, numSamples);

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
    // Note the stage is NOT bit-transparent even below the knee: the DC blocker
    // below runs on every sample. It is -0.09 dB at 13.75 Hz and -0.04 dB at
    // 20 Hz, so it is inaudible, but "inaudible" and "bypassed" are different
    // claims and only the first one is true here.
    constexpr float kOutputGain = 2.8f;
    constexpr float kKnee       = 0.80f;  // -1.9 dBFS - linear below this
    constexpr float kCeiling    = 0.995f; // -0.04 dBFS - asymptote, never exceeded
    constexpr float kRange      = kCeiling - kKnee;

    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
    {
        auto* data = buffer.getWritePointer (ch);

        for (int n = 0; n < numSamples; ++n)
        {
            const auto x = data[n] * kOutputGain;
            const auto a = std::abs (x);

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

    if (meterSampleCount >= meterWindowSamples)
    {
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
void HorizonPadAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    juce::ValueTree root ("HorizonPadState");
    root.setProperty ("program", currentProgram.load(), nullptr);
    root.setProperty ("activeBuffer", activeBufferIndex.load (std::memory_order_relaxed), nullptr);
    root.setProperty ("activePresetKind", activePresetKind.load (std::memory_order_relaxed), nullptr);
    root.setProperty ("activePresetIndex", activePresetIndex.load (std::memory_order_relaxed), nullptr);
    root.appendChild (apvts.copyState(), nullptr);

    // Both A/B snapshots, independent of whichever one is currently live in
    // the APVTS (which copyState() above already captures).
    const char* bufferNames[2] { "BufferA", "BufferB" };

    for (int b = 0; b < 2; ++b)
    {
        juce::ValueTree bufferTree (bufferNames[b]);

        for (int i = 0; i < kNumLayers; ++i)
            bufferTree.setProperty ("v" + juce::String (i), buffers[(size_t) b].vols[(size_t) i].load (std::memory_order_relaxed), nullptr);

        for (int i = 0; i < kNumGlobalParams; ++i)
            bufferTree.setProperty ("m" + juce::String (i), buffers[(size_t) b].macros[(size_t) i].load (std::memory_order_relaxed), nullptr);

        root.appendChild (bufferTree, nullptr);
    }

    if (auto xml = root.createXml())
        copyXmlToBinary (*xml, destData);
}

void HorizonPadAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    auto xml = getXmlFromBinary (data, sizeInBytes);

    if (xml == nullptr)
        return;

    auto root = juce::ValueTree::fromXml (*xml);

    if (! root.hasType ("HorizonPadState"))
    {
        // Backwards/forwards tolerance: an APVTS-only state still loads.
        if (root.hasType (apvts.state.getType()))
            apvts.replaceState (root);

        return;
    }

    if (auto params = root.getChildWithName (apvts.state.getType()); params.isValid())
        apvts.replaceState (params);

    currentProgram.store (juce::jlimit (0, juce::jmax (0, getNumFactoryPresets() - 1),
                                        (int) root.getProperty ("program", 0)));

    activeBufferIndex.store (juce::jlimit (0, 1, (int) root.getProperty ("activeBuffer", 0)),
                             std::memory_order_relaxed);
    activePresetKind.store ((int) root.getProperty ("activePresetKind", (int) PresetKind::factory),
                            std::memory_order_relaxed);
    activePresetIndex.store ((int) root.getProperty ("activePresetIndex", 0), std::memory_order_relaxed);

    const char* bufferNames[2] { "BufferA", "BufferB" };
    bool haveSavedBuffers = false;

    for (int b = 0; b < 2; ++b)
    {
        auto bufferTree = root.getChildWithName (bufferNames[b]);

        if (! bufferTree.isValid())
            continue;

        haveSavedBuffers = true;

        for (int i = 0; i < kNumLayers; ++i)
            buffers[(size_t) b].vols[(size_t) i].store ((float) bufferTree.getProperty ("v" + juce::String (i), 0.0), std::memory_order_relaxed);

        for (int i = 0; i < kNumGlobalParams; ++i)
            buffers[(size_t) b].macros[(size_t) i].store ((float) bufferTree.getProperty ("m" + juce::String (i), 0.0), std::memory_order_relaxed);
    }

    if (! haveSavedBuffers)
    {
        // Older saved state with no A/B buffers: seed both from the
        // just-restored live parameter values, matching construction.
        for (int i = 0; i < kNumLayers; ++i)
        {
            buffers[0].vols[(size_t) i].store (volumeParams[(size_t) i]->load(), std::memory_order_relaxed);
            buffers[1].vols[(size_t) i].store (volumeParams[(size_t) i]->load(), std::memory_order_relaxed);
        }

        for (int i = 0; i < kNumGlobalParams; ++i)
        {
            buffers[0].macros[(size_t) i].store (macroParams[(size_t) i]->load(), std::memory_order_relaxed);
            buffers[1].macros[(size_t) i].store (macroParams[(size_t) i]->load(), std::memory_order_relaxed);
        }
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
