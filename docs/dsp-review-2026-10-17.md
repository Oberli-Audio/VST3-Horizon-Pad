# Sound-design review, Tier P - 2026-10-17

Scope: Horizon Pad's sound as shipped on `main` after the register change (A-010),
reviewed against the playbook v3.4 core card: the definition of done (section 9),
the instrument contract (I.0) and battery (I.8), the rule audits `gate-status.md`
still listed as owed (29, 30, 33, 37), and recipe D.8.5 (baselines first, findings
with numbers). Everything marked **measured** was measured today with the Release
`HorizonPadSoundTool` built from this commit, 48 kHz, seeded; the commands are given
so every number reproduces. Nothing here was heard: the listening items are listed
at the end for the owner.

The short version: the instrument is in better shape than any earlier review found
it - every gate's measurement still passes after the worship envelopes and the
register change, the translation battery passes on every preset, and the DSP runs
at a fifth of its deadline at p99. What the review turned up is at the ends of the
register the owner just set, in the control path's missing tests, and in two
descriptors that drifted since the last baseline without anyone deciding they should.

## 1. Baselines frozen today

| Battery | Command | Saved as |
|---|---|---|
| Descriptors (grid, keyboard C2-C7, pocket, stereo, movement, presets), 4 seeds | `python3 tools/measure/descriptors.py --all --seeds 4 --save ... --compare docs/baselines/descriptors-v2-macros.json` | `docs/baselines/descriptors-2026-10-17.json` |
| Register battery C0-C7 plus the two low voicings, 3 seeds | `python3 tools/measure/register.py --seeds 3 --save ...` | `docs/baselines/register-c0-c7-2026-10-17.json` |
| Translation battery, every preset, 3 seeds | `python3 tools/measure/translation.py --seeds 3 --json ...` | `docs/baselines/translation-2026-10-17.json` |
| Test suite | `ctest --test-dir build-release -LE validation` | 25/25 in 26 s (4 parallel) |

## 2. Definition of done (core card section 9), instrument track

| Item | Status | Evidence |
|---|---|---|
| No allocation, locking or I/O in `processBlock`, verified under RTSan (rules 1, 35) | PASS, **stale** | 19 tests clean under RTSan 2026-10-03; the six tests added since (A/R span, worship envelopes, register, Root bass, Expanse top, ...) have not run under RTSan. `build-rtsan` must be rebuilt (CLAUDE.md recipe). |
| `reset()` clears state; seeded runs bit-identical | PASS | `reset_determinism` |
| Clean at 6 rates x 6 block sizes | PASS | `rate_block_matrix` |
| 60 s silence after signal: no NaN, no denormal storm | PASS | `silence_after_note` |
| Latency reported and asserted | PASS | `latency_zero` |
| No zipper at maximum automation rate | **not tested** | No test automates a macro at full rate; FILTER is ramped per sample (`smoothedBrightness`), the volumes by `SmoothedValue`. A sweep test is cheap and owed (D.3). |
| No time constant in samples (rule 21) | PASS | audit 2026-09-26; no new constants since in samples (grep `Samples =` in `Source/dsp`) |
| Presence tests for every defining feature (rule 22) | PASS | `shimmer_octave_present`, `root_sub_present`, `clearing_ensemble_present`, `bloom_tremolo_survives_chords`, `width_profile_staggered`, `detune_spreads_every_stack` |
| Descriptors as a baseline with design intent as numbers | PASS, re-frozen today | section 1 |
| Worst-case block time (rule 37) | **measured today**, budget machine still owed | section 4 |
| Golden render compared at a tolerance | PARTIAL | G5 compares against the prototype at fixed points; no per-commit golden |
| Every fixed defect has a regression test and a failure-library row | PASS | README test table; I.9 rows for the shimmer, width and steal defects |
| **I**: register and a low and a mid voicing measured (rule 24) | PASS, with findings | section 3 |
| **I**: bass mono and unmodulated (rule 26) | PASS | `bass_is_mono`; side below 150 Hz at C0-C2: Root -65/-56/-49 dB, Bloom -38/-34/-30, Clearing -40/-36/-33 |
| **I**: pedal and stealing tiers tested (rule 25) | PASS | `sustain_pedal_holds`, `voice_steal_declick` |
| **I**: CPU per voice and at full polyphony | per voice **not measured**; full polyphony: section 4 | - |
| Translation battery (rule 34) | PASS | section 5 |
| IDs frozen, previous states load (rule 36) | n/a until release | brief: unreleased, no migration |
| Validators blocking in CI | PASS | CI on `f0f12be`: macOS + Windows validator and pluginval L10 |

## 3. The register, C0-C7 (rule 24, I.2) - measured

`register.py` now covers the register the owner set. Values are per layer solo,
reverb 0, 3 seeds, 8 s holds; the v2 baseline (`register-v2.json`, C1-C5) in
brackets where it moved by more than 5%.

