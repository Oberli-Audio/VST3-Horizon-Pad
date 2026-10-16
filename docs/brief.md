# Horizon Pad - sonic brief (G0)

Template: `dsp-sound-design-playbook.md` 6.2. This is the G0 artefact. The gate does
not close until the four `TODO (owner)` fields below are filled and the whole document
is confirmed.

**Status: G0 CLOSED, confirmed by the owner 2026-09-23.** Written on commit
`a0a0aec`. Every field is confirmed except the two items under `open_questions`,
neither of which blocks design work.

```yaml
name:            Horizon Pad (Quelle Music)
type:            synth
tier:            P          # promoted from S on 2026-09-22 - see "Tier history"
one_sentence:    A four-layer ambient pad instrument whose four pads are each usable
                 on their own for a different section of a song, and which blend into
                 one another by volume alone.
references:
  - Vital - a synth you program. Wavetables, a full modulation matrix, an editing
    surface that rewards study. Horizon Pad is not that: it is an INSTRUMENT you
    play. Some depth, nowhere near Vital's, and what depth there is sits on
    performance controls rather than on a design surface.
  - Spitfire Audio (LABS / Originals) - the same "load it and it already sounds
    like the record" immediacy, and the same instrument framing, but sample-based,
    so a library download and disk streaming. Horizon Pad is synthesised in code:
    no samples, instant load, and it never runs out of a round-robin.
  # The frame: an instrument, not a synth. Spitfire's immediacy at Vital's zero
  # install weight. Editing depth is deliberately shallow - see out_of_scope.
must_do:
  - Four contrasting pads, not four variations of one idea; any single pad must hold
    up alone in a mix.
  - Blending is by volume alone. No per-layer tone block beyond WIDTH.
  - Everything synthesised in code: no samples, no audio files, no bitmap artwork.
  - Playable live from a keyboard: zero latency, spring-loaded PITCH, held MOD.
out_of_scope:
  - Per-layer tone knobs (cutoff, detune, osc mix). Confirmed out, 2026-09-23.
    This is the line that keeps the product an instrument rather than a synth -
    each pad's character stays baked in, and a preset stays a point in the
    12-dimensional blend/width/macro space.
  - MPE and per-note expression
  - A resizable GUI (fixed 1080x748, every child paints its own layout)
  - Double-precision processing (supportsDoublePrecisionProcessing left at false)
  # Deferred, NOT permanently excluded:
  #   Sampled content - "follows on a later date" (owner, 2026-09-22). That is a
  #   promotion trigger, not a v1 feature: it changes the install story, the
  #   licensing and the CPU profile, and re-enters at G0 when it happens.
core_controls:
  - VOL x4 (Root/Clearing/Expanse/Bloom) - the blend; the product's central idea
  - FILTER - global brightness macro, ramped per-sample via smoothedBrightness
  - ATTACK / RELEASE - global scalars on each layer's own fixed ADSR timings,
    preserving the relative timing offsets that stop all four pads arriving at once
  - REVERB - shared stereo send, equal-power wet/dry
  - WIDTH x4 - per-pad Haas spread, lead channel alternated per layer
  # Performance state, deliberately NOT host parameters: PITCH (spring-loaded),
  # MOD (holds). Two atomics, read once per block.
signal_flow: |
  MIDI -> central voice allocator (8 slots, 3-tier steal: free -> oldest released
          -> oldest held). Every layer gets the same note in the same slot.
    |
    +-- Root      tri stack + polyBLEP saw + sub(-1 oct) -> LPF (breathing cutoff)
    +-- Clearing  unison stack -> LFO-swept delay 2-11 ms @ 0.11 Hz -> LPF
    +-- Expanse   detuned stack -> swept BPF
    |               \-- HPF -> OctaveShimmer (2-grain, +1 oct) -> small reverb
    +-- Bloom      detuned stack -> LPF (LFO cutoff) -> tremolo
    |
    each layer: -> ADSR (fixed per-layer timings) -> WIDTH (Haas tap, <=90 samples,
                   lead channel alternates L/R/L/R across the four layers)
    |
    sum with per-layer smoothed gain
    -> FxChain: shared stereo juce::dsp::Reverb send, equal-power crossfade,
       wet pre-scaled by kWetCalibrationGain = 0.48
    -> output trim 0.75 -> tanh soft clip -> out
cpu_budget:      CONFIRMED 2026-09-23: < 8% of one core per instance at
                 48 kHz / 64 samples, with 8 voices sounding, all four layers at
                 non-zero volume and REVERB at 50%. Measured in Release, and again
                 AFTER the sound stops (where denormal problems appear).
                 Minimum target machine: "a machine that ran Ableton Live 10
                 comfortably" (owner) - read as a ~2017-2018 dual-core i5 laptop.
                 8% leaves room for 3-4 instances plus a band's worth of other
                 tracks, which is the real worship-rig case.
latency_budget:  0 samples. Non-negotiable: this is played live. No setLatencySamples
                 call exists; to be asserted by impulse measurement in a test (G6).
sample_rates:    [44100, 48000, 88200, 96000, 176400, 192000]
                 # verified clean 2026-09-22: no NaN/Inf/clip/click at any of them
channels:        mono-to-stereo
target_user:     A musician with technical interests - reads a spec, but is
                 playing a set on Sunday, not designing a patch.
target_genre:    Worship and CCM. Reference artists: Rend Collective, Eden Music,
                 Upstream - specifically their acoustic versions. That sets the
                 job: sit UNDER acoustic guitar, piano and group vocals without
                 fighting them, and stay playable live from a controller.
quality_targets:
  alias_floor_db:    ASR < -60 dB measured on a single high note (worst case, sparse
                     spectrum, not a dense chord); NMR < 0 dB across MIDI 21-108.
                     NMR is the decisive metric - ASR can pass while one unmasked
                     alias tone is clearly audible.
  thd_n_db:          The instrument is not a clean path; the only nonlinearity is the
                     output tanh. Target: contribution of the safety clipper < -80 dB
                     at typical preset levels, i.e. it must not engage in normal use.
                     Conditions to be stated with the number.
  response_error_db: < 0.5 dB (analog-style) for the FILTER macro against its design
                     target, with the 44.1 vs 96 kHz responses overlaid - this doubles
                     as the cramping test.
  other:
    - DC offset < -80 dBFS (measured 2026-09-22: ~1e-6 worst case, i.e. ~-120 dB)
    - Denormal count zero
    - Null residual for a pure refactor < -100 dBFS
    - Envelope timing within 10% or 2 ms of each layer's designed ADSR
    - Typical preset chord peaks at -3 dBTP (decided 2026-09-22). DONE
      2026-09-23: output stage re-staged +8.9 dB. Loudest factory presets now
      -2.6 to -4.1 dBTP, median -7.3 dBTP, pathological maximum -0.1 dBTP with
      no clipping. The remaining spread is preset work, not gain-stage work.
    - Factory presets loudness-matched to within +/-1 LU. DONE 2026-09-23:
      bank curated 30 -> 18 on character (docs/preset-curation-2026-09-23.md),
      then matched to -18.0 LUFS. 17 of 18 land within +/-1 LU (measured spread
      1.04 LU across three voicings); Sternenzelt cannot reach it and is
      recorded as EX-002
open_questions:
  - Windows x64 host access: CI builds it, but 5.5 needs a human to tick the boxes.
  - Logic Pro would need an AU target; CMakeLists currently builds VST3 + Standalone.
  - Three of four layers will ship without a Faust prototype (see Tier history).
```

