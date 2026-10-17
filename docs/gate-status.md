# Tier P gate status

One page: where Horizon Pad stands against the playbook's gates (v3.6: core card
section 2, F.15.1). Last updated **2026-10-17**. The current state is on top; the
dated sections under "History" are the record of how it got here.

Tier **P** (Product), promoted from S on 2026-09-22, which means re-entering at
G0 and doing every gate the project skipped as a sketch.

| Gate | Status | Artefact |
|---|---|---|
| **G0** Brief | **CLOSED** 2026-09-23, owner-confirmed | [`brief.md`](brief.md) + amendments A-001..A-011 (A-010 the register C0-C7, A-011 ATTACK/RELEASE show a time) |
| **G1** Algorithm | **CLOSED** 2026-09-24 | [`g1-algorithm.md`](g1-algorithm.md) |
| **G2** Prototype | **CLOSED** 2026-09-24 | [`g2-prototype.md`](g2-prototype.md), `sound design/four_pads.dsp` |
| **G3** Measure | **CLOSED** 2026-09-24, two rows short of the brief | [`g3-measurements.md`](g3-measurements.md) |
| **G4** Port | **CLOSED** | Clean Release build; constants documented beside the code |
| **G5** Null test | **CLOSED** on "characterised and declared intentional" | [`g5-null-test.md`](g5-null-test.md) |
| **G6** Validate | **3 of 4** - hosts: the owner's three-DAW play-through | below |
| **G7** Product | **CLOSED**, compatibility matrix pending hosts | README + below |

## G6 in detail

| Requirement | Status |
|---|---|
| Steinberg validator exit 0 | **PASS** 47/47 (VST3 SDK 3.8.1), in CI on every push to main, macOS and Windows |
| pluginval strictness 10 | **PASS**, in CI on the downloaded macOS and Windows artefacts, all six sample rates; `tools/validate/validate.py` runs the same locally |
| CTest suite green | **PASS 29/29** on 2026-10-17 (four control-path tests added: `velocity_curve`, `pitch_bend_range`, `mod_wheel_adds_movement`, `automation_is_zipper_free`) |
| Real-time safety (rule 35) | **PASS 29/29 under RealtimeSanitizer**, 2026-10-17 (`build-rtsan` rebuilt fresh; the self-test aborts as it must). Covers per-block automation during playback |
| Three hosts, two platforms | **1 of 3.** Ableton Live on macOS confirmed by the owner. The owner's play-through in three DAWs is the next step |

## What is outstanding

1. **Three hosts, two platforms** (G6) - the owner's play-through; the release gate's
   interface checks are listed in [`ui-review-2026-10-17.md`](ui-review-2026-10-17.md).
2. **The listening items** below.
3. **CPU on the budget machine** (A-004, rule 37). On the M3 Pro, 8 voices at every
   pad's full volume, measured with `HorizonPadSoundTool --profile`, three runs
   each: **p99 6% of the deadline** at 64 and at 128 samples. Maxima 16-42%; three
   single blocks in ~114,000 went over (100-150%), each at a different time in its run -
   the OS preempting the test thread, not the DSP (D.2.1). The 2017-era i5 the budget is written for is
   roughly 3-4x slower.
4. **FILTER response error at 44.1 vs 96 kHz** (G3). Not measured as a swept response;
   mitigated structurally by every filter being TPT/ZDF.
5. **CI**: the RTSan job is local only (`validate.py` runs it when `build-rtsan` exists).

## Reviews of 2026-10-17 (Tier P) - integrated the same day

- **Sound**: [`dsp-review-2026-10-17.md`](dsp-review-2026-10-17.md) - DoD checklist,
  the register battery over C0-C7, rule audits 29/30/33/37, descriptor drift,
  translation battery PASS on all 17 presets; baselines in
  `docs/baselines/*-2026-10-17.json`. Its resolution table: four control-path tests,
  RTSan re-run, block time in the harness, the limiter's 1x `tanh` documented with a
  rate comparison, listening items for the open questions.
