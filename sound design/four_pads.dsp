//-----------------------------------------------------------------------
// HORIZON PAD - core DSP engine (bob Audio)
// Faust sound-design source of truth - STATUS: validated, ready for VST3 port.
//
// GUI reference (Claude Design draft, matches this engine's 4-layer/8-macro
// shape): https://claude.ai/artifact/JfHpmQcecSdVZXkESLfwVQ
// Full mapping between this file's controls and that GUI's knobs/wheels is
// in the handoff doc delivered alongside this file
// ("Horizon_Pad_VST3_Handoff.md").
//
// Four Pads - built from scratch per "How To Make A Noise" (Simon Cann)
// and "Synth Secrets" (Gordon Reid), plus current sound-design practice.
//
// Design brief (from research, not from the earlier Horizon Pad sketch):
//   - Cann's "main food groups": every sound is placed by its brightness
//     (very bright / bright / dull / very dull) and its attack speed
//     (fast-percussive / fast-non-percussive / medium / slow). Four pads
//     meant to blend by volume alone need to sit at four DIFFERENT points
//     on that grid, or stacking them just gets louder and muddier rather
//     than more interesting.
//   - Cann on richness/warmth: detuned oscillators + a sub-oscillator for
//     richness; slow attack/release + a second, even-slower-enveloped
//     oscillator for warmth and a sense of "changing tone" over time.
//   - Cann on layering: give each layer its OWN filter and envelope
//     (a "layered sound", not a "layered oscillator") so each can be
//     controlled and will age independently - this is what a modern
//     ambient-pad tutorial (MusicRadar) also lands on: separate amp/filter
//     envelopes per layer is what stops layers moving in lockstep.
//   - Synth Secrets on string machines: the classic analog ensemble sound
//     is detuned-oscillator-stack + a genuinely LFO-swept chorus, not a
//     fixed delay (a fixed delay is just a static comb filter and reads
//     as reedy/brassy - a mistake made and fixed earlier on this project).
//   - Vector synthesis (Prophet VS / Wavestation): the classic four-source
//     blend works because the four sources are chosen to be maximally
//     CONTRASTING, not four variations on the same idea.
//
// So the four pads below are deliberately spread across brightness and
// attack speed, and each owns its own oscillators/filter/envelope end to
// end - no shared signal path - so any combination you fade in blends
// rather than mudding up.
//
//   1) Warm Foundation - dull/warm, slow attack     - low-mid body
//   2) Analog Ensemble  - bright, medium attack      - mid/chorused
//   3) Airy Choir       - very bright, very slow     - high/air, long tail
//   4) Motion Pad       - medium, medium attack      - built-in movement
//
// Each has its own volume slider (v:Pads/...) so they blend purely by
// level, the way the user asked - no other interaction between them.
//-----------------------------------------------------------------------

import("stdfaust.lib");

// ---- Performance controls ----
freq = hslider("h:Voice/freq", 220, 20, 4000, 0.01) : si.smoo;
gain = hslider("h:Voice/gain", 0.7, 0, 1, 0.01) : si.smoo;
gate = button("h:Voice/gate");

// Slow analog-style pitch wander. A plain sine LFO, not a filtered-noise
// lowpass - an Nth-order Butterworth smoother at sub-Hz cutoffs is
// numerically unstable in single-precision float over long notes (found
// and fixed earlier on this project). Different "rate" args decorrelate
// oscillators from each other.
drift(rate) = os.oscsin(rate) * 0.004;

//=========================================================================
// PAD 1: Warm Foundation - dull, slow attack, low-mid register.
// The "verse pad": a detuned triangle/saw body plus a sub-oscillator
// (Cann's recipe for richness), heavily low-passed, very slow attack and
// release (Cann's recipe for warmth), with a slow filter "breathe" so it
// isn't frozen.
//=========================================================================
warmFoundation = layer
with {
    osc(det,rate) = os.triangle(freq * (1 + det * 0.01 + drift(rate)));
    sawEdge  = os.sawtooth(freq * (1 + drift(0.15))) * 0.15;
    stack    = (osc(0,0.11) + osc(0.07,0.14) + osc(-0.06,0.09)) * 0.3 + sawEdge;
    sub      = os.triangle(freq * 0.5 * (1 + drift(0.08))) * 0.25;
    body     = stack + sub;
    // Attack pulled in (was 3.2s - part of what made the four pads arrive
    // at noticeably different times) and the breathe LFO deepened a touch
    // so there's still audible movement once the swell has landed.
    env      = en.adsr(2.2, 0.8, 0.75, 2.4, gate);
    breathe  = os.osc(0.13) * 0.22 + 0.78;
    cutoff   = (350 + env * 900) * breathe;
    layer    = body : fi.lowpass(2, cutoff) : *(env);
};

