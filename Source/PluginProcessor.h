#pragma once

#include <JuceHeader.h>

#include "dsp/HorizonTypes.h"
#include "dsp/PerformanceState.h"
#include "dsp/LayerBase.h"
#include "dsp/WarmFoundationLayer.h"
#include "dsp/AnalogEnsembleLayer.h"
#include "dsp/AiryChoirLayer.h"
#include "dsp/MotionPadLayer.h"
#include "dsp/FxChain.h"
#include "presets/Presets.h"
#include "presets/UserPresetStore.h"

/**
    Horizon Pad - a four-layer ambient pad synthesiser.

    Thread-safety model
    -------------------
      * The ten host-automatable parameters (four pad volumes, six macros)
        live in an AudioProcessorValueTreeState; the audio thread reads them
        through cached std::atomic<float>* pointers and applies them per block.

      * PITCH and MOD are performance controls, not host parameters (matching
        a real keyboard's wheels): horizon::PerformanceState holds them as two
        independent atomics, written by either incoming MIDI or the on-screen
        wheel being dragged, and read once per block by the audio thread. No
        lock anywhere on the audio path.

      * Programs are applied on whatever thread the host calls setCurrentProgram
        on; that only touches parameters (thread-safe), then posts a
        ChangeBroadcaster message for the editor.

      * The A/B buffers mirror the mockup's design: an AudioProcessorValueTreeState::
        Listener callback (parameterChanged(), which can fire from ANY thread -
        the audio thread during host automation, or the message thread during a
        GUI edit) keeps the *active* buffer's snapshot in sync with live
        parameter values, so switching or copying buffers is just atomics.

      * User presets (the "+Save preset" library) are message-thread-only: they
        are created, deleted and read exclusively from GUI actions and editor
        polling, so the plain std::vector needs no extra synchronisation - the
        audio thread never touches it.
*/
class HorizonPadAudioProcessor final : public juce::AudioProcessor,
                                       public juce::ChangeBroadcaster,
                                       private juce::AudioProcessorValueTreeState::Listener
{
public:
    /** Which preset (if any) the current parameter values were last recalled
        from - purely for the preset row's highlight, matching the mockup's
        `activePreset` field. */
    enum class PresetKind { none, factory, user };

    HorizonPadAudioProcessor();
    ~HorizonPadAudioProcessor() override;

    // --- AudioProcessor -----------------------------------------------------
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }

    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 8.0; }

    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int index, const juce::String& newName) override;

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    // --- Horizon Pad --------------------------------------------------------
    juce::AudioProcessorValueTreeState& getAPVTS() noexcept { return apvts; }

    /**
        Pins every layer's oscillator start-phase RNG so a render is
        reproducible. Offline measurement only - the plugin never calls it.

        Start phases are randomised per voice on purpose (see LayerBase's rng
        member), which is why two renders of the same build differ. A null
        test, a regression baseline and "two runs from reset are bit-identical"
        all need that switched off, so the harness pins it and the product does
        not. Each layer gets seed + its own index, or all four would draw the
        same phase sequence and start locked together.
    */
    void setDeterministicSeed (juce::int64 seed) noexcept
    {
        for (size_t i = 0; i < layers.size(); ++i)
            layers[i]->setRandomSeed (seed + (juce::int64) i);
    }

    /** For the on-screen PITCH/MOD wheels (mouse-dragged, not host-automated). */
    horizon::PerformanceState& getPerformanceState() noexcept { return performanceState; }

    /** Factory programs (read-only, host-visible via getProgramName/setCurrentProgram). */
    const std::vector<horizon::Preset>& getPresets() const { return horizon::getFactoryPresets(); }

    /** User-saved presets (the "+Save preset" library) - global, shared across
        every instance via a small file on disk, not part of the host's own
        program list. */
    const std::vector<horizon::UserPreset>& getUserPresets() const noexcept { return userPresets; }
    void saveCurrentAsUserPreset (const juce::String& name);
    void deleteUserPreset (int index);
    void applyUserPreset (int index);

    PresetKind getActivePresetKind() const noexcept { return (PresetKind) activePresetKind.load (std::memory_order_relaxed); }
    int getActivePresetIndex() const noexcept { return activePresetIndex.load (std::memory_order_relaxed); }

    /** True when any of the ten parameters differs from the active preset's
        stored value - the sound has been edited since the recall (or the
        other A/B buffer holds something else). Compared, not tracked, so
        automation, the GUI, an A/B switch and a loaded session all agree.
        Message thread. */
    bool isActivePresetModified() const;

    /** The editor's size as a factor of its designed 1080 x 748 (0.75-1.5),
        per instance and saved with the session; a new instance starts from
        the size last chosen in any instance (a small settings file beside
        UserPresets.xml). Message thread. */
    float getEditorScale() const noexcept { return editorScale; }
    void setEditorScale (float newScale);

    /** True only the first time an editor opens on this instance: the
        sunrise plays once, not every time the window is reopened. */
    bool takeFirstEditorOpen() noexcept { return std::exchange (firstEditorOpen, false); }

    /** A/B buffers: two independent snapshots of all 10 parameters. */
    int getActiveBufferIndex() const noexcept { return activeBufferIndex.load (std::memory_order_relaxed); }
    void switchBuffer (int index);
    void copyActiveBufferToOtherBuffer();

    /** Output RMS (both channels) over the last meter window (kMeterWindowSeconds),
        for the editor's live meter. 0..1. */
    float getOutputLevel() const noexcept { return outputLevel.load (std::memory_order_relaxed); }

    /** One layer's RMS after its VOL gain and before the shared reverb, over the
        same window - what that pad contributes right now. For the pad cards'
        level glow. 0..1. */
    float getLayerLevel (int layerIndex) const noexcept
    {
        return juce::isPositiveAndBelow (layerIndex, horizon::kNumLayers)
                 ? layerLevels[(size_t) layerIndex].load (std::memory_order_relaxed)
                 : 0.0f;
    }

    /** Bumped on every preset recall (factory or user, even of the preset that
        is already active) and every A/B switch, so the editor can tell a recall
        from automation. Incremented after the active-preset fields are set and
        before any parameter moves (release); read with acquire. */
    int getPresetRecallCount() const noexcept { return presetRecallCount.load (std::memory_order_acquire); }
    int getBufferSwitchCount() const noexcept { return bufferSwitchCount.load (std::memory_order_acquire); }

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    void applyPreset (const horizon::Preset& preset);
    void renderSegment (juce::AudioBuffer<float>& output, int startSample, int numSamples);
    void handleMidiMessage (const juce::MidiMessage& message);
    void noteOn (int midiNote, float velocity);
    void noteOff (int midiNote);
    void allNotesOff (bool immediately);
    int findFreeVoiceSlot();
    void parameterChanged (const juce::String& parameterID, float newValue) override;

    juce::AudioProcessorValueTreeState apvts;

    // Cached raw parameter pointers: no string lookups on the audio thread.
    std::array<std::atomic<float>*, (size_t) horizon::kNumLayers> volumeParams {};
    std::array<std::atomic<float>*, (size_t) horizon::kNumGlobalParams> macroParams {};

    horizon::PerformanceState performanceState;

    horizon::WarmFoundationLayer warmFoundation;
    horizon::AnalogEnsembleLayer analogEnsemble;
    horizon::AiryChoirLayer airyChoir;
    horizon::MotionPadLayer motionPad;
    std::array<horizon::LayerBase*, (size_t) horizon::kNumLayers> layers {};

    horizon::FxChain fxChain;

    std::array<juce::SmoothedValue<float>, (size_t) horizon::kNumLayers> layerGain;

    juce::AudioBuffer<float> layerBuffer;

    /*  Output DC blocker state, one pole per channel.

        The output limiter is odd-symmetric, but the summed four-layer signal
        is not, so when the limiter engages it shaves asymmetric peaks
        asymmetrically and leaves a DC residue. Measured 2026-09-23 at the
        pathological extreme (all twelve parameters at maximum, eight voices at
        velocity 127) that residue reached -58.6 dBFS at 96 kHz, past both
        invariant #6 and the -80 dBFS target in the playbook's metric table.
        In normal use - any factory preset, or the default patch - it is
        -140 dBFS or lower, i.e. nothing, because the limiter never engages.

        The corner is deliberately very low (kDcBlockerHz): this instrument's
        sub-oscillator runs an octave below the played note, so MIDI 21 puts
        real musical content at 13.75 Hz. At 2 Hz the blocker is -0.09 dB
        there and -0.04 dB at 20 Hz.
    */
    static constexpr float kDcBlockerHz = 2.0f;
    float dcBlockerCoeff = 0.0f;                  // set in prepareToPlay
    std::array<float, 2> dcBlockerX1 { 0.0f, 0.0f };
    std::array<float, 2> dcBlockerY1 { 0.0f, 0.0f };

    struct VoiceSlot
    {
        int midiNote = -1;          ///< -1 when neither held nor sustained
        bool keyDown = false;       ///< the key itself is held
        juce::uint32 order = 0;     ///< monotonic allocation counter, for voice stealing
    };

    std::array<VoiceSlot, (size_t) horizon::kMaxVoices> voiceSlots;
    juce::uint32 voiceOrderCounter = 0;

    /** Sustain pedal (CC64 >= 64). While it is down, a key release does not
        release the voice: the slot keeps its note, with keyDown false, until
        the pedal comes up. Audio thread only. */
    bool sustainPedalDown = false;
    void setSustainPedal (bool down);
    void releaseSlot (int slot);

    std::atomic<int> currentProgram { 0 };

    /*  Level meters for the editor: the audio thread accumulates, the editor
        polls the atomics from its frame callback.

        Accumulated over a fixed window rather than published per block, so
        the reading does not depend on the host's block size: a 64-sample
        block is 1.3 ms, shorter than one cycle of a low note, and its RMS
        swings with the waveform's phase. 20 ms still resolves Bloom's 3.2 Hz
        tremolo comfortably. The sums are audio-thread only. */
    static constexpr double kMeterWindowSeconds = 0.02;
    int meterWindowSamples = 960;                 // set in prepareToPlay
    int meterSampleCount = 0;
    float outputSumSquares = 0.0f;
    std::array<float, (size_t) horizon::kNumLayers> layerSumSquares {};

    std::atomic<float> outputLevel { 0.0f };
    std::array<std::atomic<float>, (size_t) horizon::kNumLayers> layerLevels {};

    std::atomic<int> presetRecallCount { 0 };
    std::atomic<int> bufferSwitchCount { 0 };

    // Set (message thread, by applyPreset()/applyUserPreset()/switchBuffer())
    // whenever an instant full-parameter jump happens; consumed at the top of
    // the next processBlock(), before that block's new FILTER value lands, to
    // freeze every currently sounding voice's brightness at its old value -
    // see LayerBase::freezeBrightnessForActiveVoices().
    std::atomic<bool> freezeBrightnessRequested { false };

    // --- A/B buffers: see the class-level thread-safety note above.
    struct BufferSnapshot
    {
        std::array<std::atomic<float>, (size_t) horizon::kNumLayers> vols;
        std::array<std::atomic<float>, (size_t) horizon::kNumGlobalParams> macros;
    };

    std::array<BufferSnapshot, 2> buffers; // 0 = A, 1 = B
    std::atomic<int> activeBufferIndex { 0 };

    // --- Preset row highlight state (factory or user, or none once buffers diverge).
    std::atomic<int> activePresetKind { (int) PresetKind::factory };
    std::atomic<int> activePresetIndex { 0 };

    // --- User preset library (message-thread only - see class-level note).
    std::vector<horizon::UserPreset> userPresets;

    // --- Editor settings (message thread).
    float editorScale = 1.0f;
    bool firstEditorOpen = true;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (HorizonPadAudioProcessor)
};