- **Interface**: [`ui-review-2026-10-17.md`](ui-review-2026-10-17.md) - one grade-3 and
  eight grade-2 findings, all fixed (SIZE menu, double-click reset, edited preset dims,
  preset arrows, ATTACK/RELEASE as a time, keyboard and accessible names, footer
  contrast 3.2-5.9:1, dBFS meter, host context menu); two cosmetic ones left, with
  reasons.

## The register (A-010)

C0-C7, set by the owner's listening; silent outside. `register_is_c0_to_c7` guards it.

## Open listening items

Everything measurement has taken as far as it can. One page plays the newest ones:

```bash
cmake --build build-release --target HorizonPadSoundTool
python3 tools/listening/make_session.py --baseline build-release/HorizonPadSoundTool \
        --candidate build-release/HorizonPadSoundTool --out build-listening \
        --only register_bottom_root,register_bottom_bloom,register_bottom_blend,register_top_root,register_top_clearing,bloom_movement
open build-listening/index.html
```

Each pairs the end key with the octave inside it (or Bloom at DETUNE 0% with 50%),
loudness-matched; the preference column records the verdict. The older items compare
v1 (`bf17ee8`) with `main` - recipe in the history below.

| From | Item | Question |
|---|---|---|
| Review 2026-10-17 R-1 | `register_bottom_root`, `register_bottom_bloom`, `register_bottom_blend` | C0 against C1: music, or a wobble with a pitch? (roughness at C0: Bloom 3.6, Clearing 1.6, Root 1.25) |
| Review 2026-10-17 R-2, R-3 | `register_top_root`, `register_top_clearing` | C7 against C6: Root loses its pitch above C5 (sounds an octave low?), Clearing is roughest at C7 |
| Review 2026-10-17 D-1 | `bloom_movement` | Bloom's measured movement halved since DETUNE's default went to 0%: lost life, or only beating? |
| A-009 | In the plugin | The pads at worship length now - right, or too abrupt? The in-betweens (Talwind, Sturmfront, Kupferglanz) release longer than the pads - keep? |
| Pass 2, 2026-10-07 | `blend_high`, the C2 open fifth, Root C1/C2 start, `detune_wide`, Daemmerlicht | Still open from the second pass |
| Presets, 2026-10-07 | In the plugin | Glasperle still glass; Silberpfad over a pad; the in-betweens speaking in 0.2-0.45 s; the first five |
| Carried | EX-001's 2 Hz DC blocker; G5's filter Q and Expanse's lowpass order; the shimmer blend 16 dB under the prototype; G1's linear interpolation on flanger and shimmer reads | Character questions with no complaint so far - heard implicitly in every pass, never asked by name |

## Exceptions on the books

| ID | Subject | Status |
|---|---|---|
| EX-001 | DC offset when the output limiter engages | CLOSED 2026-09-23 - fixed, marginal pass |
| EX-002 | Sternenzelt ~4.8 LU below the matched bank | OPEN - accepted; confirmed by ear 2026-10-03 |
| EX-003 | Expanse aliases at MIDI 96-108 | CLOSED 2026-10-07 - v2's top octave preferred by ear; v1's "not usable" |

# History

**The v1 against `main` session** (the older listening items, and the "recipe above"
the dated sections below refer to): compare v1 - `bf17ee8`, the signed-off sound
before sound-design v2 - with the current build.

```bash
git worktree add --detach ../hp-v1 bf17ee8
cmake -S ../hp-v1 -B ../hp-v1/build -G Ninja -DCMAKE_BUILD_TYPE=Release \
      -DFETCHCONTENT_SOURCE_DIR_JUCE="$PWD/build-release/_deps/juce-src"
cmake --build ../hp-v1/build --target HorizonPadSoundTool
python3 tools/listening/make_session.py --baseline ../hp-v1/build/HorizonPadSoundTool \
        --candidate build-release/HorizonPadSoundTool --out build-listening
open build-listening/index.html
```

## Sound-design v2 (branch `sound-design-v2`, 2026-09-26)

A second review found the gates green while the design itself had drifted, and
that a harness bug had invalidated the G5 numbers (corrected in
[`g5-null-test.md`](g5-null-test.md)). Findings:
[`dsp-review-2026-09-26.md`](dsp-review-2026-09-26.md); changes and measurements:
[`sound-design-v2.md`](sound-design-v2.md). On the branch at the time; merged into `main` as `2724152`:

- **G3/G5 reopen for v2** - measured against v1 with `tools/measure/descriptors.py`
  and `register.py` (baselines in `docs/baselines/`), 17/17 tests. The v2 sound
  deliberately diverges from the Faust prototype, which stays the frozen v1 reference.
- **G6 on v2** - Steinberg validator 47/47 and pluginval L10 re-run 2026-10-03
  (see G6 above); hosts still owed.
- **Listening debt** - ten items, listed in `sound-design-v2.md`, with a blind,
  loudness-matched A/B page generated by `tools/listening/make_session.py`.

## WIDTH and DETUNE macros (branch `sound-design-v2`, 2026-10-02)

Owner decisions in [`brief.md`](brief.md) A-006; design and measurements in
[`width-design-draft.md`](width-design-draft.md). Four per-pad WIDTH knobs became
one WIDTH macro with a fixed profile per pad, and DETUNE was added: ten host
parameters, 19/19 tests (two new, each shown failing with its feature disabled).
WIDTH 0 with DETUNE 50% nulls bit-exact against `05c3f44`; presets moved -0.01
to +0.29 LU. Baseline: `docs/baselines/descriptors-v2-macros.json`.

Listening debt added (items in `tools/listening/make_session.py`, baseline a
worktree of `05c3f44`):

| Item | Question |
|---|---|
| `preset_daemmerlicht`, `preset_lagerfeuer` | The staggered WIDTH profile against the same width on all four pads: clearer, or just narrower? |
| ~~`preset_sternenzelt`~~ | Heard 2026-10-03, owner: "Sternenzelt is ok as is" |
| `preset_klarheit` -> `preset_daemmerlicht` | Klarheit became an in-between preset on 2026-10-07; the WIDTH-profile question moved to Daemmerlicht, an unchanged pad |
| `root_chord` | Root at full WIDTH is now 60% of its spread: still enough stereo? |
| `detune_tight` | DETUNE 0%: tighter and cleaner, or static? |
| `detune_wide` | DETUNE 100%: lush, or seasick? Clearing responds least (its ensemble dominates its spread) - audible enough there? |
| GUI | Six macros in the MACROS card: 37 px knobs where the design had 42 px - still comfortable to grab? |

## 2026-10-03 - flaky test fixed, G6 re-run, real-time safety, translation battery

- **`voice_steal_declick` was flaky, not regressed.** It took the worst of six
  *unseeded* renders per side and failed about 2 runs in 10. Over seeds 1-30 the
  current build and `05c3f44` are indistinguishable (same three seeds high), so the
  WIDTH/DETUNE change did not cause it. Now seeded over twelve fixed seeds, comparing
  medians and worst cases; shown failing on a build with the declick removed
  (median steal -9.6 dB vs free -17.5 dB) and passing on HEAD.
- **G6 re-run on v2**: Steinberg validator 47/47 (SDK 3.8.1), pluginval L10 pass.
- **Rule 35**: `HORIZON_RTSAN` builds the sound tool with RealtimeSanitizer; all 19
  tests pass clean; `HORIZON_RTSAN_SELFTEST=1` aborts as it must.
- **Rule 34, translation battery** (`tools/measure/translation.py`, baseline
  `docs/baselines/translation-v2.json`, 18 presets x 3 seeds): mono sums lose -0.3 to
  -2.3 LU with no comb (worst band -4.4 dB); phone-speaker loss -0.4 to -3.0 LU, no
  outlier against the bank; PLR 11.1-12.9 dB, so normalised to -14 LUFS the peaks
  reach -0.9 to -2.4 dBTP (Bergecho -0.9 and Morgentau -1.0 brush AES TD1008's -1 dBTP,
  which only matters if someone raises the pad 4 dB with no limiter); AAC and MP3 at
  128 kbit/s decode at or below -1.0 dBTP with tail residuals near -30 dB. Failed
  only on stereo bass from the reverb - fixed the same day (item 2); after the fix
  every preset passes.
