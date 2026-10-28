#pragma once

#include "HorizonTypes.h"

namespace horizon
{

/**
    Common machinery shared by the four Four Pads sound layers.

    Voice allocation happens once, centrally, in the processor: every layer is
    told about the same note in the same voice slot, so a chord plays coherently
    across all four layers. Each layer keeps its own oscillator state per voice
    and its own amplitude envelope with layer-specific, fixed (non-user-editable)
    ADSR timings taken straight from the validated Faust sound design
    (four_pads.dsp) - unlike an earlier iteration of this plugin, the current
    product spec exposes no per-layer tone knobs, only the four pad volumes and
    four global macros, so each layer's character is baked in rather than
    user-editable.

    Three global macros reach every layer identically, set once per block by
    the processor:
      * attackTimeScale     - ATTACK macro. 1.0 = the layer's own designed
                               attack time, the knob's top since brief A-009
                               (it reaches 0.01..1.0); <1 snappier. Multiplies
                               attack only, so each layer's relative timing
                               offset (part of what keeps the four pads from
                               all arriving at once) is preserved.
      * releaseTimeScale    - RELEASE macro. 1.0 = the layer's own designed
                               release time, likewise the top; <1 shorter. Multiplies
                               release only, the same way attackTimeScale
                               multiplies attack.
      * brightness           - FILTER macro, ramped per sample (smoothedBrightness,
                               not stepped once per block like the other two
                               above) since it feeds a filter cutoff directly
                               and a block-rate step there is audible as
                               zipper noise. 1.0 = the layer's own designed
                               cutoff curve; <1 darker, >1 brighter. Multiplies
                               whatever cutoff-over-time curve the layer already
                               computes from its envelope, rather than replacing
                               it, so the "opens with the envelope" shape from
                               the sound design survives at every macro setting.
                               Read live per voice per sample via
                               effectiveBrightness(), which uses this shared
                               ramp unless that voice was frozen by
                               freezeBrightnessForActiveVoices() - see its doc
                               comment for why.

    Stereo width comes from one WIDTH macro shared by all four layers. Each
    layer turns it into its own width through a fixed profile
    (widthProfile(): where on the knob the layer starts to open, and how wide
    it gets), so the shape across the pads changes as the knob turns - the
    air opens first, the foundation last; since 2026-10-07 every pad reaches
    its full spread at WIDTH 100% (Root stopped at 60%, Bloom at 90%). Until 2026-10-02 there
    was a WIDTH knob per pad, and every factory preset set all four to the
    same value. Since sound-design v2 (2026-09-26) the width itself is built by spreading
    each voice's detuned oscillators across the stereo field (panGains()),
    not by a Haas delay tap on the summed layer. Detuned oscillators are
    decorrelated, so the spread is mono-safe by construction - no comb
    filter, no null - and constant-power panning keeps the level flat from
    WIDTH 0 to 1. The old Haas tap measured a comb in mono and a ~1.5 dB
    level drop at full width (docs/dsp-review-2026-09-26.md, S-3). WIDTH 0
    reproduces the old mono sound exactly: every pan gain is 1.

    DETUNE (2026-10-02) scales every layer's unison spread: the static detune
    of each stack oscillator and its baseline drift - and on Clearing, Expanse
    and Bloom the drift is most of the spread (static detune 0.3-1.6 cents,
    drift about +/-7). The multiplier runs 0.5x..2x on the macroMultiplier
    curve, so 50% is the designed sound, bit-exact. It eases out below C3
    (makeDetuneRamp(), the same register blend as unisonFor()), so the bass
    tightening keeps low notes stable whatever DETUNE does. It leaves alone
    what is not a unison partner: Root's saw edge and sub-octave, and the
    extra drift the MOD wheel adds on top (driftDepth()).

    Performance state (pitch bend, mod wheel) also reaches every layer
    identically, set once per block:
      * pitchBendSemitones  - added to every voice's frequency this block.
      * modAmount            - extra analog-style drift depth on top of each
                               layer's own baseline drift, 0 = baseline only.

    Rendering is done into a per-layer scratch buffer so that each layer can
    apply its own filtering/effects independently before being summed into the
    mix by the processor.

    Everything here is allocation-free once prepare() has run. prepare() is the
    only method that may allocate and is only ever called from prepareToPlay().
*/
class LayerBase
{
public:
    virtual ~LayerBase() = default;

