#pragma once

#include "LayerBase.h"
#include "EnvelopeMacro.h"

namespace horizon
{

/**
    PAD 1 / ROOT - Warm Foundation: dull, slow attack, low-mid register.

    Direct C++ port of the "warmFoundation" instrument in four_pads.dsp: a
    three-oscillator detuned triangle stack plus a touch of sawtooth edge, a
    sub-oscillator an octave down, all through a 2-pole lowpass whose cutoff
    opens from dark to bright following the amplitude envelope, further
    shaped by a slow "breathe" LFO so the timbre never sits frozen even once
    the envelope has settled. Every oscillator drifts slightly in pitch (a
    plain sine LFO, not filtered noise - see four_pads.dsp for why) to read
    as analog rather than digital-static.
*/
class WarmFoundationLayer final : public LayerBase
{
public:
    WarmFoundationLayer() = default;

protected:
    void prepareLayer (const juce::dsp::ProcessSpec& spec) override;
    void resetLayer() override;
    void startVoice (int voiceIndex, float frequency, float velocity) override;
    void renderVoice (int voiceIndex, juce::AudioBuffer<float>& target, int numSamples) override;

    float attackSeconds() const noexcept override  { return kRootAttackSeconds; }   // shown by ATTACK (EnvelopeMacro.h)
    float decaySeconds() const noexcept override   { return 0.8f; }
    float sustainLevel() const noexcept override   { return 0.75f; }
    float releaseSeconds() const noexcept override { return kRootReleaseSeconds; }  // shown by RELEASE

    /** The foundation opens last: it stays centred until WIDTH 20% and
        reaches its full spread at WIDTH 100%. Below 140 Hz the output is mono
        anyway (FxChain), so what width it has is upper partials. Until
        2026-10-07 it opened from 30% and stopped at 60% of its spread; in the
        owner's blind A/B of 2026-10-06 Root solo and Sternenzelt at WIDTH 100%
        lost to v1's full width, so the knob's top end is full width again. */
    WidthProfile widthProfile() const noexcept override { return { 0.20f, 1.00f }; }

private:
    // Oscillator layout: 0..2 = detuned triangle stack, 3 = saw edge, 4 = sub.
    static constexpr int kNumOscs = 5;
    static constexpr float kOscDetuneCents[3] { 0.0f, 7.0f, -6.0f };
    static constexpr float kOscDriftRateHz[5] { 0.11f, 0.14f, 0.09f, 0.15f, 0.08f };

    /** Stereo position of each triangle at WIDTH 1 (see LayerBase::makePanRamp). */
    static constexpr float kOscSpread[3] { 0.0f, 0.8f, -0.8f };

    /** Cutoff key tracking, anchored at C4 (see LayerBase::keyTrack). Above
        C4, half tracking: the cutoff rises an octave for every two the note
        does, so high notes are no longer filtered away (-29 LUFS at C7
        against -18 at C4) while Root stays the darkest pad. Below C4, full
        tracking: a low note keeps C4's harmonic count instead of buzzing. */
    static constexpr float kCutoffTrackingBelowC4 = 0.5f;  // 0.8 until 2026-10-07: low chords too dark (owner's blind A/B of 2026-10-06 (docs/gate-status.md))
    static constexpr float kCutoffTrackingAboveC4 = 0.5f;

    /** Bass onset (see renderVoice): how far below C3 the cutoff's envelope term
        is lifted towards the sustain level - full at C2 and below, none at C3. */
    static constexpr float kBassOnsetLift = 0.7f;
    /** ...and how much brighter the bass sits overall: the owner liked the tone
        once the breathe LFO opened it, so below C3 the cutoff is lifted towards
        that state, as far as the C2 roughness limit allows (test
        low_register_stays_musical). */
    static constexpr float kBassBrightLift = 0.40f;
    static constexpr float kBassOnsetFullHz = 65.41f;   // C2
    static constexpr float kBassOnsetNoneHz = 130.81f;  // C3

    struct VoiceState
    {
        std::array<float, kNumOscs> phase {};
        std::array<Drift, kNumOscs> drift {};
        float breathePhase = 0.0f;
        juce::dsp::StateVariableTPTFilter<float> filter; // stereo: one state per channel
    };

    std::array<VoiceState, (size_t) kMaxVoices> voiceState;
};

} // namespace horizon