**Roughness** (Vassilakis, normalised to the loudest partial; v1's "not musical" bass
measured 1.24 for Root at C1):

| Layer | C0 | C1 | C2 | C3 | C4 | C5 | C6 | C7 | open 5th C2 | triad C2 |
|---|---|---|---|---|---|---|---|---|---|---|
| Root | **1.25** | 0.51 (0.14) | 0.15 (0.05) | 0.05 | 0.01 | 0.01 | 0.02 | 0.04 | 0.38 (0.22) | 1.20 (0.66) |
| Clearing | 1.55 | 0.61 (0.44) | 0.60 (0.37) | 2.37 (1.75) | 1.88 (2.43) | 1.00 (1.82) | 2.07 | **3.73** | 1.44 | 2.19 |
| Expanse | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.01 | 0.01 | 0.02 | 0.01 | 0.09 (0.28) |
| Bloom | **3.63** | 1.76 (0.47) | 0.71 (0.31) | 0.20 (0.13) | 0.06 (0.15) | 0.03 | 0.01 | 0.01 | 1.20 (0.73) | 2.84 (1.45) |

**Pitch salience** (1 = a clean, stable pitch at the played note):

| Layer | C0 | C1 | C2 | C3 | C4 | C5 | C6 | C7 |
|---|---|---|---|---|---|---|---|---|
| Root | 1.00 | 1.00 | 0.99 | 0.39 | 0.39 | 0.25 | **0.12** | **-0.04** |
| Clearing | 1.00 | 1.00 | 1.00 | 1.00 | 1.00 | 1.00 | 1.00 | 1.00 |
| Expanse | 0.98 | 0.99 | 1.00 | 1.00 | 1.00 | 1.00 | 1.00 | 1.00 |
| Bloom | 0.97 | 1.00 | 1.00 | 1.00 | 1.00 | 1.00 | 1.00 | 1.00 |

Findings:

- **R-1 The bottom octave is the roughest place in the instrument.** Bloom at C0
  measures 3.63, Root 1.25, Clearing 1.55 - all above the 1.24 that v1's Root had at
  C1 when the owner called the bass "not musical". Bloom and Root at C1 are 3.7x and
  3.6x their v2 values: listening pass 2 (`05a26fe`, "Root's bass opens sooner") and
  DETUNE's default moving to 0% were heard and chosen, but neither listening session
  had an item below C2 (`make_session.py`: the lowest is `--notes=24,36`). The
  register was set by ear on the whole blend; whether each layer solo is still music
  at C0-C1 has not been heard. Listening item, not a defect.
- **R-2 Root loses its pitch above C5.** Salience 0.25 at C5, 0.12 at C6, -0.04 at
  C7, with centroid/f0 at 0.80-0.84: the sub-octave carries the note and the
  autocorrelation at the played period is gone. In the blend Clearing and Expanse hold
  the pitch there, so this is Root behaving as a foundation; solo it sounds an octave
  low. Decide and write it down (I.2.1 rule 5: "fade what does not belong") - either
  fade Root's sub above C5 or accept it as the design.
- **R-3 Clearing is roughest at C7 (3.73) and at C3 (2.37).** The ensemble's 5.5 Hz
  tap beating against the unison; the C3 figure grew from 1.75. Same listening item as
  R-1's top end (`expanse_top` plays F6-A6-C7 on the full Expanse; add Clearing solo).
- **R-4 Expanse below C4 is one note.** Identical values at C0-C3 (pinned near C5 by
  design, I.2.1 rule 4). Correct, and the reason the layer measures 0.00 roughness in
  the bass. No action.
- **R-5 Clearing's `side_low_db` reads +1.5 dB at C5 and +3.7 dB at C7.** Not a mono-bass
  defect: at C5-C7 nothing of the layer sits below 150 Hz, so the ratio compares two
  noise floors. The metric should report "n/a" below a floor; noted for the tool.

## 4. Rule audits that were owed