    void prepare (double newSampleRate, int maximumBlockSize)
    {
        sampleRate = newSampleRate;
        maxBlockSize = juce::jmax (1, maximumBlockSize);

        scratch.setSize (2, maxBlockSize, false, true, false);
        scratch.clear();

        stealFadeSamples = juce::jmax (1, (int) (kStealFadeSeconds * newSampleRate));

        for (auto& v : voices)
        {
            v.env.setSampleRate (newSampleRate);
            v.env.reset();
            v.active = false;
            clearStealState (v);
        }

        smoothedWidth.reset (newSampleRate, 0.05);
        smoothedWidth.setCurrentAndTargetValue (0.0f);

        smoothedDetune.reset (newSampleRate, 0.05);
        smoothedDetune.setCurrentAndTargetValue (1.0f);

        // See effectiveBrightness()/render() below: FILTER's brightness
        // multiplier is ramped per sample, the same way width and (in the
        // processor) volume and reverb send already are, so twisting FILTER
        // - by hand or under host automation - sweeps the cutoff smoothly
        // instead of stepping it once per block.
        smoothedBrightness.reset (newSampleRate, 0.02);
        smoothedBrightness.setCurrentAndTargetValue (1.0f);
        brightnessBlock.setSize (1, maxBlockSize, false, true, false);
        brightnessBlock.clear();

        prepareLayer ({ newSampleRate, (juce::uint32) maxBlockSize, 2 });
    }

    void reset()
    {
        for (auto& v : voices)
        {
            v.env.reset();
            v.active = false;
            clearStealState (v);
        }

        scratch.clear();
        resetLayer();
    }

    /** Called from the audio thread, once per block. */
    void setMacros (float newAttackTimeScale, float newReleaseTimeScale, float newBrightnessMultiplier) noexcept
    {
        attackTimeScale = newAttackTimeScale;
        releaseTimeScale = newReleaseTimeScale;
        smoothedBrightness.setTargetValue (newBrightnessMultiplier);
    }

    /** Called from the audio thread, but NOT once per block - only when the
        processor detects a preset/buffer switch, and only just before that
        block's setMacros() lands the new preset's FILTER value (so
        smoothedBrightness's current value here still reflects the *old*,
        pre-switch ramp). Snapshots that old value into every currently active voice (held or
        still releasing) and locks it there for the rest of that voice's
        life, so an already-sounding note keeps fading on its original
        timbre instead of jumping to the new preset's FILTER setting. Voices
        triggered afterwards are unaffected - see noteOn(). */
    void freezeBrightnessForActiveVoices() noexcept
    {
        // getCurrentValue() peeks the ramp without advancing it, so this
        // doesn't perturb the per-sample sweep render() drives below.
        const auto current = smoothedBrightness.getCurrentValue();

        for (auto& v : voices)
        {
            if (v.active)
            {
                v.frozenBrightness = current;
                v.brightnessFrozen = true;
            }
        }
    }

    /** Called from the audio thread, once per block, with the WIDTH macro
        (0..1). widthProfile() turns it into this layer's own width: 0 = mono
        (every oscillator centred), 1 = the layer's full designed spread. */
    void setWidth (float widthMacro) noexcept
    {
        smoothedWidth.setTargetValue (profiledWidth (widthProfile(), widthMacro));
    }

    /** Called from the audio thread, once per block, with the DETUNE macro's
        multiplier (0.5..2, 1 = the designed detune). */
    void setDetune (float newDetuneScale) noexcept
    {
        smoothedDetune.setTargetValue (juce::jlimit (0.25f, 4.0f, newDetuneScale));
    }

    /** Called from the audio thread, once per block. */
    void setPerformance (float newPitchBendSemitones, float newModAmount) noexcept
    {
        pitchBendSemitones = newPitchBendSemitones;
        modAmount = newModAmount;
    }