- **Battery redesign, disclosed:** the first version also failed presets whose true
  peak would exceed -1 dBTP if turned up to -14 LUFS with no limiter (after the fix:
  Sternenzelt and Steinerne Ruhe +0.4 dBTP). No delivery chain does that; AES TD1008's
  -1 dBTP applies to a mastered file before encoding. The codec row now masters each
  render to -1 dBTP and measures the decode (AAC overshoot <= 0.1 dB, MP3 no clipped
  samples), and the -14 LUFS peak is reported, not judged. PLR 11.4-13.6 dB.

## Listening pass 2, 2026-10-07 - results and what they changed

Same page and items against v1, candidate = the build with the first pass's changes
(`b57b808`); verdicts in `docs/listening/2026-10-07-verdicts.json`.

**The first pass's changes worked.** Root solo at WIDTH 100%, Bloom chord, the C2 close
triad and Sternenzelt at WIDTH 100% flipped to the candidate; on the Expanse chord and
Steinerne Ruhe X was no longer identified - the differences fell below audibility. The
blends, Clearing, Lagerfeuer and the register fixes stayed with the candidate; the note
on v1's top octave ("a lot going on in the lower register") is the aliasing EX-003 was
about.

| Still or newly v1 / open | What it changed (2026-10-07, second round) |
|---|---|
| C6 chord, default patch (v1, 2nd pass running) | Expanse's shimmer send rolls off 9 dB/octave above C5: at C6 Expanse measured +28 dB at 5-12 kHz against v1, the octave-up shimmer. Chord's excess at 5-12 kHz +5.5 -> +2.2 dB; Expanse at C7 unchanged. (Lowering Root's and Bloom's top tracking was tried first and made the chord *relatively* brighter - reverted.) Test `expanse_shimmer_recedes_at_top` |
| Root C1, C2: "first few seconds I don't like, then the tone changes and I like it - on both" | Below C3 Root's cutoff starts near its sustained brightness and sits 40% brighter (the breathing LFO's bright phase was the liked tone): early centroid at C2 99 -> 146 Hz; C3 and up unchanged. Test `root_bass_starts_open` |
| DETUNE 100%: "a bit seasick at max" | The slow pitch drift grows to 1.5x at DETUNE 100%, not 2x; the static detune keeps its full range |
| DETUNE 0% vs designed | **A tie.** This pass `blend_mid` and `detune_tight` were the same audio pair (the default patch is DETUNE 0%); one verdict went each way, X correct both times. The default stays at 0% |
| C2 open fifth (v1, 2nd time) | Not changed directly; Root's bass change above brightens it - heard again next pass |
| Daemmerlicht, WIDTH 55% (v1) | Not changed: one item, against Klarheit at WIDTH 50% preferring the candidate in pass 1 |
| Expanse bass line: "2 is C3 G2 C3" (candidate preferred) | Explained, not changed: register pinning folds each note towards C5 by pitch class, so C2-G2-C3 sounds C5-G4-C5 and the G lands below the Cs. Folding the whole played octave instead (C2-B2 up three octaves) would keep contour inside each octave and fold at C instead - proposal P6 below |

**Next pass**: the C6 chord, the C2 open fifth, Root C1/C2 (the start), DETUNE 100%,
Daemmerlicht - regenerate the page with the recipe above.

## Listening pass, 2026-10-06 - results and what they changed

The owner ran the blind A/B/X page (`tools/listening/make_session.py`): v1 (`bf17ee8`)
against the v2 build of 2026-10-04, loudness-matched, 19 items. **X was identified
correctly on all 18 items answered**, so every difference was audible.