## Tier history

| Date | Tier | Note |
|---|---|---|
| (origin) | S, undeclared | Built as a sketch; G0 was never written |
| 2026-09-22 | **P** | Promoted by the owner. Re-enters at G0 and does every skipped gate, per 0.1. No gate is grandfathered on the grounds that the plugin already works. |

## Decisions taken at promotion (2026-09-22)

1. ~~**Prototype / null-test basis.** Faust is reconstructed for **Root only**, as a
   pilot, to find out whether the C++ can null against a rebuilt prototype at all.
   The other three layers proceed without one, which is a deviation from 6.1's G5 and
   needs a dated 12.5 exception each. 0.1's Tier P definition of null testing
   ("whole-plugin, vs previous release") governs in the meantime.~~

   **SUPERSEDED 2026-09-24, owner-approved.** The premise was false: the original
   prototype exists for all four layers and is now in `sound design/` (see A-001).
   Nothing is reconstructed, no 12.5 exceptions are owed, and G5 runs per-layer
   against the original. See [`g1-algorithm.md`](g1-algorithm.md) for what will and
   will not null.
2. **Output level.** Re-stage to a typical preset chord peak of **-3 dBFS**.
3. **Presets.** Curate 30 down to **~18**, loudness-matched. Measured spread today is
   9.1 dB RMS.