    /**
        Starts `midiNote` in this slot, declicking the handover if the slot is
        still sounding.

        Taking over a slot used to be a hard reset of the envelope and the
        oscillator phases mid-cycle. Measured with HorizonPadSoundTool
        (`--note-onsets` to force a steal, `--probe-at` on the steal instant),
        that step was -2..-11 dB relative to the signal, against -18..-23 dB
        for a note-on into a free slot: an audible click, and easy to provoke
        with a sustain pedal and more than kMaxVoices notes held.

        So a sounding slot is ramped to zero over kStealFadeSeconds first and
        the incoming note is started once the ramp lands - at the next block
        boundary after it completes, which is at most one block late. That is
        well under the ~2 ms onset JND for every attack this instrument can
        produce (the ATTACK macro floors out at 12 ms), and a pad has no
        transient to smear.
    */
    void startNote (int voiceIndex, int midiNote, float velocity)
    {
        auto& v = voices[(size_t) voiceIndex];

        // A silent slot has nothing to click: take it over immediately.
        if (! v.active)
        {
            beginNote (voiceIndex, midiNote, velocity);
            return;
        }

        v.pendingNote = midiNote;
        v.pendingVelocity = velocity;
        v.notePending = true;

        if (v.stealFadeRemaining <= 0)
            v.stealFadeRemaining = stealFadeSamples;
    }

    void beginNote (int voiceIndex, int midiNote, float velocity)
    {
        auto& v = voices[(size_t) voiceIndex];
        clearStealState (v);
        v.midiNote = midiNote;
        v.frequency = (float) juce::MidiMessage::getMidiNoteInHertz (midiNote);
        v.velocity = velocity;
        v.active = true;
        v.brightnessFrozen = false; // a freshly triggered note always tracks the live FILTER macro

        juce::ADSR::Parameters p;
        p.attack  = juce::jmax (0.001f, attackSeconds() * attackTimeScale);
        p.decay   = decaySeconds();
        p.sustain = sustainLevel();
        p.release = juce::jmax (0.001f, releaseSeconds() * releaseTimeScale);
        v.env.setParameters (p);
        v.env.reset();
        v.env.noteOn();

        startVoice (voiceIndex, v.frequency, velocity);
    }

    void noteOff (int voiceIndex)
    {
        voices[(size_t) voiceIndex].env.noteOff();
    }

    /** Hard stop, no declick ramp - for panic / allNotesOff(immediately),
        where the host wants silence now. Ordinary note starts go through
        startNote(), which ramps. */
    void killVoice (int voiceIndex)
    {
        auto& v = voices[(size_t) voiceIndex];
        v.env.reset();
        v.active = false;
        clearStealState (v);
    }

    /**
        Renders this layer and sums it into `target` (stereo).
        Audio thread only: no allocation, no locks, no file I/O.
    */
    void render (juce::AudioBuffer<float>& target, int numSamples)
    {
        jassert (numSamples <= scratch.getNumSamples());
        numSamples = juce::jmin (numSamples, scratch.getNumSamples());

        scratch.clear (0, numSamples);

        // WIDTH for this block: pan gains are interpolated linearly from the
        // block's start width to its end width (rule 18 - a control signal
        // driving a gain is interpolated to sample rate, not stepped).
        blockWidthStart = smoothedWidth.getCurrentValue();
        smoothedWidth.skip (numSamples);
        blockWidthEnd = smoothedWidth.getCurrentValue();
        blockLength = numSamples;

        // DETUNE the same way: a per-sample ramp across the block (rule 18 -
        // it drives pitch), see makeDetuneRamp().
        blockDetuneStart = smoothedDetune.getCurrentValue();
        smoothedDetune.skip (numSamples);
        blockDetuneEnd = smoothedDetune.getCurrentValue();

        beginBlock (numSamples);

        // Precompute this block's brightness ramp once (not once per voice):
        // every renderVoice() call below reads brightnessBlock[n] by sample
        // index, so many voices sharing a block all see the same ramp
        // instead of each call racing it forward independently.
        {
            auto* bb = brightnessBlock.getWritePointer (0);

            for (int n = 0; n < numSamples; ++n)
                bb[n] = smoothedBrightness.getNextValue();
        }

        // A stolen slot whose declick ramp finished during the last block
        // starts its queued note here, before anything is rendered.
        for (int i = 0; i < kMaxVoices; ++i)
        {
            auto& v = voices[(size_t) i];

            if (v.notePending && v.stealFadeRemaining <= 0)
                beginNote (i, v.pendingNote, v.pendingVelocity);
        }

        for (int i = 0; i < kMaxVoices; ++i)
        {
            auto& v = voices[(size_t) i];

            if (! v.active)
                continue;

            renderVoice (i, scratch, numSamples);

            if (! v.env.isActive())
                v.active = false;
        }

        renderLayerTail (scratch, numSamples);

        for (int ch = 0; ch < juce::jmin (2, target.getNumChannels()); ++ch)
            target.addFrom (ch, 0, scratch, ch, 0, numSamples);
    }