| Rule | Verdict | Evidence |
|---|---|---|
| **29** events at sample offsets | PASS | `processBlock` splits the block at every MIDI event (`renderSegment`, `PluginProcessor.cpp:767`) and re-applies pitch-bend/mod at each boundary. A stolen voice starts after its 5 ms fade (`LayerBase::startNote`), documented; 5 ms is above the ~2 ms onset JND but only on steals, and the fade is what removes the click (`voice_steal_declick`). |
| **30** control mappings stated, tested with scripted MIDI | PARTIAL | Stated: velocity `0.35 + 0.65 sqrt(v)` (code comment), bend +/-2 st, CC1 -> MOD. Tested: the pedal (`sustain_pedal_holds`), stealing, same-key retrigger. **Not tested**: velocity curve monotonic and sensible at both ends, bend range, CC1 depth. Note hygiene: all notes off on `prepareToPlay`, CC120/123 handled; a preset change does not cut held notes (deliberate for a pad - the key is still down, nothing is stuck). Three scripted-MIDI tests owed. |
| **33** nonlinear model stated | PARTIAL | The output limiter states its operating point (knee -1.9 dBFS, ceiling -0.04 dBFS, +11.4 dB make-up, `PluginProcessor.cpp:790-822`) and is measured at the pathological case (+2.0 dBTP in -> -0.04 out). Antialiasing: none - `tanh` at 1x. Rule 4 allows that only when documented as deliberate: the comment proves no factory preset reaches the knee, which is the argument; one sentence saying so, and one B.5 rate comparison at the pathological patch, closes it. |
| **37** worst-case block time | **measured** on the M3 Pro, the budget machine still owed | Harness from the 2026-10-14 performance session (`blocktime_release_new`, Release DSP, default patch, 48 kHz, 128-sample blocks = 2.67 ms deadline, 29.5 s with 42 note-ons): mean 212 us (8.0%), **p99 541 us (20.3%)**, p99.9 778 us, **max 1785 us (66.9% of the deadline)**, the worst block at 28.15 s. Note-on blocks are no worse than the rest (p99 507 us). The maximum is 3.3x the p99 and comes in a cluster at 20.0-20.4 s and 28.2-28.4 s: that looks like scheduling, not DSP, but it is the number a 64-sample deadline (1.33 ms) is judged against, and there it would be a dropout. Owed: the same histogram at 64 samples, and on the budget machine (A-004). |

## 5. Descriptors and translation - measured

Against `descriptors-v2-macros.json` (2026-10-02), 4 seeds. Expected moves: attack
times (A-009: Root 1.48 -> 0.65 s, Clearing 2.04 -> 0.58, Expanse 1.93 -> 0.50, Bloom
1.12 -> 0.31), Expanse's high/air (-3.9 / -7.7 dB, the two-pole safety lowpass restored
2026-10-07, heard in EX-003), preset loudness (every preset within 0.5 LU, spread 2.3
LU). Unexpected:

- **D-1 Bloom's movement depth halved**: 43.0 -> 25.1% on a note, 43.5 -> 24.2% in a
  chord; rate 3.17 -> 2.54 Hz. `bloom_tremolo_survives_chords` still passes (its floor
  is 20%). The likely cause is DETUNE's default at 0% (the descriptor counts beating as
  movement; the tremolo itself is unchanged), but nobody decided Bloom should move
  half as much. Either re-baseline with the reason written down, or raise the test
  floor to protect the design.
- **D-2 Expanse's WIDTH changes its level by +0.66 dB** (was -0.11; rule 8, threshold in
  `width_mono_safe_and_level_flat` is 1.0). Within tolerance, but the only pad whose
  width is not level-neutral; the shimmer reverb follows WIDTH since v2. Watch it.
- **D-3 Mono-sum loss at WIDTH 100% grew on every pad** (Root -0.2 -> -0.54 dB, Expanse
  -1.14 -> -1.81, Bloom -1.17 -> -1.67). Still above the -3 dB test floor and the
  translation battery's -3.5 LU; the trend follows A-007 (full width on every pad).

Translation battery (rule 34): **PASS on all 17 presets** - mono loss -0.3 to -2.2 LU
(worst band -2.3 to -3.9 dB), bass band -0.0 dB everywhere (rule 26 holds through the
reverb), phone loss within 1.8 LU of the bank median, AAC decoded true peak at or
below -0.9 dBTP from a -1 dBTP master, MP3 without clipped samples.

## 6. What to do, ranked by audible impact (core card section 7)

1. **Hear the ends of the register** (R-1, R-2, R-3): one blind session with per-layer
   items at C0, C1 and C7 and the blend at C0-E0-G0 and C6. If Bloom at C0 is not
   music, the fix is in `MotionPadLayer` (unison and drift already tighten below C3;
   the filter LFO depth may need the same), not in the register.
2. **Decide Bloom's movement** (D-1) and Root's top octave (R-2); write both down.
3. **Three scripted-MIDI tests** for rule 30 (velocity, bend, CC1) and one automation
   sweep for zipper - all cheap, all in `dsp_tests.py`.
4. **Rebuild `build-rtsan` and run the 25 tests under it** (rule 35 is stale by six tests).
5. **Block time at 64 samples**, and on the budget machine with the Windows pass (A-004,
   rule 37).
6. One sentence on the limiter's aliasing being deliberate (rule 4/33), plus the B.5
   rate comparison at the pathological patch.

## 7. Listening items added to `gate-status.md`

| Item | Question |
|---|---|
| register_bottom | Each layer solo at C0 and C1, and the blend at C0-E0-G0: music, or a wobble with a pitch? (R-1) |
| register_top | Clearing solo at C7 and Root solo at C6-C7: Root sounding an octave low, Clearing's 3.7 roughness. (R-2, R-3) |
| bloom_movement | Bloom with DETUNE 0% against 50%: did the pad lose its life, or only its beating? (D-1) |
