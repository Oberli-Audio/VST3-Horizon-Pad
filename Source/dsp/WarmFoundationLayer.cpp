#include "WarmFoundationLayer.h"

namespace horizon
{

void WarmFoundationLayer::prepareLayer (const juce::dsp::ProcessSpec&)
{
    const juce::dsp::ProcessSpec stereoSpec { sampleRate, (juce::uint32) maxBlockSize, 2 };

    for (auto& vs : voiceState)
    {
        vs.filter.prepare (stereoSpec);
        vs.filter.setType (juce::dsp::StateVariableTPTFilterType::lowpass);
        vs.filter.setResonance (0.55f);
    }

    resetLayer();
}

void WarmFoundationLayer::resetLayer()
{
    for (auto& vs : voiceState)
    {
        vs.phase.fill (0.0f);
        vs.drift = {};
        vs.breathePhase = 0.0f;
        vs.filter.reset();
    }
}

void WarmFoundationLayer::startVoice (int voiceIndex, float, float)
{
    auto& vs = voiceState[(size_t) voiceIndex];

    for (int i = 0; i < kNumOscs; ++i)
    {
        vs.phase[(size_t) i] = rng.nextFloat();
        vs.drift[(size_t) i].start (rng, kOscDriftRateHz[i], sampleRate);
    }

    vs.breathePhase = rng.nextFloat();
    vs.filter.reset();
}

void WarmFoundationLayer::renderVoice (int voiceIndex, juce::AudioBuffer<float>& target, int numSamples)
{
    auto& v = voices[(size_t) voiceIndex];
    auto& vs = voiceState[(size_t) voiceIndex];

    auto* left  = target.getWritePointer (0);
    auto* right = target.getWritePointer (1);

    const auto invSr = 1.0f / (float) sampleRate;
    const auto baseFreq = bentFrequency (v.frequency);
    const auto unison = unisonFor (baseFreq, 3);
    // Pitch-ratio drift depth, matching drift() in the Faust source. The saw
    // edge and the sub are single oscillators, not unison partners, so
    // DETUNE leaves their drift alone; the triangle stack's scales with it.
    const auto edgeDriftDepth = driftDepth (1.0f) * unison.driftScale;
    auto detune = makeDetuneRamp (baseFreq);
    const auto level = 0.20f * v.velocity;
    const auto tracking = keyTrack (baseFreq, kCutoffTrackingBelowC4, kCutoffTrackingAboveC4);

    // The cutoff opens with the envelope - the swell that is Root's character in
    // the middle register. In the bass, tracked down, it started as a dull hum
    // for the first seconds (centroid ~80-100 Hz at C1/C2, the harmonics arriving
    // 6-12 dB later); the owner disliked that start and liked the opened tone
    // (owner's second blind A/B, 2026-10-07 (git 8a14117:docs/gate-status.md)). Below C3 the cutoff now starts near
    // where the envelope settles: the envelope term moves towards the sustain
    // level by up to kBassOnsetLift, fading out by C3. Sustain is unchanged.
    const auto bassness = 1.0f - registerBlend (baseFreq, kBassOnsetFullHz, kBassOnsetNoneHz);
    const auto onsetLift = kBassOnsetLift * bassness;
    const auto sustainForCutoff = sustainLevel();

    // The sub sits an octave below the note. Below ~45 Hz it is rumble a PA
    // high-passes anyway and mud in a room: at C2 it is 32.7 Hz, where it
    // made Root's centroid 55 Hz (review S-2/S-4). Fade it from full at
    // 50 Hz to nothing at 30 Hz.
    const auto subHz = baseFreq * 0.5f;
    const auto subGain = 0.25f * juce::jlimit (0.0f, 1.0f, (subHz - 30.0f) / 20.0f);

    std::array<PanRamp, 3> pan;
    for (int i = 0; i < 3; ++i)
        pan[(size_t) i] = makePanRamp (kOscSpread[i], voiceIndex);

    // Each detune as a ratio offset (ratio - 1), so DETUNE scales it as
    // 1 + offset * detune: at detune 1 that is the designed ratio exactly.
    std::array<float, 3> detuneOffset;
    for (int i = 0; i < 3; ++i)
        detuneOffset[(size_t) i] = std::pow (2.0f, kOscDetuneCents[i] / 1200.0f) - 1.0f;

    for (int n = 0; n < numSamples; ++n)
    {
        const auto envGain = v.env.getNextSample();
        // Voice-steal declick ramp; 1.0 unless this slot is being taken over.
        const auto stealGain = nextStealGain (v);
        const auto brightness = effectiveBrightness (voiceIndex, n);
        const auto stackDriftDepth = driftDepth (detune.value) * unison.driftScale;

        auto advance = [&] (int idx, float freqHz, float depth) noexcept
        {
            const auto drift = vs.drift[(size_t) idx].next (rng, kOscDriftRateHz[idx], sampleRate) * depth;
            const auto inc = juce::jlimit (0.0f, 0.49f, freqHz * (1.0f + drift) * invSr);
            vs.phase[(size_t) idx] = wrapPhase (vs.phase[(size_t) idx] + inc);
            return inc;
        };

        // Three detuned triangle voices, spread across the field by WIDTH.
        float stackL = 0.0f, stackR = 0.0f;

        for (int i = 0; i < 3; ++i)
        {
            const auto inc = advance (i, baseFreq * (1.0f + detuneOffset[(size_t) i] * detune.value), stackDriftDepth);
            const auto tri = polyBlampTriangle (vs.phase[(size_t) i], inc) * (i == 0 ? 1.0f : unison.partnerGain);
            auto& p = pan[(size_t) i];
            stackL += tri * p.left;
            stackR += tri * p.right;
            p.advance();
        }

        // Saw edge and sub stay centred: the sub is bass, and bass is mono.
        const auto sawInc = advance (3, baseFreq, edgeDriftDepth);
        const auto sawEdge = polyBlepSaw (vs.phase[3], sawInc) * 0.15f;

        const auto subInc = advance (4, subHz, edgeDriftDepth);
        detune.advance();
        const auto sub = polyBlampTriangle (vs.phase[4], subInc) * subGain;

        const auto centre = sawEdge + sub;
        const auto bodyL = stackL * 0.3f * unison.normalise + centre;
        const auto bodyR = stackR * 0.3f * unison.normalise + centre;

        // Slow filter "breathe" so the timbre keeps moving once the envelope settles.
        vs.breathePhase = wrapPhase (vs.breathePhase + 0.13f * invSr);
        const auto breathe = std::sin (vs.breathePhase * juce::MathConstants<float>::twoPi) * 0.22f + 0.78f;

        const auto envForCutoff = envGain < sustainForCutoff
                                      ? envGain + onsetLift * (sustainForCutoff - envGain)
                                      : envGain;
        const auto cutoff = juce::jlimit (40.0f, (float) (sampleRate * 0.45),
                                          (350.0f + envForCutoff * 900.0f) * breathe * brightness * tracking
                                              * (1.0f + kBassBrightLift * bassness));
        vs.filter.setCutoffFrequency (cutoff);

        const auto gain = envGain * level * stealGain;
        left[n]  += vs.filter.processSample (0, bodyL) * gain;
        right[n] += vs.filter.processSample (1, bodyR) * gain;
    }
}

} // namespace horizon