    bool isVoiceActive (int voiceIndex) const noexcept { return voices[(size_t) voiceIndex].active; }

    /**
        Pins this layer's RNG so a render is reproducible.

        The plugin never calls this: per-voice random start phases and drift
        are deliberate (see the rng member). The offline harness does, because
        "two seeded runs from reset are bit-identical" is how a null test and a
        regression baseline are possible at all. Layer-wide random state (a
        shared LFO phase, say) was drawn in prepare(), before the seed arrived,
        so it is re-drawn here.
    */
    void setRandomSeed (juce::int64 seed) noexcept
    {
        rng.setSeed (seed);
        reseedLayerState();
    }

protected:
    struct Voice
    {
        bool active = false;
        int midiNote = -1;
        float frequency = 440.0f;
        float velocity = 0.0f;
        juce::ADSR env;

        // See freezeBrightnessForActiveVoices() below.
        bool brightnessFrozen = false;
        float frozenBrightness = 1.0f;

        // Voice-steal declick, see startNote(). stealFadeRemaining counts the
        // ramp down to zero; notePending marks a note-on waiting for it.
        int stealFadeRemaining = 0;
        bool notePending = false;
        int pendingNote = -1;
        float pendingVelocity = 0.0f;
    };

    /**
        Irregular, slow pitch drift for one oscillator: smoothed random
        segments, not a sine.

        The prototype used a sine LFO because Faust's `no.lfnoise` (a
        Butterworth smoother at a sub-Hz cutoff) is unstable in single
        precision - a constraint of the prototyping tool, not a sound-design
        decision, and one C++ does not share. A sine drift is periodic, and
        periodic movement is one of the cues that reads as synthetic; the
        playbook's guidance for movement is "small, slow, irregular".

        Each segment runs from one uniform random target in [-1, 1] to the
        next along a smoothstep (continuous in value and slope, so no pitch
        corner), and each segment's length is jittered +/-30% around half the
        period of the old sine, so the timescale is the one the sound design
        was voiced with. kRmsMatch scales the result to the old sine's RMS
        (0.707): E[v^2] of this construction is 0.2476, RMS 0.4976. State is
        double precision (numerics, playbook 3.3).
    */
    struct Drift
    {
        static constexpr double kRmsMatch = 0.70711 / 0.49760;

        double from = 0.0, to = 0.0, position = 0.0, step = 0.0;

        void start (juce::Random& random, float rateHz, double sr) noexcept
        {
            from = random.nextDouble() * 2.0 - 1.0;
            to = random.nextDouble() * 2.0 - 1.0;
            position = random.nextDouble();
            step = segmentStep (random, rateHz, sr);
        }

        /** One sample of drift, scaled to the old sine's RMS (so +/-1 nominal). */
        float next (juce::Random& random, float rateHz, double sr) noexcept
        {
            position += step;

            if (position >= 1.0)
            {
                position -= 1.0;
                from = to;
                to = random.nextDouble() * 2.0 - 1.0;
                step = segmentStep (random, rateHz, sr);
            }

            const auto s = position * position * (3.0 - 2.0 * position);
            return (float) ((from + (to - from) * s) * kRmsMatch);
        }

        static double segmentStep (juce::Random& random, float rateHz, double sr) noexcept
        {
            // A sine at rateHz goes extreme-to-extreme twice per period.
            const auto jitter = 0.7 + 0.6 * random.nextDouble();
            return 2.0 * (double) rateHz * jitter / sr;
        }
    };