| Item | Preferred | Note | What it changed (2026-10-07) |
|---|---|---|---|
| Default patch, C3-E4 | v2 | "v1 more flat, v2 lives more" | - |
| Root solo, WIDTH 100% | **v1** | | Root reaches full spread at 100% (stopped at 60%) |
| Clearing solo | v2 | | - |
| Expanse solo, C4-E4-G4 | **v1** | | Expanse back to v1's two-pole lowpass: +258 -> +139 cents vs v1 |
| Bloom solo | **v1** | | Shared pulse shallower: chord depth 44% -> ~29% |
| Low register, C2 fifth / triad | **v1** | | Key tracking below C4 eased: brightness gap -932/-910 -> -629/-525 cents vs v1, within roughness limits |
| Root solo C1, C2 | v2 | | kept (register work) |
| Expanse bass line C2-C3 | v2 | | kept (register pinning) |
| Clearing top C5 | v2 | | - |
| Bergecho | v2 | "v2 fuller, v1 clearer" | - |
| Steinerne Ruhe | **v1** | | Root wider at high WIDTH; preset -0.5 dB; heard again next pass |
| Expanse top C7 (EX-003) | v2 | "v1 not usable" | EX-003 closed |
| Default patch, C6 chord | **v1** | | **open** - one item, not changed (no stacking unheard changes) |
| Lagerfeuer | v2 | | - |
| Klarheit, WIDTH 50% | v2 | | the staggered profile at mid widths kept |
| Sternenzelt, WIDTH 100% | **v1** | | full spread at 100% for Root and Bloom |
| DETUNE 0% vs designed | **0%** | | default patch (Lagerfeuer, parameter default) at DETUNE 0% |
| DETUNE 100% vs designed | designed | X not answered | - |

Reading across the items: v2 wins in the blends and wherever it fixed a register
problem; v1 wins where v2 narrowed or darkened a single pad (width profiles, Expanse's
top end, low-chord tracking) and where Bloom's pulse was exposed. The changes target
exactly those, and each moved its item towards v1 by measurement (current minus v1,
per item: `abdiff` numbers in `docs/preset-curation-2026-09-23.md`, 2026-10-07 second
pass). Width at the default 40% was left alone: those verdicts conflict (Lagerfeuer
and the default patch, narrower, preferred v2; Steinerne Ruhe and Bloom solo, wider,
preferred v1).

Owner's notes from playing the presets: Glasperle "no longer piercing, but not quite
glass anymore" (moved halfway back, strongest partial -15.3 dB); Silberpfad "sits on
top of Lagerfeuer"; in-betweens "work well - the main place to be are harmonies";
first five good, but order each run from longest attack and release to shortest and
repeat (done: cycles, see `README.md`). The owner used the 2026-10-04 session (with
the Klarheit item), which compares the same DSP.

**Next listening pass** - regenerate the page (recipe above) and judge: Root solo and
Sternenzelt at WIDTH 100%; Expanse chord; Bloom chord; the C2 fifth and triad;
Steinerne Ruhe; the C6 chord (still open); the default patch at DETUNE 0%; Glasperle's
glass; and the new preset order when browsing.

## Worship envelopes (A-009, 2026-10-14)

Owner's listening test: the pads sounded cinematic. ATTACK/RELEASE now run from the old
fast floor (0%) to each layer's designed times (100%, was 50%) on one even curve, and
every pad preset has a third of its former attack and release (Root 0.64-1.19 s /
0.70-1.30 s); in-between and leads unchanged. Loudness unchanged; limiter still linear
in typical playing. New tests `attack_release_span`, `presets_have_worship_envelopes`
(24/24). Details in [`brief.md`](brief.md) A-009.

| Preset | Question |
|---|---|
| The pads (Lagerfeuer, Alpengluehen, Steinerne Ruhe, ...) | Worship length now - or still too slow / now too abrupt? |
| Talwind, Sturmfront, Kupferglanz | They now release longer than the pads (~1.1-1.5 s): keep, or shorten them too? |
| ATTACK/RELEASE knobs | Is the short end easier to set? Anything missing above 100%? |

## Presets: three families, 17 (2026-10-07)

Owner's verdict on the first leads: Funkenflug liked ("sounds very good"), Glasperle
"too bright fast", Bergquelle "hurts when holding" - both measured and dealt with
(`docs/preset-curation-2026-09-23.md`, 2026-10-07 section): Glasperle's dominant
octave partial -12.4 -> -16.6 dB; Bergquelle retired (Root's +4-5 dB swell on held
notes). The bank is now 9 pads, 5 in-between, 3 leads, the five most different first.
Low register, owner: "acceptably good - not the best choice for bass or sub-bass, but
still musical for that type of instrument" (closes v2 listening item 8).

