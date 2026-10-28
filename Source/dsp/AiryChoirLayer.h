#pragma once

#include "LayerBase.h"
#include "OctaveShimmer.h"

namespace horizon
{

/**
    PAD 3 / EXPANSE - Airy Choir: very bright, very slow attack, high/air
    register with a long tail.

    Direct C++ port of "airyChoir" in four_pads.dsp: a three-oscillator detuned
    triangle stack an octave up, through a resonant bandpass that sweeps slowly
    into the top end, plus an octave-up "shimmer" send (highpass -> pitch shift
    -> reverb) so this pad occupies clear air above the other three. All
    oscillator-based, no noise source, per the sound design's own history (an
    earlier noise-based texture layer was replaced for reading as unwanted hiss).

    The shimmer's pitch shift and reverb are shared per LAYER rather than
    computed per voice (each voice still gets its own bandpass sweep and its
    own envelope): the Faust source's envelope is applied to the *combined*
    swept+shimmer signal, not before, so per-voice contributions can be summed
    into one shimmer bus and processed once - both truer to the original
    signal-flow order and far cheaper than a pitch-shifter and reverb per voice.

    Sound-design v2 (2026-09-26):
      * The sweep and the safety lowpass track the note, anchored at C4 where
        the sweep was voiced. Fixed in Hz, the layer measured -43 LUFS at C2,
        -29 at C4 and -55 at C7 - a 27 dB span, most of it the bandpass
        missing the oscillators entirely away from C4.
      * The safety lowpass is one pole, as in the prototype
        (`fi.lowpass(1, 1800)`). The port had a two-pole SVF there: -17 dB at
        5-10 kHz against the prototype, the largest real divergence G5 found.
      * Triangles are polyBLAMP-corrected (EX-003: this layer runs an octave
        up and aliased at MIDI 96-108).
      * Oscillators are spread by WIDTH, and the shimmer reverb runs in
        stereo with its width following the layer's WIDTH, so the shimmer is
        the widest thing in the instrument instead of a mono tail.
      * Register pinning. This layer is "clear air above the other three".
        Played at C3 its octave-up stack sat at 262 Hz, at C2 at 131 Hz - in
        the other layers' band, and measured +6.8 dB in 200-500 Hz at the
        reference chord once key tracking stopped filtering it away. Below C4
        the stack now stays near the octave it was voiced in (C5), following
        the note's pitch class and crossfading between the two nearest
        octaves - the way an organ's mixture ranks "break back" to stay in
        the ear's bright register however low the note. Same pitch class,
        same harmony; at C4 and above nothing changes.
*/
class AiryChoirLayer final : public LayerBase
{
public:
    AiryChoirLayer() = default;

protected:
    void prepareLayer (const juce::dsp::ProcessSpec& spec) override;
    void resetLayer() override;
    void startVoice (int voiceIndex, float frequency, float velocity) override;
    void beginBlock (int numSamples) override;
    void renderVoice (int voiceIndex, juce::AudioBuffer<float>& target, int numSamples) override;
    void renderLayerTail (juce::AudioBuffer<float>& target, int numSamples) override;

    float attackSeconds() const noexcept override  { return 2.6f; }
    float decaySeconds() const noexcept override   { return 1.0f; }
    float sustainLevel() const noexcept override   { return 0.4f; }
    float releaseSeconds() const noexcept override { return 1.8f; }

    /** The air: the first pad to open and the widest, its width following
        the WIDTH knob one to one. */
    WidthProfile widthProfile() const noexcept override { return { 0.0f, 1.0f }; }

private:
    static constexpr int kNumOscs = 3;
    static constexpr float kOscDetuneFraction[3] { 0.0002f, -0.0003f, 0.0005f }; // det * 0.01 from the Faust source
    static constexpr float kOscDriftRateHz[3] { 0.29f, 0.31f, 0.24f };
    static constexpr float kOscSpread[3] { 0.0f, -0.85f, 0.85f };

    /** Sweep and safety-lowpass tracking, anchored at C4. Above C4, 0.7: the
        layer should still thin out towards the top, just not vanish. Below
        C4, only 0.3: this is the layer that sits *above* the others, and a
        sweep that follows the note down lands in 200-500 Hz, the other
        layers' band - measured +6.8 dB there at the C3-E4 chord with 0.7.
        The register break-back takes care of the low notes instead. */
    static constexpr float kSweepTrackingBelowC4 = 0.3f;
    static constexpr float kSweepTrackingAboveC4 = 0.7f;

    /** Shimmer send level. The prototype's octave partial sits 15 dB above
        what the port produced at the same send (corrected G5, 640-1280 Hz
        at C4) - the two reverbs' internal gains differ. Matched by
        measurement to the prototype's octave-to-fundamental ratio. */
    static constexpr float kShimmerBlend = 0.16f;
    static constexpr float kShimmerRolloffFromHz = 523.25f;      // C5
    static constexpr float kShimmerRolloffDbPerOctave = 9.0f;

    /** Below this note the stack stays pinned near the octave it was voiced
        in (see the class comment). */
    static constexpr float kPinHz = 261.6256f; // C4

    struct VoiceState
    {
        std::array<float, kNumOscs> phase {};
        std::array<float, kNumOscs> upperPhase {}; // the second pinning octave, below C4 only
        std::array<Drift, kNumOscs> drift {};
        float sweepPhase = 0.0f;

        juce::dsp::StateVariableTPTFilter<float> bandpass;      // stereo
        // stereo, two poles as in v1: the owner preferred v1's Expanse to the prototype's
        // one-pole top end (owner's blind A/B of 2026-10-06 (git 8a14117:docs/gate-status.md))
        juce::dsp::StateVariableTPTFilter<float> safetyLowpass;
    };

    std::array<VoiceState, (size_t) kMaxVoices> voiceState;

    // Layer-wide shimmer bus: sum of every voice's (highpassed, enveloped) raw
    // swept signal, pitch-shifted and reverbed once per block.
    juce::AudioBuffer<float> shimmerBus;     // channel 0: mono send; both channels: stereo reverb
    juce::dsp::StateVariableTPTFilter<float> shimmerHighpass;

    /** The +1 octave shifter reads every second sample, so the send is
        band-limited below fs/4 first or its top octave folds back down. With
        key tracking the top notes now reach that region. */
    juce::dsp::StateVariableTPTFilter<float> shimmerAntiAlias;
    OctaveShimmer shimmerPitch;
    juce::dsp::Reverb shimmerReverb;
    float shimmerReverbWidth = 0.0f;
};

} // namespace horizon