    /**
        Key tracking for a filter cutoff: (f / C4)^amount.

        Every filter in the Faust design is fixed in Hz and was voiced around
        C4, so the layers' level and brightness changed by up to 27 dB across
        the keyboard (docs/dsp-review-2026-09-26.md, S-2). Anchoring the
        tracking at C4 leaves the voiced sound at C4 exactly as designed -
        the same convention as the macros' "0.5 = the validated sound" - and
        makes the rest of the keyboard follow it. amount 0 = no tracking,
        1 = the cutoff moves with the note (constant timbre).
    */
    static float keyTrack (float frequencyHz, float amount) noexcept
    {
        return std::pow (frequencyHz / kKeyTrackAnchorHz, amount);
    }

    /** Key tracking with a different amount below and above C4. Below C4 a
        fixed-Hz filter lets through more harmonics as the note falls - twice
        as many at C2 as at C4 - and below ~C3 those harmonics sit closer
        together than a critical band, which is roughness: measured 6-40x the
        C4 figure on single notes (tools/measure/register.py, review of
        2026-09-26). v2 tracked fully below C4 (amount 0.8-1.0); the owner's
        blind A/B of 2026-10-06 preferred v1's brighter low chords, so since
        2026-10-07 Root and Bloom track 0.5 and Clearing 0.75 below C4 - as far
        towards v1 as the roughness limits allow (low_register_stays_musical). */
    static float keyTrack (float frequencyHz, float amountBelow, float amountAbove) noexcept
    {
        return keyTrack (frequencyHz, frequencyHz < kKeyTrackAnchorHz ? amountBelow : amountAbove);
    }

    static constexpr float kKeyTrackAnchorHz = 261.6256f; // C4, MIDI 60

    /** Smoothstep from 0 at loHz to 1 at hiHz: the register helpers below
        change nothing above hiHz, so the sound design above C3 is untouched. */
    static float registerBlend (float frequencyHz, float loHz, float hiHz) noexcept
    {
        const auto t = juce::jlimit (0.0f, 1.0f, (frequencyHz - loHz) / (hiHz - loHz));
        return t * t * (3.0f - 2.0f * t);
    }

    /**
        Unison in the bass. Detuned partners beat at a rate proportional to
        frequency: a shimmer of 1-4 Hz in the middle register, a slow, deep
        phasing swell at C1-C2, where it reads as the note going out of tune
        rather than as life. Below C3 the partner oscillators fade towards
        kBassPartnerFloor of their level and the drift depth halves, so the
        bass tightens into a stable note - the way a string section's low
        players are fewer and a synth programmer tightens detune for bass.
        Unchanged at and above C3 (130.8 Hz).
    */
    struct Unison
    {
        float partnerGain = 1.0f;   ///< gain for every oscillator except the centre one
        float normalise = 1.0f;     ///< keeps the stack's power where it was with full partners
        float driftScale = 1.0f;
    };

    static Unison unisonFor (float frequencyHz, int numOscs) noexcept
    {
        const auto blend = registerBlend (frequencyHz, kBassRegisterLowHz, kBassRegisterHighHz);
        Unison u;
        u.partnerGain = kBassPartnerFloor + (1.0f - kBassPartnerFloor) * blend;
        const auto partners = (float) (numOscs - 1);
        u.normalise = std::sqrt ((1.0f + partners) / (1.0f + partners * u.partnerGain * u.partnerGain));
        u.driftScale = 0.5f + 0.5f * blend;
        return u;
    }

    static constexpr float kBassRegisterLowHz = 55.0f;    // A1: fully "bass"
    static constexpr float kBassRegisterHighHz = 130.81f; // C3: fully the voiced sound
    static constexpr float kBassPartnerFloor = 0.35f;

    /** Constant-power pan gains for a position in [-1, 1], normalised so the
        centre is (1, 1): WIDTH 0 leaves every oscillator exactly as loud in
        each channel as it was before stereo spread existed. */
    static void panGains (float position, float& left, float& right) noexcept
    {
        const auto angle = (juce::jlimit (-1.0f, 1.0f, position) + 1.0f) * juce::MathConstants<float>::pi * 0.25f;
        left  = std::cos (angle) * juce::MathConstants<float>::sqrt2;
        right = std::sin (angle) * juce::MathConstants<float>::sqrt2;
    }