4. **Hosts.** Ableton Live confirmed, plus Waveform Free and Reaper covering
   Windows x64 (5.5's matrix still needs a human to tick the boxes).
5. **Positioning.** An instrument, not a synth. Confirmed 2026-09-23 after the
   first draft read as "no editing surface at all" - there is some depth, it is
   just far shallower than Vital's and lives on performance controls.
6. **Per-layer tone knobs.** Confirmed OUT of scope, 2026-09-23.

## Amendments after confirmation

The YAML above is the owner-confirmed G0 artefact and is left as confirmed. Facts
that have changed since are recorded here instead, dated, rather than edited into it.

### A-001 - the Faust prototype was never lost (2026-09-24)

Promotion decision #1 below, and the third entry under `open_questions`, both assume
there is no Faust source and commit the project to reconstructing one for Root as a
pilot, with three dated 12.5 exceptions owed for the other layers.

That assumption is wrong. `four_pads.dsp` (177 lines, all four layers, header
`STATUS: validated, ready for VST3 port`) sits one directory above the repo in
`../sound design/`, together with the per-layer renders, the five blend mixes and
the handoff document. It is unversioned and outside the repo, which is how it came
to be presumed gone. Details in [`g1-algorithm.md`](g1-algorithm.md).

Therefore:

- Nothing needs reconstructing; G2 becomes import-and-verify for all four layers.
- **The three 12.5 exceptions are not owed.** Decision #1 should be amended, not
  executed - owner's call, since it was an owner decision.
- `open_questions` entry 3 ("Three of four layers will ship without a Faust
  prototype") is void.
- **DONE 2026-09-24:** the prototype is under version control in `sound design/`,
  renders included. The tension with `must_do`'s "no samples, audio files ...
  anywhere in the repo" was resolved by A-003 below.

### A-002 - first G6 evidence (2026-09-24)

Reported by the owner: the **Steinberg validator ran green on CI**, and a **smoke
test in Ableton Live** behaved correctly. This is the first validation evidence since
the four-layer/12-parameter rewrite and retires the first README "Known gaps" bullet.

G6 is **not** closed by it. Still outstanding: pluginval at strictness 10, CTest
cancellation tests (the repo has no tests and `CMakeLists.txt` has no test wiring at
all), and two more hosts on a second platform. CI's validator step is also
`continue-on-error: true`, so it reports rather than gates.

### A-003 - `must_do`'s "no audio files" rule is about the product, not the repo (2026-09-24)

`must_do` says "Everything synthesised in code: no samples, no audio files, no bitmap
artwork." Read literally that forbids the prototype's reference renders from being
committed. Owner-approved 2026-09-24: **the rule governs what the plugin ships and
loads at runtime, not what the repository stores for design reference.**

The binding form of the rule: Horizon Pad contains no sampled or pre-rendered audio
and no bitmap artwork in its build output, and loads none at runtime - every sound
and every pixel is generated in DSP or paint code. Nothing under `sound design/` is
compiled, linked, packaged or read by the plugin.

Now committed under `sound design/` (~1.0 MB):

| File | What it is |
|---|---|
| `four_pads.dsp` | the prototype, all four layers - the design source of truth |
| `Horizon_Pad_VST3_Handoff.md` | control-to-GUI mapping from the original handoff |
| `reference-renders/pad{1..4}_*.mp3` | per-layer renders of the prototype |
| `reference-renders/four_pads_full_blend_v5.mp3` | the blend the owner signed off on |

The renders are the "and heard" half of G2's artefact and the listening reference for
G5. `_Archive/` (v1-v4 and six earlier previews) was left out - superseded takes.

### A-004 - CPU measured, budget not yet met (2026-09-24)

`cpu_budget` is "< 8% of one core ... minimum target machine: a machine that
ran Ableton Live 10 comfortably (a ~2017-2018 dual-core i5)".

Measured 2026-09-24: **5.03%** of one core at 48 kHz / 64 samples, 8 voices,
all four layers up, REVERB at 50%, Release - and **0.60%** over a 60 s silent
tail, so there is no denormal problem.

**That measurement was taken on an Apple M3 Pro and therefore does not close
the budget.** Single-core throughput differs from the target machine by
roughly 3-4x, which would put the same load near 15-20% of a core there. The
figure proves the plugin is not pathologically expensive and that silence is
cheap; it says nothing about the machine the budget was written for. One run
on a low-end x86 laptop is owed, and the Windows validation pass is the
natural place for it.

### A-005 - gate tracking moved to its own page (2026-09-24)

Gate status is no longer tracked in this document. It lives in
[`gate-status.md`](gate-status.md), which links the artefact for each gate.
This file stays what it is: the G0 brief and its dated amendments.

### A-006 - one WIDTH macro, a DETUNE macro, ten parameters (2026-10-02)

Owner decisions, 2026-10-02, following [`width-design-draft.md`](width-design-draft.md):

- **WIDTH x4 becomes one WIDTH macro** (macro 5). Every factory preset gave all four
  pads the same width, so the four knobs carried one knob's worth of information
  and the shape across the pads never changed. Each pad now has a fixed width
  profile (where on the knob it opens, how wide it gets): Expanse from 0% to
  100%, Clearing from 10% to 100%, Bloom from 20% to 90%, Root from 30% to 60%. The air
  opens first and the foundation last, so one knob changes the shape as well as
  the amount. The mechanism is the v2 per-oscillator spread, not the "Haas
  spread" `core_controls` still names (that wording has been stale since v2).
- **DETUNE is added** (macro 6). It scales every pad's own designed unison
  detune and baseline drift, 0.5x-2x on the character-macro curve, 50% = the
  designed sound (bit-exact), easing out below C3. It is a global macro like
  FILTER, which scales each pad's own cutoff, so it does not reopen
  `out_of_scope`'s "per-layer tone knobs (cutoff, detune, osc mix)".
- **Ten host parameters**: four volumes, then ATTACK, RELEASE, FILTER, REVERB,
  WIDTH, DETUNE. Params 1-8 are unchanged, so the Launchkey mapping holds. A
  preset is now a point in a 10-dimensional blend/macro space, and the only
  per-pad control is volume: `must_do`'s "no per-layer tone block beyond WIDTH"
  now reads "no per-layer tone block".
- **No backwards compatibility until the owner says so.** The plugin has not
  been used beyond testing, so parameter IDs, state and presets changed without
  migration. This also answers `sound-design-v2.md`'s open question about a
  version flag for existing projects: none is needed.
- **Naming rule**: a control name must not have a second meaning in audio
  (a "BAND" macro was proposed and rejected for reading as bandwidth). Recorded
  in the playbook, F.14.2 and D.5.2.

Measured and verified in [`width-design-draft.md`](width-design-draft.md) ("Built");
the listening items are in [`gate-status.md`](gate-status.md).

### A-007 - decisions from the owner's blind listening pass (2026-10-07)

The owner ran the blind A/B/X session (v1 `bf17ee8` against the v2 build of
2026-10-04, 19 items, every X identified correctly) and played the new presets.
Results and measurements in [`gate-status.md`](gate-status.md); decisions:

- **WIDTH at 100% is full width again on every pad.** Amends A-006's profiles: Root
  now opens from 20% (was 30%) and Bloom from 15% (was 20%), and both reach their full
  spread at 100% (were 60% and 90%). The opening order - air first, foundation last -
  stays. (Root solo and Sternenzelt at WIDTH 100% lost to v1's full width.)
- **Expanse returns to v1's two-pole safety lowpass** (and v1's level), departing from
  the Faust prototype's one-pole on purpose: the owner preferred v1's Expanse.
- **Bloom's shared pulse is shallower** (chord depth ~29%, was 44%; v1 averaged it to
  16%) - it stays shared, so the depth no longer depends on the voicing.
- **Low chords brighter**: key tracking below C4 eased (Root and Bloom 0.5, Clearing
  0.75, were 0.8-1.0) as far as the roughness limits allow.
- **The default patch plays DETUNE 0%** (Lagerfeuer and the parameter default); 50%
  still reproduces the designed detune.
- **Presets: 17 in three families** (9 pads, 5 in-between, 3 leads), ordered in
  repeating cycles from the longest envelope to the shortest; the plugin opens on
  Lagerfeuer. In-between presets: "the main place to be are harmonies".
- **EX-003 closed**: the owner judged v2's top octave usable and v1's not.

### A-008 - from the second blind listening pass (2026-10-07)

Results in [`gate-status.md`](gate-status.md). The first pass's changes held up (four
items flipped to the candidate, two became inaudible). From the remaining verdicts and
notes: Root's bass starts near its opened tone and sits brighter below C3; Expanse's
shimmer send rolls off above C5 (the C6 chord); DETUNE's drift grows only to 1.5x at
100% ("a bit seasick at max"); the default patch stays at DETUNE 0% (the comparison
was a tie). Proposal P6 (contour-keeping pinning for Expanse) is open for the owner.

