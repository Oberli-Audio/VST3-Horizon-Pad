#include "FxChain.h"

namespace horizon
{

void FxChain::prepare (double sampleRate, int maximumBlockSize)
{
    currentSampleRate = sampleRate;

    const juce::dsp::ProcessSpec stereoSpec { sampleRate, (juce::uint32) juce::jmax (1, maximumBlockSize), 2 };

    reverb.prepare (stereoSpec);
    reverbParams.roomSize = 0.7f;
    reverbParams.damping = 0.35f;
    reverbParams.width = 1.0f;
    reverbParams.wetLevel = 1.0f;   // fully wet; the send-level blend happens by hand below
    reverbParams.dryLevel = 0.0f;
    reverbParams.freezeMode = 0.0f;
    reverb.setParameters (reverbParams);

    dryMono.setSize (1, juce::jmax (1, maximumBlockSize), false, true, false);
    wetStereo.setSize (2, juce::jmax (1, maximumBlockSize), false, true, false);
    lowBand.setSize (2, juce::jmax (1, maximumBlockSize), false, true, false);

    for (auto& hp : sideHighpass)
    {
        hp.prepare ({ sampleRate, (juce::uint32) juce::jmax (1, maximumBlockSize), 1 });
        hp.setType (juce::dsp::StateVariableTPTFilterType::highpass);
        hp.setResonance (0.7071f);
        hp.setCutoffFrequency (kMonoBassHz);
    }

    bassSplit.prepare (stereoSpec);
    bassSplit.setType (juce::dsp::FirstOrderTPTFilterType::lowpass);
    bassSplit.setCutoffFrequency (kBassDryHz);

    preDelaySamples = juce::jmax (1, (int) std::round (kPreDelaySeconds * sampleRate));
    preDelay.assign ((size_t) preDelaySamples, 0.0f);

    const auto ramp = 0.05; // 50 ms
    smoothedReverbSend.reset (sampleRate, ramp);
    smoothedReverbSend.setCurrentAndTargetValue (0.28f);

    reset();
}

void FxChain::reset()
{
    reverb.reset();
    for (auto& hp : sideHighpass)
        hp.reset();
    bassSplit.reset();
    dryMono.clear();
    wetStereo.clear();
    lowBand.clear();
    std::fill (preDelay.begin(), preDelay.end(), 0.0f);
    preDelayWrite = 0;
}

void FxChain::setParameters (float reverbSend) noexcept
{
    reverbSend = juce::jlimit (0.0f, 1.0f, reverbSend);
    smoothedReverbSend.setTargetValue (reverbSend);
}

void FxChain::process (juce::AudioBuffer<float>& buffer, int startSample, int numSamples)
{
    const auto numChannels = juce::jmin (2, buffer.getNumChannels());

    if (numChannels == 0 || numSamples <= 0)
        return;

    numSamples = juce::jmin (numSamples, dryMono.getNumSamples());

    auto* left  = buffer.getWritePointer (0, startSample);
    auto* right = numChannels > 1 ? buffer.getWritePointer (1, startSample) : left;

    // Each layer now has its own width, so left/right may genuinely differ by
    // this point - the reverb send is still built from a mono sum (a stereo
    // room send from two already-different channels would just smear the
    // image), but the dry path below preserves left/right exactly as they
    // arrived rather than rebuilding one from the other. The side channel is
    // high-passed at the very end (see kMonoBassHz), after the reverb return.

    // Split off the bass (see the class comment): low stays dry, high is
    // what the reverb hears and what REVERB crossfades. low + high == dry.
    auto* lowL = lowBand.getWritePointer (0);
    auto* lowR = lowBand.getWritePointer (1);

    for (int n = 0; n < numSamples; ++n)
    {
        lowL[n] = bassSplit.processSample (0, left[n]);
        lowR[n] = right != left ? bassSplit.processSample (1, right[n]) : lowL[n];
    }

    // The send is the mono sum of the high band, pre-delayed.
    auto* dry = dryMono.getWritePointer (0);

    for (int n = 0; n < numSamples; ++n)
    {
        const auto high = right != left ? 0.5f * ((left[n] - lowL[n]) + (right[n] - lowR[n]))
                                        : left[n] - lowL[n];
        dry[n] = preDelay[(size_t) preDelayWrite];
        preDelay[(size_t) preDelayWrite] = high;
        preDelayWrite = (preDelayWrite + 1) % preDelaySamples;
    }

    // Both wet channels start from the same mono sum - the stereo image in
    // the tail comes entirely from Reverb's own internal stereo-spread comb
    // filters once it's driven through processStereo() (see the member
    // comment in FxChain.h), not from feeding it different input per side.
    auto* wetL = wetStereo.getWritePointer (0);
    auto* wetR = wetStereo.getWritePointer (1);
    juce::FloatVectorOperations::copy (wetL, dry, numSamples);
    juce::FloatVectorOperations::copy (wetR, dry, numSamples);

    {
        juce::dsp::AudioBlock<float> block (wetStereo.getArrayOfWritePointers(), 2, 0, (size_t) numSamples);
        juce::dsp::ProcessContextReplacing<float> ctx (block);
        reverb.process (ctx);
    }

    for (int n = 0; n < numSamples; ++n)
    {
        const auto reverbSend = smoothedReverbSend.getNextValue();

        // Turning REVERB up adds a decorrelated tail on top of the dry
        // signal, which raises total output energy if the dry path stays at
        // a fixed level - that's the "reverb makes it louder" complaint.
        // A flat trim (as this used to be) can't fully fix it because the
        // wet tail isn't just "more of the same level" - it's already
        // louder than the dry signal feeding it (see kWetCalibrationGain).
        // Instead this is an equal-power (cos/sin) crossfade between dry
        // and the level-matched wet signal: with dry and (calibrated) wet
        // at roughly equal RMS and largely decorrelated, cos^2 + sin^2 = 1
        // keeps total power ~constant across the whole sweep, not just at
        // the endpoints. At REVERB=0 this reduces to the original fixed
        // 0.85 dry trim (cos(0) = 1) with no wet, so the un-reverbed sound
        // is unchanged; at REVERB=1 it's pure (calibrated) wet.
        const auto theta = reverbSend * juce::MathConstants<float>::halfPi;
        const auto dryLevel = 0.85f * std::cos (theta);
        const auto wetLevel = 0.85f * kWetCalibrationGain * std::sin (theta);

        left[n]  = lowL[n] * 0.85f + (left[n]  - lowL[n]) * dryLevel + wetL[n] * wetLevel;
        right[n] = lowR[n] * 0.85f + (right[n] - lowR[n]) * dryLevel + wetR[n] * wetLevel;
    }

    // Mono bass (see kMonoBassHz): high-pass the side channel of the finished
    // mix - dry layers AND reverb return. Done last because the reverb's
    // decorrelated return rebuilds a side channel from its mono send: with
    // this filter before the send (until 2026-10-03) the dry bass was mono
    // but REVERB 100% put the side back at -5.3 dB under the mid below 100 Hz.
    // Every stage above treats L and R identically and the send only uses the
    // mid, so the dry path's result is unchanged by the move.
    if (right != left)
    {
        for (int n = 0; n < numSamples; ++n)
        {
            const auto mid = 0.5f * (left[n] + right[n]);
            const auto side = sideHighpass[1].processSample (0, sideHighpass[0].processSample (0, 0.5f * (left[n] - right[n])));
            left[n]  = mid + side;
            right[n] = mid - side;
        }
    }
}

} // namespace horizon