    /**
        Per-block pan ramp for one oscillator of one voice. `spread` is the
        oscillator's designed position at WIDTH 1; odd voice slots mirror it,
        so the oscillators of a chord interleave across the field instead of
        every note stacking its sharp oscillator on the same side.
    */
    struct PanRamp
    {
        float left = 1.0f, right = 1.0f, leftStep = 0.0f, rightStep = 0.0f;

        void advance() noexcept { left += leftStep; right += rightStep; }
    };

    PanRamp makePanRamp (float spread, int voiceIndex) const noexcept
    {
        const auto mirrored = (voiceIndex % 2 == 1) ? -spread : spread;
        float l0, r0, l1, r1;
        panGains (mirrored * blockWidthStart, l0, r0);
        panGains (mirrored * blockWidthEnd, l1, r1);
        const auto inv = 1.0f / (float) juce::jmax (1, blockLength);
        return { l0, r0, (l1 - l0) * inv, (r1 - r0) * inv };
    }

    /** This block's width at its end (after the profile) - for layer-wide
        stereo such as a bus reverb's width. */
    float currentWidth() const noexcept { return blockWidthEnd; }

    /**
        Where on the WIDTH knob this layer starts to open, and how wide it gets
        at 100%. Between `start` and 100% the layer's width rises linearly
        from 0 to `maximum`; below `start` it stays mono. Fixed per layer, like
        its ADSR timings - a statement of the pad's role, not a user setting.
    */
    struct WidthProfile
    {
        float start = 0.0f;
        float maximum = 1.0f;
    };

    static float profiledWidth (WidthProfile profile, float widthMacro) noexcept
    {
        const auto x = juce::jlimit (0.0f, 1.0f, widthMacro);
        const auto opened = juce::jlimit (0.0f, 1.0f, (x - profile.start) / (1.0f - profile.start));
        return profile.maximum * opened;
    }

    /**
        Per-block DETUNE ramp for one voice: the multiplier on the layer's
        designed unison detune, interpolated across the block (rule 18).

        Below C3 it eases back to 1 - the designed detune - reaching it at A1,
        the same register blend unisonFor() uses to tighten the bass, so the
        low register stays the stable note the register pass made it whatever
        DETUNE is set to. At 1.0 (50%) every value is exactly 1, and the
        layers' arithmetic reproduces the designed sound bit for bit.
    */
    struct DetuneRamp
    {
        float value = 1.0f, step = 0.0f;

        void advance() noexcept { value += step; }
    };

    DetuneRamp makeDetuneRamp (float frequencyHz) const noexcept
    {
        const auto blend = registerBlend (frequencyHz, kBassRegisterLowHz, kBassRegisterHighHz);
        const auto start = 1.0f + (blockDetuneStart - 1.0f) * blend;
        const auto end   = 1.0f + (blockDetuneEnd - 1.0f) * blend;
        return { start, (end - start) / (float) juce::jmax (1, blockLength) };
    }

    /**
        Pitch-drift depth (as a frequency ratio) for an oscillator. The
        baseline - Faust's drift() depth - scales with DETUNE (`detune`, from
        a DetuneRamp; pass 1 for an oscillator that is not a unison partner);
        the MOD wheel's extra movement is added on top, unscaled.
    */
    float driftDepth (float detune) const noexcept
    {
        // Above the designed detune (1.0) the slow pitch drift grows at half the
        // rate of the static detune: 1.5x at DETUNE 100%, not 2x. The owner found
        // DETUNE 100% "a bit seasick" (owner's second blind A/B, 2026-10-07 (git 8a14117:docs/gate-status.md)); the
        // static detune keeps its full range, so the stack still thickens.
        const auto driftScale = detune > 1.0f ? 1.0f + (detune - 1.0f) * 0.5f : detune;
        return kBaseDriftDepth * driftScale + modAmount * kModDriftDepth;
    }

