# Sound-design v2 - what changed, and what it measured

Branch `sound-design-v2`, started 2026-09-26 from `main` at `bf17ee8`. Answers the
findings in [`dsp-review-2026-09-26.md`](dsp-review-2026-09-26.md). **Merged into
`main` on 2026-10-04 (`2724152`)**; the owner's blind passes of 2026-10-06 and
2026-10-07 then judged it against v1 - verdicts and follow-up changes in
`gate-status.md` (git `8a14117`). Kept as
the record of what v2 changed and measured; the listening list at the end is
historical.

Reproduce every number with `tools/measure/descriptors.py` (four seeds per figure)
against `docs/baselines/descriptors-main-bf17ee8.json`, which is `main`'s DSP
measured with the fixed tool.

## Status by step

| Step | What | State |
|---|---|---|
| 1 | Review findings S-1 to S-9 fixed, tests, preset loudness restored | **done** (below) |
| 2 | Low register: measure why low notes stop being musical, fix it | **done** (below) |
| 3 | Playbook v2.2 (review lessons + drums, bass, harmonic, melodic, register) | **done** (below) |
| 4 | Blind A/B listening kit | **done** (`tools/listening/make_session.py`, used 2026-10-06 and 2026-10-07) |

## Step 1 - what changed

| Change | File | Why (review item) |
|---|---|---|
| `--param` repeated flags accumulate | `tools/sound_tool/Main.cpp` | Harness bug: only the last `--param` was applied. G5, the alias check, the EDR measurement and two tests measured a different patch than they asked for. G5 correction appended to `g5-null-test.md`. |
| `--pedal=down,up,...` (CC64) | `tools/sound_tool/Main.cpp` | Makes the sustain pedal testable |
| Descriptor tool, four-seed averaging | `tools/measure/descriptors.py` | G-1, G-3, G-8 |
| WIDTH = per-oscillator stereo spread (constant power, odd voices mirrored), Haas tap removed | `LayerBase.h`, all layers | S-3 |
| Key tracking anchored at C4 (Root/Clearing/Bloom 0.5, Expanse sweep and safety LP 0.7) | all layers | S-2 |
| Drift: smoothed random segments, RMS-matched to the old sine | `LayerBase.h` | S-6 |
| polyBLAMP triangles | `LayerBase.h`, Root, Expanse | EX-003 |
| Root: sub faded out between 50 and 30 Hz | `WarmFoundationLayer.cpp` | S-2, S-4 |
| Clearing: wet-only 3-tap stereo ensemble on the layer bus (0.6 Hz + 5.5 Hz + common 0.11 Hz drift), Hermite reads, sized in seconds; +7.2 dB make-up | `AnalogEnsembleLayer.*` | S-7, rules 10 and 21 |
| Expanse: one-pole safety LP (as the prototype), stereo shimmer reverb following WIDTH, anti-alias LP before the octave shifter | `AiryChoirLayer.*`, `OctaveShimmer.h` | G5, S-3 |
| Bloom: tremolo and filter LFO shared by all voices | `MotionPadLayer.*` | S-5 |
| Reverb send: one-pole bass split at 160 Hz (bass stays dry, only the high band is crossfaded), 20 ms pre-delay | `FxChain.*` | S-4 |
| Sustain pedal, 4-tier stealing (free, released, pedalled, held), same-key retrigger releases the old voice | `PluginProcessor.*` | S-8 |
| 17 presets' volumes scaled back to their `main` loudness; defaults follow Lagerfeuer | `Presets.cpp`, `PluginProcessor.cpp` | 5.3 |

## Step 1 - measured result (interim; final figures are in "Final numbers" below)

**Across the keyboard** - solo LUFS at C2, C3, C4, C5, C6, C7; span C2-C6:

| Layer | main | v2 |
|---|---|---|
| Root | -19.6 -17.2 -18.5 -20.4 -25.4 -31.2 (span 8.2) | -20.4 -19.5 -16.0 -17.8 -19.5 -19.6 (span 4.4) |
| Clearing | -21.1 -20.1 -19.9 -21.0 -21.3 -24.9 (span 1.4) | -16.6 -15.6 -19.5 -15.9 -16.3 -16.4 (span 4.0) |
| Expanse | -39.9 -32.7 -27.3 -31.9 -42.0 -57.5 (span 14.7) | -29.4 -27.1 -28.9 -26.5 -23.4 -24.1 (span 6.0) |
| Bloom | -22.0 -20.5 -22.0 -23.5 -25.7 -31.7 (span 5.2) | -22.5 -20.0 -20.2 -20.9 -21.3 -20.9 (span 2.5) |

Clearing's keyboard figures predate its +7.2 dB make-up gain being tuned at the
chord; its span, not its absolute level, is the point here.

**Stereo at WIDTH 100%** (chord, reverb off): mono-sum loss -2.7/-2.8/-1.2/-2.9 dB
-> -0.8/-2.2/-1.2/-2.0 dB; level change WIDTH 0 -> 100% -1.5/-1.5/-0.8/-1.5 dB ->
0.0/+0.1/0.0/+0.1 dB (Root/Clearing/Expanse/Bloom).

**Movement** - envelope modulation depth, single C4 / C3-E4 chord: Bloom
45%/13% -> 43%/43% (the tremolo now survives chords); Clearing 27%/14% ->
16%/18%; Root and Expanse within a few points.

**REVERB level-flatness** (rule 8; five seeds, 0 -> 100%): main Root -0.3,
Clearing -1.1, default patch -0.4 dB; v2 +0.7, -1.0, +0.6 dB. The first v2 split
(two-pole) measured +2.5 dB on Root and was replaced; see `FxChain.h`.

**Preset bank**: every factory preset within 0.01 LU of its `main` loudness
except Sternenzelt, deliberately left 2.4 LU louder - it was 4.8 LU quiet
(EX-002) and v2 halves that gap.

**CPU**: 8 voices, all layers, REVERB 50%, 48 kHz / 64: 4.9% -> 5.2% of one M3
Pro core (best of 3, same render, analysis included).

**Tests**: 15/15 in 35 s. The seven new ones were run against `main` as well;
five fail there for exactly the defect they guard (mono ensemble, 13% chord
tremolo, 16.6 LU Expanse span, WIDTH mono loss, no pedal) - they test what they
say.

## Step 2 - the low register

The owner's report: "Horizon Pad sounds good in the middle, but in the low register
it is not musical any more." Measured with the new `tools/measure/register.py`
(three seeds, reverb off) on `main`, it is three things:

1. **Roughness.** Below ~500 Hz a critical band is ~100 Hz wide - wider than the
   harmonic spacing of a low note - so neighbouring harmonics grate. With every
   filter fixed in Hz, a low note also carried *more* harmonics than the C4 the
   sound was voiced at. Root at C1 measured 40x its C4 roughness, Bloom 20x;
   Clearing's close C2 triad scored 9.0.
2. **Slow unison phasing.** Detune beats at a rate proportional to frequency: a
   shimmer at C4, a slow swell at C2 that reads as the note going out of tune.
3. **The pad's identity leaves its register.** Expanse, the "air above", sat at
   131 Hz on a C2; v2's first key-tracking pass even pulled its sweep into
   200-500 Hz (+6.8 dB there). And v2's stereo spread had put the bass in the
   side channel (6-10 dB more low side than v1).

The fixes, all unchanged at and above C3/C4 so the voiced sound stays put:

| Fix | Where |
|---|---|
| Asymmetric key tracking: Root 0.8 / Clearing 1.0 / Bloom 1.0 below C4 (low notes keep C4's harmonic count), 0.5 / 0.25 / 0.5 above | layer headers |
| Bass unison: partner oscillators fade to 35% and drift halves from C3 down to A1, power-normalised | `LayerBase::unisonFor` |
| Clearing's ensemble only choruses the band above 200 Hz (one-pole, power-complementary split); ensemble made power-neutral (taps / sqrt 3) and the level moved into `kLayerLevel` | `AnalogEnsembleLayer` |
| Expanse register pinning: below C4 its stack stays near C5, crossfading between the two nearest octaves of the played pitch class (the organ-mixture "break back"); level matched to v1 at C4 | `AiryChoirLayer` |
| Mono bass: side channel high-passed at 140 Hz, 24 dB/oct - since 2026-10-03 at the end of the chain, after the reverb return (before the split, the reverb rebuilt stereo bass: -5.3 dB side/mid) | `FxChain` |

Measured, `main` -> v2 (C1, C2 single notes | C2 open fifth, C2 close triad):

| Roughness | main | v2 |
|---|---|---|
| Root | 1.24, 0.28 \| 0.63, 1.33 | 0.14, 0.05 \| 0.22, 0.66 |
| Clearing | 2.45, 2.54 \| 6.04, 8.95 | 0.44, 0.37 \| 1.22, 2.01 |
| Bloom | 2.28, 0.65 \| 1.24, 2.64 | 0.47, 0.31 \| 0.73, 1.45 |
| Expanse | 0.06, 0.01 \| 0.22, 0.28 | 0.00, 0.00 \| 0.06, 0.28 |

- Expanse sounds at 16x / 8x / 4x f0 at C1 / C2 / C3, loudness span C2-C6
  1.5 LU (main 14.7), 200-500 Hz energy at the reference chord 2.7 dB *below* main.
- Side below 100 Hz on a full-width C2 chord: -12.3 dB -> better than -15 dB
  (test `bass_is_mono`).
- Clearing's mid-register "roughness" (~2.4 vs ~0.8 on main) is the metric
  counting the ensemble's 5.5 Hz vibrato sidebands, not roughness a listener
  hears - a known limit of the Vassilakis model on modulated sounds.
- Presets within 0.35 LU of main; 17/17 tests (two new: `low_register_stays_musical`,
  `bass_is_mono`, both failing on main).

Also found on the way: the descriptor tool measured band energy on the mono sum,
which inherited v1's Haas comb and made v1 look 3 dB less muddy than it was. It
now measures per-channel power. With that, the default patch's 200-500 Hz is
+1.2 dB against main (at +0.5 LU overall) - the pocket target (S-4) is not met
by v2 and is left as an owner call, since the obvious fix, a low-mid dip, would
change the warmth that was signed off.

## Final numbers (final code; `docs/baselines/*-v2.json` against `*-main-bf17ee8.json`)

Loudness per note, solo, C2 C3 C4 C5 C6 C7 (span C2-C6):

| Layer | main | v2 |
|---|---|---|
| Root | -19.6 -17.2 -18.5 -20.4 -25.4 -31.2 (8.2) | -23.1 -20.6 -16.6 -17.8 -19.5 -19.7 (6.5) |
| Clearing | -21.1 -20.1 -19.9 -21.0 -21.3 -24.9 (1.4) | -21.4 -20.2 -18.5 -16.2 -17.4 -19.0 (5.2) |
| Expanse | -39.9 -32.7 -27.3 -31.9 -42.0 -57.5 (14.7) | -27.5 -27.5 -27.5 -28.7 -29.0 -31.8 (1.5) |
| Bloom | -22.0 -20.5 -22.0 -23.5 -25.7 -31.7 (5.2) | -23.5 -21.0 -20.8 -20.7 -21.3 -20.9 (2.8) |

Root at C2 is 3.5 dB below main on purpose: main's C2 was mostly its 33 Hz
sub-octave (centroid 55 Hz), which v2 fades out. Clearing's span grew from 1.4 to
5.2 LU (its top octaves are louder); a listening item.

At WIDTH 100%: mono-sum loss -2.7/-2.8/-1.2/-2.9 -> -0.6/-1.7/-1.1/-1.5 dB, level
change -1.5/-1.5/-0.8/-1.5 -> -0.3/-0.2/-0.1/-0.3 dB. Chord modulation depth:
Bloom 13% -> 43%, the others within 2 points. Presets within 0.1-0.34 LU of main
(Sternenzelt +3.4, EX-002 gap roughly halved). Default patch 200-500 Hz +1.2 dB at
+0.5 LU overall.

## Step 3 - playbook v2.2

`~/.claude/docs/dsp-sound-design-playbook.md` (reference) and
`dsp-playbook-core.md` (core card), plus the invariants mirror and both skills:

- 1.4 Register: the psychoacoustics per band and seven design rules (asymmetric key
  tracking, bass unison, mono unmodulated bass, register pinning, fading what does
  not belong, key-scaled decay, a written level curve), with the measurement battery.
- 2.6 drums and percussion, 2.7 bass and sub-bass, 2.8 pads/keys/chord beds, 2.9
  leads/plucks/arps - briefs by musical role, each with recipe, traps and
  measurements.
- 3.1 prototype-era workarounds, modulation-domain port fidelity, prototype as the
  frozen v1 reference; 3.2 power-complementary splits and multi-tap make-up gain.
- 4.11 sound-design descriptors; 4.12 verify the harness.
- Rules 24-28, role rows in the selection table, brief fields `musical_role`,
  `playable_range`, `register_intent`, `mix_context`; nine failure-library rows.

## Decisions still owed (owner)

- **Existing projects will sound different.** Playbook 5.7 says a sound-changing
  fix keeps the old path behind a version flag once a product is released. If
  v1 is in users' hands, v2 needs that flag; if not, v2 simply becomes the sound.
  *Answered 2026-10-02 (owner): not used beyond testing, so no flag - see
  `brief.md` A-006.*
- **Root's detune** is +7/-6 cents in C++ and +1.2/-1.0 in the prototype. Not
  changed - character call.
- The brief's `core_controls` describes WIDTH as a "Haas spread"; v2 changes the
  mechanism (proposed amendment, not made: `brief.md` is owner-confirmed).
  *Recorded in `brief.md` A-006 (2026-10-02), together with WIDTH becoming one
  macro.*

## Listening list for the A/B

Level-matched against `main` renders; everything above is measured, none of it
heard.

1. WIDTH: spread vs Haas - image width and depth, all four pads, 50% and 100%.
   *2026-10-06: spread preferred at 50% (Klarheit), Haas at 100% (Root, Sternenzelt) -> every pad reaches full spread at 100% since 2026-10-07.*
2. Clearing: ensemble vs slow flanger - is it still Clearing?
3. Drift: irregular vs sine - does the pad still "breathe"?
4. Bloom: shared pulse in chords - musical, or too obvious?
   *2026-10-06: v1's Bloom preferred - too obvious; the shared pulse is shallower since 2026-10-07.*
5. Expanse: one-pole LP and stereo shimmer - brighter; too bright?
   *2026-10-06: v1's Expanse preferred - back to the two-pole LP since 2026-10-07.*
6. Across the keyboard: C2 and C6 chords on every pad.
7. REVERB with the bass kept dry: does the room still feel big?
   *2026-10-06: Bergecho - v2 preferred ("fuller"); Steinerne Ruhe - v1 preferred; to be heard again.*
8. The low register: C1-C3 single notes and a C2 open fifth / close triad on every
   pad - is it music now?
   *Heard 2026-10-07, owner: "acceptably good - not the best choice for bass or
   sub-bass, but still musical for that type of instrument."*
9. Expanse's register pinning: a bass line C2-G2-C3 - does the "air above" follow
   naturally, or is the octave crossfade audible?
   *2026-10-06: v2 preferred (Expanse bass line).*
10. Clearing's top octaves (C5-C7), now louder than its middle.
   *2026-10-06: v2 preferred (Clearing C5-E5-G5).*
