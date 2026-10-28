#include "MotionPadLayer.h"

namespace horizon
{

void MotionPadLayer::prepareLayer (const juce::dsp::ProcessSpec&)
{
    const juce::dsp::ProcessSpec stereoSpec { sampleRate, (juce::uint32) maxBlockSize, 2 };

    for (auto& vs : voiceState)
    {
        vs.filter.prepare (stereoSpec);
        vs.filter.setType (juce::dsp::StateVariableTPTFilterType::lowpass);
        vs.filter.setResonance (0.5f);
    }

    lfoBlock.setSize (2, maxBlockSize, false, true, false);
    resetLayer();
}

void MotionPadLayer::resetLayer()
{
    for (auto& vs : voiceState)
    {
        vs.phase.fill (0.0f);
        vs.drift = {};
        vs.filter.reset();
    }

    lfoBlock.clear();
    reseedLayerState();
}

void MotionPadLayer::reseedLayerState()
{
    tremPhase = rng.nextFloat();
    filterLfoPhase = rng.nextFloat();
}

void MotionPadLayer::startVoice (int voiceIndex, float, float)
{
    auto& vs = voiceState[(size_t) voiceIndex];

    for (int i = 0; i < kNumOscs; ++i)
    {
        vs.phase[(size_t) i] = rng.nextFloat();
        vs.drift[(size_t) i].start (rng, kOscDriftRateHz[i], sampleRate);
    }

    vs.filter.reset();
}

void MotionPadLayer::beginBlock (int numSamples)
{
    const auto invSr = 1.0f / (float) sampleRate;
    const auto twoPi = juce::MathConstants<float>::twoPi;
    auto* trem = lfoBlock.getWritePointer (0);
    auto* cutoff = lfoBlock.getWritePointer (1);

    for (int n = 0; n < juce::jmin (numSamples, lfoBlock.getNumSamples()); ++n)
    {
        filterLfoPhase = wrapPhase (filterLfoPhase + 0.6f * invSr);
        tremPhase = wrapPhase (tremPhase + 3.2f * invSr);

        cutoff[n] = std::sin (filterLfoPhase * twoPi) * 600.0f + 1400.0f;
        // +/-0.20 (0.35 until 2026-10-07): the shared pulse was too obvious in
        // chords - the owner preferred v1's Bloom (owner's blind A/B of 2026-10-06 (git 8a14117:docs/gate-status.md)).
        // Still shared, so the depth no longer depends on the voicing.
        trem[n] = std::sin (tremPhase * twoPi) * 0.20f + 0.65f;
    }
}

void MotionPadLayer::renderVoice (int voiceIndex, juce::AudioBuffer<float>& target, int numSamples)
{
    auto& v = voices[(size_t) voiceIndex];
    auto& vs = voiceState[(size_t) voiceIndex];

    auto* left  = target.getWritePointer (0);
    auto* right = target.getWritePointer (1);
    const auto* trem = lfoBlock.getReadPointer (0);
    const auto* lfoCutoff = lfoBlock.getReadPointer (1);

    const auto invSr = 1.0f / (float) sampleRate;
    const auto baseFreq = bentFrequency (v.frequency);
    const auto unison = unisonFor (baseFreq, kNumOscs);
    auto detune = makeDetuneRamp (baseFreq);
    const auto level = 0.25f * v.velocity; // 0.24 in v1: +0.35 dB restores v1's level, which the shared LFOs and new drift left 0.35 dB low (six-seed match, WIDTH 0)
    const auto tracking = keyTrack (baseFreq, kCutoffTrackingBelowC4, kCutoffTrackingAboveC4);

    std::array<PanRamp, kNumOscs> pan;
    for (int i = 0; i < kNumOscs; ++i)
        pan[(size_t) i] = makePanRamp (kOscSpread[i], voiceIndex);

    for (int n = 0; n < numSamples; ++n)
    {
        const auto envGain = v.env.getNextSample();
        // Voice-steal declick ramp; 1.0 unless this slot is being taken over.
        const auto stealGain = nextStealGain (v);
        const auto brightness = effectiveBrightness (voiceIndex, n);
        const auto depth = driftDepth (detune.value) * unison.driftScale;

        float stackL = 0.0f, stackR = 0.0f;

        for (int i = 0; i < kNumOscs; ++i)
        {
            const auto drift = vs.drift[(size_t) i].next (rng, kOscDriftRateHz[i], sampleRate) * depth;
            const auto freqHz = baseFreq * (1.0f + kOscDetuneFraction[i] * detune.value + drift);
            const auto inc = juce::jlimit (0.0f, 0.49f, freqHz * invSr);
            vs.phase[(size_t) i] = wrapPhase (vs.phase[(size_t) i] + inc);

            const auto saw = polyBlepSaw (vs.phase[(size_t) i], inc) * (i == 0 ? 1.0f : unison.partnerGain);
            auto& p = pan[(size_t) i];
            stackL += saw * p.left;
            stackR += saw * p.right;
            p.advance();
        }

        detune.advance();

        vs.filter.setCutoffFrequency (juce::jlimit (40.0f, (float) (sampleRate * 0.45),
                                                    lfoCutoff[n] * brightness * tracking));

        const auto gain = envGain * level * trem[n] * stealGain;
        left[n]  += vs.filter.processSample (0, stackL * 0.4f * unison.normalise) * gain;
        right[n] += vs.filter.processSample (1, stackR * 0.4f * unison.normalise) * gain;
    }
}

} // namespace horizon