    static constexpr float kBaseDriftDepth = 0.004f;
    static constexpr float kModDriftDepth = 0.010f;

    /** The FILTER macro value a voice should render with at this sample: its
        own frozen snapshot if freezeBrightnessForActiveVoices() caught it
        still sounding across a preset/buffer switch, otherwise this block's
        precomputed brightness ramp at sampleIndex (see render()) - so
        real-time filter sweeps on a held note keep working smoothly, with no
        block-rate stepping, right up until a preset change, at which point
        that note is done listening. */
    float effectiveBrightness (int voiceIndex, int sampleIndex) const noexcept
    {
        const auto& v = voices[(size_t) voiceIndex];
        return v.brightnessFrozen ? v.frozenBrightness : brightnessBlock.getSample (0, sampleIndex);
    }

    /**
        This voice's voice-steal declick gain for one sample, advancing the
        ramp. Layers multiply their per-sample output by it, so a slot being
        taken over fades instead of cutting - see startNote().

        Once the ramp has landed the voice stays silent until render() starts
        the queued note at the next block boundary.
    */
    float nextStealGain (Voice& v) noexcept
    {
        if (v.stealFadeRemaining <= 0)
            return v.notePending ? 0.0f : 1.0f;

        --v.stealFadeRemaining;
        return (float) v.stealFadeRemaining / (float) stealFadeSamples;
    }

    static void clearStealState (Voice& v) noexcept
    {
        v.stealFadeRemaining = 0;
        v.notePending = false;
        v.pendingNote = -1;
        v.pendingVelocity = 0.0f;
    }

    /** Subclass hooks. */
    virtual void prepareLayer (const juce::dsp::ProcessSpec&) {}
    virtual void resetLayer() {}
    virtual void startVoice (int /*voiceIndex*/, float /*frequency*/, float /*velocity*/) {}

    /** Re-draw any layer-wide random state (not per-voice state, which is
        drawn at each note-on) after the RNG has been re-seeded. */
    virtual void reseedLayerState() {}

    /** Called once per block before any voice is rendered. */
    virtual void beginBlock (int /*numSamples*/) {}

    virtual void renderVoice (int voiceIndex, juce::AudioBuffer<float>& target, int numSamples) = 0;

    /** Optional post-voice processing that must run even with no active voices. */
    virtual void renderLayerTail (juce::AudioBuffer<float>& /*target*/, int /*numSamples*/) {}

    /** This layer's fixed (Faust-validated) ADSR timings, in seconds/0..1. Subclasses override. */
    virtual float attackSeconds() const noexcept = 0;
    virtual float decaySeconds() const noexcept = 0;
    virtual float sustainLevel() const noexcept = 0;
    virtual float releaseSeconds() const noexcept = 0;

    /** This layer's fixed response to the WIDTH macro - see WidthProfile. */
    virtual WidthProfile widthProfile() const noexcept = 0;

    /** Cheap band-limited-ish saw: a polyBLEP-corrected ramp. */
    static float polyBlepSaw (float phase, float phaseIncrement) noexcept
    {
        float value = 2.0f * phase - 1.0f;

        if (phase < phaseIncrement)
        {
            const auto t = phase / phaseIncrement;
            value -= (t + t - t * t - 1.0f);
        }
        else if (phase > 1.0f - phaseIncrement)
        {
            const auto t = (phase - 1.0f) / phaseIncrement;
            value -= (t * t + t + t + 1.0f);
        }

        return value;
    }

    /**
        Triangle from a 0..1 phase, with polyBLAMP correction at both corners.

        The naive triangle's corners are slope discontinuities; their
        harmonics fall at 12 dB/octave, which is gentle enough at the played
        pitch but not an octave up at the top of the keyboard: Expanse's
        stack runs at 2 x f0 and measured aliases 21 dB below the layer's
        peak at MIDI 108 (EX-003). polyBLAMP - the integrated counterpart of
        polyBLEP - rounds each corner over the two samples around it and
        leaves the waveform untouched everywhere else.

        Corners: phase 0 is the peak (slope changes by -8 per unit phase),
        phase 0.5 the trough (+8). The residual is scaled by the slope change
        per sample, 8 * phaseIncrement.
    */
    static float polyBlampTriangle (float phase, float phaseIncrement) noexcept
    {
        auto value = 4.0f * std::abs (phase - 0.5f) - 1.0f;
        const auto slopeChange = 8.0f * phaseIncrement;

        value -= slopeChange * blampResidual (phase, phaseIncrement);

        auto shifted = phase + 0.5f;
        if (shifted >= 1.0f)
            shifted -= 1.0f;

        value += slopeChange * blampResidual (shifted, phaseIncrement);
        return value;
    }