Listening items - play them in the plugin:

| Preset | Question |
|---|---|
| Glasperle | Still glass, and no longer too bright on held high notes? |
| Silberpfad | Sits above a pad (try it over Lagerfeuer), or blurs into it? |
| Frostklang, Kupferglanz, Klarheit, Talwind, Sturmfront | In-between: does each still feel like itself, now speaking in 0.2-0.45 s? Release ~1 s - right, or too long for rhythm parts? |
| The first five | Lagerfeuer, Funkenflug, Alpengluehen, Frostklang, Talwind: different enough, and a good first impression? |

## Checked against two practitioner videos (2026-10-04) - proposals for the owner

Sage Audio, "Fixing the 3 WORST Sounds in Modern Music Production" and "Formant
Shifting is WAY More Useful Than People Think" (transcripts read; integrated into the
playbook as v3.3: I.1, I.5.1, I.7.1, E.2-E.4, E.8.6-E.10). What they mean for Horizon Pad:

**Measured: Horizon Pad is not "sterile".** The first video's complaint about stock
synths is identical repeats - same tuning, timing and decay every time. The same
four-note chord struck three times (product mode, unseeded) differs strike to strike
by a +3.0 dB residual (uncorrelated waveforms: random start phases and drift), up to
6.3 dB in 100 ms envelope windows and 640 cents in brightness, while the mean level
holds within 0.09 dB - which is what a pad should keep. Nothing to fix; the
measurement (`dspkit.repeat_variation`) is the playbook's new A.6 descriptor.

**Proposals - each changes the signed-off sound, so none is applied:**

| # | Idea | From | What it would change | How to judge it |
|---|---|---|---|---|
| P1 | ~~**Expanse as a choir with fixed formants.**~~ **Declined by the owner, 2026-10-04: "leave as is - it adds more movement across the register, I like it."** The key-tracked resonance stays. Expanse is called a choir but has no vowel formants: its one resonant band-pass tracks the note (0.7 above C4, 0.3 below), so its resonance - its apparent size - moves with every note. Real voices keep their formants fixed while the pitch moves. A small fixed vowel-formant bank (not key-tracked) after the stack would keep one "singer" across the keyboard | Formants video | Expanse's character across the range | Spectral-envelope peak vs note (flat = one singer); keyboard span test; blind A/B against `05c3f44`/HEAD |
| P2 | **Voice-card variance.** Small fixed per-voice-slot offsets (cutoff, envelope time, tuning) so a chord's notes are never quite identical - what analog polysynths do and their reissues expose as "vintage" | Playbook I.5.1, prompted by the stock-synth fix | Chord texture, very subtly | Chord-note variance; must stay inside level and tuning JNDs |
| P3 | **A quiet delay** in the FX chain to fill the gaps between chord changes, its feedback carrying the drift | Stock-synth fix, step 4 | Adds an effect; needs a macro or a fixed amount - brief amendment (editing stays shallow) | Gap energy between chords; mix-context listening |
| P4 | **Tape-style softening** (gentle HF roll-off, slight wow/flutter) on the output | Stock-synth fix, step 2 | The top end and stability of everything | Centroid and modulation spectrum; blind A/B |
| P6 | **Expanse pinning that keeps a bass line's contour.** Today each pinned note folds by pitch class towards C5, so C2-G2-C3 sounds C5-G4-C5 (the owner heard "C3 G2 C3"). Folding the whole played octave (C2-B2 up three octaves) keeps the contour inside each octave and folds only at C | Owner's note, listening pass 2 | Expanse below C4 | The `expanse_bassline` item; contour of C2-G2-C3 and of a stepwise line |
| P5 | **Early reflections before the Freeverb tail** for a believable room (the current send has 20 ms pre-delay, deliberately, and a late-tail-weighted algorithm) | "Realistic room first" | The reverb's front | Impulse response energy in 5-80 ms; listening |

Not applicable: formant de-essing, kick weight, noise-carrier air and vocal thickening
are mixing uses on recorded sources; Horizon Pad has no voice or drum input.
