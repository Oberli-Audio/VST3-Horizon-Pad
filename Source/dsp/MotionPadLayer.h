#pragma once

#include "LayerBase.h"

namespace horizon
{

/**
    PAD 4 / BLOOM - Motion Pad: medium brightness, medium attack, built-in
    movement.

    Port of "motionPad" in four_pads.dsp: a two-oscillator detuned sawtooth
    stack through a slow-sweeping lowpass, with audible tremolo on top. The
    odd one out on purpose - where the other three pads sustain and swell,
    this one keeps moving throughout the note, so it reads as
    "motion/interest" rather than another sustain layer.

    Since sound-design v2 (2026-09-26) the tremolo and the filter LFO are
    shared by every voice of the layer. Per-voice LFOs with random phases
    averaged the pulse away in chords: 3.2 Hz tremolo depth measured 44% on
    one note and 16% on a four-note chord (review S-5), and a pad is played
    in chords. One shared pulse keeps the designed depth at any voicing; the
    shared phase is still random per instance, so two instances do not lock.
*/
class MotionPadLayer final : public LayerBase
{
public:
    MotionPadLayer() = default;

protected:
    void prepareLayer (const juce::dsp::ProcessSpec& spec) override;
    void resetLayer() override;
    void reseedLayerState() override;
    void startVoice (int voiceIndex, float frequency, float velocity) override;
    void beginBlock (int numSamples) override;
    void renderVoice (int voiceIndex, juce::AudioBuffer<float>& target, int numSamples) override;

    float attackSeconds() const noexcept override  { return 1.2f; }
    float decaySeconds() const noexcept override   { return 0.5f; }
    float sustainLevel() const noexcept override   { return 0.7f; }
    float releaseSeconds() const noexcept override { return 1.6f; }

    /** The moving pad sits between the air and the foundation: it opens from
        WIDTH 15% and reaches its full spread at 100% (20% and 90% until
        2026-10-07: the owner preferred v1's wider Bloom). The shared tremolo
        is shallower since the same date, so full width no longer smears it. */
    WidthProfile widthProfile() const noexcept override { return { 0.15f, 1.00f }; }

private:
    static constexpr int kNumOscs = 2;
    static constexpr float kOscDetuneFraction[2] { 0.0004f, -0.0005f }; // det * 0.01 from the Faust source
    static constexpr float kOscDriftRateHz[2] { 0.33f, 0.37f };
    static constexpr float kOscSpread[2] { -0.8f, 0.8f };
    static constexpr float kCutoffTrackingBelowC4 = 0.5f;  // 1.0 until 2026-10-07: low chords too dark (owner's blind A/B of 2026-10-06 (git 8a14117:docs/gate-status.md))
    static constexpr float kCutoffTrackingAboveC4 = 0.5f;

    struct VoiceState
    {
        std::array<float, kNumOscs> phase {};
        std::array<Drift, kNumOscs> drift {};
        juce::dsp::StateVariableTPTFilter<float> filter; // stereo
    };

    std::array<VoiceState, (size_t) kMaxVoices> voiceState;

    // Layer-wide LFOs, computed once per block (beginBlock) and read by every voice.
    float tremPhase = 0.0f;
    float filterLfoPhase = 0.0f;
    juce::AudioBuffer<float> lfoBlock; // channel 0: tremolo gain, channel 1: cutoff (Hz)
};

} // namespace horizon
