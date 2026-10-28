#pragma once

#include "LayerBase.h"

namespace horizon
{

/**
    PAD 2 / CLEARING - Analog Ensemble: bright, medium attack, chorused mid
    register.

    Port of "analogEnsemble" in four_pads.dsp: a three-oscillator detuned
    sawtooth stack, a lowpass that starts closed and opens with the envelope
    (like every other pad, so this layer doesn't sound "ahead" of the others at
    note-on regardless of the ATTACK macro setting), then an ensemble.

    The ensemble, since sound-design v2 (2026-09-26). The prototype used one
    delay line per voice swept 2-11 ms at 0.11 Hz - a slow mono flanger. Its
    own design notes cite Synth Secrets on string machines, and the string
    machine's ensemble is something else: several short delay lines on the
    summed output, each modulated by a slow and a fast LFO at 120 degree
    phase offsets, left and right taken at different phases. Three taps
    whose modulations sum to zero keep the average delay steady while each
    tap's pitch wavers - thickness without a wobble - and the offset between
    the channels makes it genuinely stereo. That is what this layer now uses,
    on the layer bus (after the per-voice lowpass, as on a Solina), with a
    slow common drift at the prototype's 0.11 Hz so the comb notches still
    move the way the signed-off sound's did. A *fixed* delay is still the
    thing to avoid: it is a static comb filter and reads reedy/brassy, a
    mistake caught during the Faust sound design.

    Delay reads use 4-point Hermite interpolation (rule 10) and every length
    is a time converted at prepare() (rule 21).
*/
class AnalogEnsembleLayer final : public LayerBase
{
public:
    AnalogEnsembleLayer() = default;

protected:
    void prepareLayer (const juce::dsp::ProcessSpec& spec) override;
    void resetLayer() override;
    void reseedLayerState() override;
    void startVoice (int voiceIndex, float frequency, float velocity) override;
    void renderVoice (int voiceIndex, juce::AudioBuffer<float>& target, int numSamples) override;
    void renderLayerTail (juce::AudioBuffer<float>& target, int numSamples) override;

    float attackSeconds() const noexcept override  { return 2.6f; }
    float decaySeconds() const noexcept override   { return 0.7f; }
    float sustainLevel() const noexcept override   { return 0.8f; }
    float releaseSeconds() const noexcept override { return 2.2f; }

    /** The ensemble is wide by nature (its taps are stereo even at WIDTH 0):
        it opens early, from WIDTH 10%, to its full designed spread. */
    WidthProfile widthProfile() const noexcept override { return { 0.10f, 1.0f }; }

private:
    static constexpr int kNumOscs = 3;
    static constexpr float kOscDetuneFraction[3] { 0.0003f, -0.0004f, 0.0009f }; // det * 0.01 from the Faust source
    static constexpr float kOscDriftRateHz[3] { 0.19f, 0.23f, 0.27f };
    static constexpr float kOscSpread[3] { -0.8f, 0.1f, 0.8f };
    /** Layer gain. 0.16 in v1; raised so the layer measures v1's loudness at
        the C3-E4 reference chord and at C4 (six seeds, WIDTH 0, reverb off)
        now that the ensemble is power-neutral. */
    static constexpr float kLayerLevel = 0.206f;

    // 1.0 until 2026-10-07: low chords too dark (owner's blind A/B of 2026-10-06,
    // git 8a14117:docs/gate-status.md). Root and Bloom went to 0.5; Clearing only to 0.75,
    // because at 0.5 its close C2 triad measured roughness 1.59 against the 1.2
    // limit (v1: 9.0) - the register work the owner preferred on solo low notes.
    static constexpr float kCutoffTrackingBelowC4 = 0.75f;
    static constexpr float kCutoffTrackingAboveC4 = 0.25f;

    // Ensemble, all in seconds / Hz.
    static constexpr int kEnsembleTaps = 3;
    static constexpr float kEnsembleCentreSeconds = 0.0065f;
    static constexpr float kEnsembleSlowDepthSeconds = 0.0012f;  // at 0.6 Hz: ~8 cents of waver per tap
    static constexpr float kEnsembleFastDepthSeconds = 0.00012f; // at 5.5 Hz: ~7 cents
    static constexpr float kEnsembleDriftDepthSeconds = 0.0015f; // at 0.11 Hz, common to all taps
    static constexpr float kEnsembleSlowHz = 0.6f;
    static constexpr float kEnsembleFastHz = 5.5f;
    static constexpr float kEnsembleDriftHz = 0.11f;
    // Wet only, as on a string machine: a dry path summed with the taps is a
    // comb that the taps' motion turns into amplitude wobble. The wet gain
    // makes the ensemble power-neutral by itself: above ~200 Hz the three
    // taps are decorrelated (their delays differ by up to 2 ms), so their
    // sum carries sqrt(3) x one tap's level, and 3 / sqrt(3) restores unity.
    // A larger make-up here (2.29 was tried first) boosts whichever band the
    // taps stay coherent in more than the rest, which tilted the layer by
    // register; level matching belongs in the layer gain (kLayerLevel).
    static constexpr float kEnsembleDry = 0.0f;
    static constexpr float kEnsembleWet = 1.7320508f;

    struct VoiceState
    {
        std::array<float, kNumOscs> phase {};
        std::array<Drift, kNumOscs> drift {};
        juce::dsp::StateVariableTPTFilter<float> filter; // stereo
    };

    std::array<VoiceState, (size_t) kMaxVoices> voiceState;

    /** The bass is not chorused. Modulated delays on a 65 Hz fundamental turn
        it into slow phasing - pitch wobble where the ear wants the most stable
        pitch - so the ensemble only takes the band above kEnsembleBassHz; the
        one-pole split is power-complementary (see FxChain's bassSplit), so the
        level holds across it. */
    static constexpr float kEnsembleBassHz = 200.0f;
    juce::dsp::FirstOrderTPTFilter<float> ensembleBassSplit;

    std::array<std::vector<float>, 2> ensembleBuffer;
    int ensembleWritePos = 0;
    float slowPhase = 0.0f, fastPhase = 0.0f, driftPhase = 0.0f;
};

} // namespace horizon