### A-009 - worship envelopes: ATTACK/RELEASE up to the designed times (2026-10-14)

The owner's listening test: most presets sounded like a cinematic, film-score pad.
Worship playing needs shorter attack and release, and longer times than the design's
are not needed. Decided by the owner:

- **ATTACK/RELEASE span 1% -> 100% of each layer's designed times** on one exponential
  curve (every 10% of the knob x1.58). 0% is the old fast floor (~10-25 ms); 100% is
  the designed envelope - it was 50%, and 100% was double. `core_controls` still holds
  (global scalars on each layer's own ADSR, the relative offsets kept); what changed is
  the range, and that "designed" sits at 100% rather than at the macros' usual 50%.
  The G5 null test and G3's envelope check now run at 100%, bit-identical to the old 50%.
- **The pads have a third of their former attack and release**: Root 0.64-1.19 s
  attack, 0.70-1.30 s release before the room. In-between and lead presets keep
  exactly their times, so the in-betweens now release longer than the pads; the bank's
  cycles order by attack.
- Loudness is unchanged (every preset within 0.01 LU; the sustained level does not
  depend on the envelope times). Under typical playing the limiter stays linear: the
  loudest preset is still -2.18 dBTP; some pads peak up to 1.3 dB higher than before,
  all at or below -3.2 dBTP.
- Guarded by `attack_release_span` (the curve, both ends) and
  `presets_have_worship_envelopes` (no preset swells longer than 1.2 s or falls away
  slower than 1.4 s, dry); both fail on the build before.
- User presets saved earlier keep their stored knob values and play shorter - no
  migration, the plugin is unreleased.

### A-010 - the register: C0 to C7, set by listening (2026-10-17)

The brief never stated the register the instrument plays (the playbook's brief
template calls the field `playable_range`; this project says **register**). The
engine took every MIDI note, and below C0 and above C7 what came out was not
musical: the owner's listening set the ends. Decided by the owner:

- **The register is C0-C7** (MIDI 12-96, C4 = 60 as everywhere in this code).
  Outside it the instrument is silent: a note-on there starts no voice and steals
  nothing (`horizon::kRegisterLowestNote` / `kRegisterHighestNote`,
  `PluginProcessor::noteOn`). Note-offs, the pedal and the wheels are unaffected.
- Guarded by `register_is_c0_to_c7` (C0 and C7 sound; MIDI 0, 11, 97 and 127 render
  exactly nothing), which fails on the build before (`e7dce97`: MIDI 0 rendered at
  -20 dBFS peak).
- The register battery (`tools/measure/register.py`) now runs C0-C7 instead of
  C1-C5, so every number it reports is inside the register the instrument offers;
  the alias check stops at C7 (it went to MIDI 108, which no longer sounds). The
  listening item `expanse_top` plays F6-A6-C7 instead of C7-E7-G7.
- The playbook change this asked for (v3.5): at Tier P and F the register is set by
  listening, stated in the brief, enforced (silence outside), and the register
  battery runs over exactly that register.

## Where the exceptions go

12.5 requires a written, dated, named exception whenever a measurement fails and the
decision is to ship anyway: what failed, by how much, why it is acceptable, and what
would make it unacceptable. Those live in [`exceptions.md`](exceptions.md), which
currently holds one open entry (EX-001, limiter-induced DC offset).