    /** Two-point polyBLAMP residual for a unit slope change at phase 0 (per
        sample), evaluated at `phase` with `increment` = phase advance per sample. */
    static float blampResidual (float phase, float increment) noexcept
    {
        if (phase < increment)
        {
            const auto t = phase / increment - 1.0f;   // -1..0 after the corner
            return -t * t * t / 6.0f;
        }

        if (phase > 1.0f - increment)
        {
            const auto t = (phase - 1.0f) / increment + 1.0f; // 0..1 before the corner
            return t * t * t / 6.0f;
        }

        return 0.0f;
    }

    /** 4-point, 3rd-order Hermite interpolation (rule 10: modulated delays are
        not read with linear interpolation). x is the fractional position
        between y0 and y1; ym1 and y2 are the neighbours either side. */
    static float hermite (float ym1, float y0, float y1, float y2, float x) noexcept
    {
        const auto c0 = y0;
        const auto c1 = 0.5f * (y1 - ym1);
        const auto c2 = ym1 - 2.5f * y0 + 2.0f * y1 - 0.5f * y2;
        const auto c3 = 0.5f * (y2 - ym1) + 1.5f * (y0 - y1);
        return ((c3 * x + c2) * x + c1) * x + c0;
    }

    static float wrapPhase (float phase) noexcept
    {
        return phase - std::floor (phase);
    }

    /** Applies pitchBendSemitones to a base frequency. Call from renderVoice. */
    float bentFrequency (float baseFrequency) const noexcept
    {
        return baseFrequency * std::pow (2.0f, pitchBendSemitones / 12.0f);
    }

    double sampleRate = 44100.0;
    int maxBlockSize = 512;
    std::array<Voice, (size_t) kMaxVoices> voices;

    juce::AudioBuffer<float> scratch;

    /** Set once per block by the processor from the ATTACK/RELEASE/FILTER macros. */
    float attackTimeScale = 1.0f;
    float releaseTimeScale = 1.0f;

    /** FILTER macro (brightness), ramped per sample rather than stepped once
        per block - see setMacros()/render()/effectiveBrightness(). */
    juce::SmoothedValue<float> smoothedBrightness;
    juce::AudioBuffer<float> brightnessBlock;

    /** Voice-steal declick ramp length. 5 ms is long enough to remove the
        step (a 5 ms raised ramp has no energy above roughly 200 Hz worth
        speaking of) and short enough to be well inside the ~2 ms onset JND's
        tolerance for a pad with a >=12 ms attack. */
    static constexpr float kStealFadeSeconds = 0.005f;
    int stealFadeSamples = 1;

    /** This layer's width, after its profile - see setWidth() and makePanRamp(). */
    juce::SmoothedValue<float> smoothedWidth;
    float blockWidthStart = 0.0f;
    float blockWidthEnd = 0.0f;
    int blockLength = 1;

    /** DETUNE multiplier - see setDetune() and makeDetuneRamp(). */
    juce::SmoothedValue<float> smoothedDetune;
    float blockDetuneStart = 1.0f;
    float blockDetuneEnd = 1.0f;

    /** Set once per block by the processor from MIDI/UI performance state. */
    float pitchBendSemitones = 0.0f;
    float modAmount = 0.0f;

    /** Oscillator/LFO start phases and drift are randomised per voice so that
        two instances in the same project do not start phase-locked and sum
        coherently - deliberate, and the reason two unseeded renders of the
        same build are not bit-identical. setRandomSeed() pins it for
        measurement. */
    juce::Random rng { juce::Random::getSystemRandom().nextInt64() };
};

} // namespace horizon