//=========================================================================
// PAD 2: Analog Ensemble - bright, medium attack, chorused mid register.
// The "chorus/bridge pad": a sawtooth stack through a properly LFO-swept
// flanger/chorus (the delay time itself moves - a *fixed* delay is just a
// static comb filter and sounds reedy/brassy, a mistake caught earlier).
//=========================================================================
analogEnsemble = layer
with {
    osc(det,rate) = os.sawtooth(freq * (1 + det * 0.01 + drift(rate)));
    stack    = (osc(0.03,0.19) + osc(-0.04,0.23) + osc(0.09,0.27)) * 0.3;
    sweepMs  = (os.oscsin(0.11) * 0.5 + 0.5) * 0.009 + 0.002; // 2-11ms sweep
    ensemble = stack : pf.flanger_mono(2048, sweepMs * ma.SR, 0.55, 0, 0);
    // Still had a "headstart" after the attack-time fix - the real cause
    // was the filter: it started at a fixed 1800Hz (already fairly open)
    // instead of sweeping open like the other pads, so it was bright from
    // the very first instant regardless of the envelope. Now it starts
    // closed like Warm Foundation does and opens with the envelope, plus
    // the attack is pulled in line with the other pads.
    env      = en.adsr(2.6, 0.7, 0.8, 2.2, gate);
    cutoff   = 700 + env * 2200;
    layer    = ensemble : fi.lowpass(2, cutoff) : *(env);
};

//=========================================================================
// PAD 3: Airy Choir - very bright, very slow attack, high/air register
// with a long tail. Modelled on the "digital texture" layer in modern
// ambient-pad tutorials: a bandpass that sweeps up into the high end over
// a slow LFO, plus an octave-up shimmer send into its own reverb, so this
// pad occupies clear air above the other three rather than competing
// with them. All-oscillator source (no noise), per earlier feedback.
//=========================================================================
// Round 2 of taming: the entrance was fine but it kept swelling past the
// other pads (5s attack to a sustain of 0.7 means it's still growing long
// after everything else has settled) and both the release and the reverb
// tail ran too long. Fixed: much lower sustain level so it doesn't build
// into the dominant voice, release cut from 5s to 1.8s, reverb shortened
// and darkened further, sweep range and safety lowpass both dropped for
// more darkness, shimmer pulled back again.
// Round 3: overall evolution pulled in a lot (attack 5s -> 2.6s) so it
// lands with the other three pads instead of arriving noticeably later,
// but the sweep LFO is sped up a little so there's still clear ongoing
// movement within that shorter window rather than a static hold.
airyChoir = layer
with {
    osc(det,rate) = os.triangle(freq * 2 * (1 + det * 0.01 + drift(rate)));
    stack    = (osc(0.02,0.29) + osc(-0.03,0.31) + osc(0.05,0.24)) * 0.3;
    env      = en.adsr(2.6, 1.0, 0.4, 1.8, gate);
    sweepHz  = 450 + (os.oscsin(0.09) * 0.5 + 0.5) * 900;
    swept    = stack : fi.resonbp(sweepHz, 1.6, 1) : fi.lowpass(1, 1800);
    shimmer  = swept : fi.highpass(2, 1200) : ef.transpose(2048, 512, 12)
                      : re.mono_freeverb(0.45, 0.25, 0.6, 0);
    layer    = (swept * 0.55 + shimmer * 0.16) : *(env);
};

//=========================================================================
// PAD 4: Motion Pad - medium brightness, medium attack, built-in movement.
// The odd one out on purpose: where the other three sustain and swell,
// this one has audible tremolo and filter pulsing, so it reads as
// "motion/interest" rather than another sustain layer - useful for
// sections that need life without adding more static harmonic content.
//=========================================================================
motionPad = layer
with {
    osc(det,rate) = os.sawtooth(freq * (1 + det * 0.01 + drift(rate)));
    stack    = (osc(0.04,0.33) + osc(-0.05,0.37)) * 0.4;
    trem     = os.osc(3.2) * 0.35 + 0.65;
    filtLfo  = os.osc(0.6) * 600 + 1400;
    env      = en.adsr(1.2, 0.5, 0.7, 1.6, gate);
    layer    = stack : fi.lowpass(2, filtLfo) : *(env) : *(trem);
};

//=========================================================================
// Mixer - four independent level sliders, blend purely by volume.
//=========================================================================
dry = (
      warmFoundation * vslider("v:Pads/[1]Warm Foundation", 0.7, 0, 1, 0.01)
    + analogEnsemble  * vslider("v:Pads/[2]Analog Ensemble", 0.35, 0, 1, 0.01)
    + airyChoir       * vslider("v:Pads/[3]Airy Choir",      0.35, 0, 1, 0.01)
    + motionPad       * vslider("v:Pads/[4]Motion Pad",      0.4, 0, 1, 0.01)
) * gain;

// A little room, plus a short Haas-style offset between channels so the
// output isn't a flat, dead-center mono signal.
wetMono = dry : re.mono_freeverb(0.7, 0.35, 0.25, 0) * 0.28;

outL = (dry : de.delay(4096, 0))  * 0.85 + wetMono;
outR = (dry : de.delay(4096, 90)) * 0.85 + wetMono;

// Master trim - re-checked empirically against clipping (see render tests).
process = outL * 1.6, outR * 1.6;
