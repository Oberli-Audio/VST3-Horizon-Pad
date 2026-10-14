# Factory preset curation, 30 -> 18

**Date:** 2026-09-23 · **Gate:** G7 · **Rule:** playbook 5.3 — "8-20 presets that
span the instrument's range, not 40 variations of one patch."

Curated on **character**, not level. Loudness matching is a separate pass; the bank
still spans 11.5 LU after this change.

## Method

Each preset was rendered (Cmaj7 at C3, 2 renders averaged) and placed in a
nine-dimensional character space, each axis normalised by its own standard
deviation across the 30 so no axis dominates:

- layer **balance** — the four volumes normalised to sum 1, i.e. which pad leads
- mean **width**
- mean **attack/release**
- **FILTER** and **REVERB** macros
- measured **spectral centroid** (log Hz)

Absolute level is deliberately *not* an axis: it is about to be matched away, so
scoring on it would have preserved presets whose only distinguishing feature was
being quiet.

## Result

| | closest pair | mean nearest-neighbour |
|---|---|---|
| Original 30 | 0.79 | 1.39 |
| Curated 18 | **1.53** | **2.05** |

Every remaining preset is now at least 1.53 away from its nearest neighbour —
further apart than *any* of the twelve pairs that were cut.

## The 18, by the job they do in a set

| Role | Presets |
|---|---|
| Dark and close | Tiefensog, Mitternachtsblau, Lagerfeuer |
| Slow swell | Steinerne Ruhe |
| Gentle motion | Ruhepuls, Talwind |
| Neutral / mix-friendly | Klarheit, Dämmerlicht, Lichtnebel |
| Ensemble-led | Kupferglanz, Alpenglühen |
| Bright and airy | Morgentau, Goldstaub, Sternenstaub, Sternenzelt |
| Bright but dry and close | Frostklang |
| Big and cinematic | Bergecho, Sturmfront |

Spectral centroid across the kept bank runs 380 Hz to 1219 Hz with no gap wider
than ~160 Hz.

## The 12 cut, and what covers them now

| Cut | Covered by | Distance | Why |
|---|---|---|---|
| Kristallbach | Goldstaub | 0.94 | Same bright Expanse-led patch |
| Gletscherklang | Goldstaub | 1.12 | Same, third copy of it |
| Nachtreise | Ruhepuls | 1.15 | Same slow Bloom-led patch — see the judgment call below |
| Feuerglut | Tiefensog | 1.22 | Lagerfeuer with a slower envelope |
| Blütenwind | Sternenstaub | 1.27 | Same wide bright Bloom/Expanse blend |
| Sonnenaufgang | Alpenglühen | 1.29 | Same wide bright Clearing/Expanse blend |
| Federleicht | Klarheit | 1.30 | Its identity was "quiet", which matching removes |
| Waldlicht | Lichtnebel | 1.31 | Balance within 0.06, same width/filter/reverb |
| Ozeanweite | Bergecho | 1.33 | Both the huge-reverb, full-width patch |
| Windharfe | Talwind | 1.43 | Sits between Talwind and Sternenstaub |
| Nebelmeer | Alpenglühen | 1.49 | Haze already covered by Lichtnebel and Morgentau |
| Schattental | Tiefensog | 1.64 | Same dark narrow patch, slightly more reverb |

## The one judgment call

**Nachtreise vs Ruhepuls** are the closest surviving pair in the original bank
(1.15) and only one could stay. By name and description they are different moods —
"a slow journey through the night" against "a slow resting pulse". By measurement
they are nearly the same patch. Keeping Nachtreise would have left the curated bank
with a closest pair of 1.15, tighter than five pairs that were cut, so Ruhepuls
stayed and Lichtnebel was brought in instead to hold the mid-bright ground.

If the "night" mood matters more than the geometry, swapping Lichtnebel back out
for Nachtreise is a one-line change — it just costs the separation number.

## Compatibility

Cutting presets shifts factory program indices. This does **not** change how an
existing project sounds: `setStateInformation` restores all twelve parameter values
from the saved APVTS state and never re-applies a preset, and the restored program
index is clamped to the new range. The only visible effect is that a project saved
with v1.0.0 may show a different preset *name* as selected than it did before.

## Addendum: leads (2026-10-06) - superseded by the 2026-10-07 reshape below

The owner asked for presets that also cover **lead styles, with fast or medium-fast
attack and release**. The 18 pads all sit at ATTACK and RELEASE 25-85% - the fastest,
Frostklang, still takes ~1.6 s to rise on Root - so four leads were added rather than
existing pads changed. The bank is now 22, above the playbook's 8-20 guideline; the
pads keep their curation, and the leads are a second job the bank now does.

**How fast each pad can be.** ATTACK and RELEASE below 15% fall exponentially to 1%
of each layer's designed time. Measured solo at 0% (C5, no reverb), onset to -3 dB of
the level at 0.3 s: Root 14 ms, Expanse 12 ms, Bloom 16 ms, Clearing 60 ms (its saw
stack starts slower); releases 16-24 ms to -20 dB. Expanse is 9 dB quieter than Root
at the same volume.

| # | Preset | Style | Leading layer | ATTACK / RELEASE | Onset C4 / C5 / C6 | Release to -20 dB (with the room) |
|---|---|---|---|---|---|---|
| 19 | Funkenflug | Bright saw lead | Clearing, DETUNE 65% | 0% / 5% | 118 / 74 / 22 ms | 0.2-0.4 s |
| 20 | Glasperle | Glassy bell lead, plucks then sings | Expanse | 0% / 10% | 14 / 18 / 20 ms | ~0.5-0.6 s |
| 21 | Bergquelle | Round, pure lead | Root + Expanse octave, DETUNE 20% | 3% / 8% | 30 / 26 / 40 ms | ~0.4-0.6 s |
| 22 | Silberpfad | Soft melodic lead over pads | all four, Bloom's motion | 7% / 10% | 72 / 164 / 118 ms | ~0.4 s |

Leads share narrower WIDTH (15-35%) so a line stays centred, REVERB 20-30%, and
brighter FILTER (45-70%). **Loudness**: matched to the pad bank's median, -16.9 LUFS,
with the bank's own method (C3-G3-C4-E4, last 3 s of an 8 s hold, four seeds) - all
four land at -16.92 to -16.93. Glasperle could not reach it on Expanse alone (full
volume tops out at -18.9 LUFS, the cause behind Sternenzelt's EX-002), so Root and
Clearing carry part of it under the glass instead of a second exception.

**Checks**: all 22 presets pass `all_presets_safe`; pluginval L10, the Steinberg
validator and the RTSan suite pass; the translation battery passes on all four (mono
loss -0.2 to -1.0 LU, bass mono, codec decodes at or below -0.9 dBTP from a -1 dBTP
master). Calibration script: kept out of the repo; the numbers above reproduce with
`tools/measure/descriptors.py --presets` and `tools/measure/translation.py`.

**Not available, by design**: mono mode and glide (the instrument is polyphonic;
out of scope), and delayed vibrato (the MOD wheel is the only modulation gesture).
Listening items: in the gate-status listening list.

## 2026-10-07: 17 presets in three families

**Owner feedback on the leads** (after playing them): Funkenflug "nice ... sounds very
good"; Glasperle "starts ok, but gets too bright fast"; Bergquelle "hurts when
holding"; and generally, "played in lower register [the plugin] sounds acceptably
good - not the best choice for bass or sub-bass, but still musical for that type of
instrument". Asked for: 17 presets, some pads kept, some changed to fill the gap
between pads and leads, the most diverse five first with Funkenflug among them, the
rest as variants.

**What the complaints were, measured.** Neither preset grew brighter on a held note
(their spectral centroids fall), so brightness growth was not it:

- *Glasperle* - on held upper notes its sound became dominated by one near-pure
  partial, Expanse's octave-up fundamental at 1.5-2.6 kHz, where the ear is most
  sensitive: at G5 it rose from -13 to **-5 dB of the whole sound** within 3 s as
  everything around it decayed. Funkenflug and Silberpfad, judged fine, peak at about
  -19 dB. Lowering FILTER made it worse (-10.1 dB: it removed the partials around the
  tone). Fixed by *surrounding* the tone: Expanse 0.88 -> 0.50, Bloom and Clearing up,
  DETUNE 60% so Expanse's three triangles beat instead of fusing, FILTER 60%: worst
  partial **-16.6 dB**, no held-note swell. Onset unchanged (15-25 ms).
- *Bergquelle* - held notes **swelled +4 to +5 dB within 2-3 s**: Root's breathing
  filter, the layer the preset was built on (the swell stays with Expanse removed).
  A Root-led pure lead cannot avoid it, so Bergquelle was retired rather than patched.

**Retired (5)**, by nearest-neighbour distance in the curation space (layer balance,
width, attack, release, filter, reverb, measured centroid) and by role:

| Retired | Nearest | Distance | Why |
|---|---|---|---|
| Lichtnebel | Morgentau | 1.60 | The hub of the bank's tightest cluster - nearest neighbour of five other presets |
| Sternenstaub | Goldstaub | 1.65 | Same bright, wide, Expanse/Bloom patch |
| Morgentau | Lichtnebel | 1.60 | Bright-airy is still covered by Goldstaub, Sternenzelt and Alpengluehen |
| Tiefensog | Lagerfeuer | 1.81 | Dark and close is covered by Lagerfeuer and Mitternachtsblau |
| Bergquelle | - | - | Root's swell on held notes (above) |

**Converted to IN-BETWEEN (5)**: the pads already leaning fast or present - Frostklang,
Kupferglanz, Klarheit, Talwind, Sturmfront. Same layer balance, ATTACK 8-12% and
RELEASE 13-16% (the macro's fast zone), descriptions updated:

| Preset | Onset C4 / C5 | Release (with the room) | Worst 1.5-6 kHz partial | Held-note swell |
|---|---|---|---|---|
| Frostklang | 245 / 160 ms | 1.1 / 0.9 s | -16.6 dB | +0.8 dB |
| Kupferglanz | 290 / 195 ms | 1.2 / 1.1 s | -17.7 dB | +0.1 dB |
| Klarheit | 245 / 165 ms | 1.1 / 0.8 s | -19.4 dB | +1.5 dB |
| Talwind | 335 / 255 ms | 1.4 / 1.2 s | -17.1 dB | +2.3 dB |
| Sturmfront | 450 / 300 ms | 1.4 / 1.4 s | -16.9 dB | +0.6 dB |

So the families separate cleanly: leads onset 15-165 ms and release 0.2-0.6 s,
in-between 160-450 ms and 0.8-1.4 s, pads from ~1.5 s up.

**Kept as is (9 pads)**: Lagerfeuer, Mitternachtsblau, Steinerne Ruhe, Ruhepuls,
Daemmerlicht, Alpengluehen, Goldstaub, Sternenzelt, Bergecho. **Leads (3)**: Funkenflug,
Glasperle (fixed), Silberpfad.

**Order.** Lagerfeuer stays first - it is the plugin's default sound (the parameter
defaults equal it). Funkenflug is fixed in the top five (owner). The other three were
chosen to maximise the smallest distance among the five, with every family present and
no family more than twice; unconstrained, the search picked two vast reverb pads
(Sternenzelt and Bergecho), different in numbers but close relatives by ear. Result,
smallest distance among the five 3.29 (unconstrained 3.36):

1. Lagerfeuer (pad) - 2. Funkenflug (lead) - 3. Alpengluehen (pad) - 4. Frostklang
(in-between) - 5. Talwind (in-between)

Every other preset follows as a variant of the first-five sound it is nearest to,
nearest first: Lagerfeuer -> Steinerne Ruhe, Mitternachtsblau; Funkenflug ->
Kupferglanz; Alpengluehen -> Daemmerlicht, Bergecho, Sternenzelt; Frostklang ->
Glasperle, Klarheit, Silberpfad, Goldstaub; Talwind -> Sturmfront, Ruhepuls.

**Loudness** (`descriptors.py --presets`, four seeds): median -16.89 LUFS; 16 of 17
within +/-0.9 LU (-17.24 to -16.02); Sternenzelt -18.90 (EX-002, accepted by ear on
2026-10-03).

## 2026-10-07, second pass: listening results, cycles, Glasperle

**From the blind A/B** (19 items, X correct on all 18 answered; full table in
`gate-status.md`; verdicts in `docs/listening/2026-10-06-verdicts.json`). Measured with
`tools/listening/compare_items.py` (current minus v1, after the session's -20 LUFS
match), the v1-preferred items before and after this pass's DSP changes:

| Item (owner preferred v1) | Before | After | Change |
|---|---|---|---|
| Root solo, WIDTH 100% - width 150-500 Hz / 2-5 kHz | -12.8 / -5.6 dB | -9.4 / -5.5 dB | Root and Bloom reach full spread at WIDTH 100% |
| Expanse chord - brightness | +258 c | +147 c | Expanse back to v1's two-pole lowpass and level |
| Bloom chord - movement | +0.9 dB | -0.4 dB | shared pulse +/-0.35 -> +/-0.20 (chord depth ~29%) |
| C2 fifth / triad - brightness | -932 / -910 c | -480 / -559 c | tracking below C4: Root, Bloom 0.5; Clearing 0.75 (roughness limit) |
| C6 chord - top (5-12 kHz) | +5.6 dB | +5.5 dB | not changed - one item, open for the next pass |

The items preferred on v2 kept their character (blends, Clearing, the register fixes).
Default patch: DETUNE 0% (preferred over the designed 50%).

**Order: repeating cycles.** Owner: first five good, but each run from the longest
attack and release to the shortest, repeating from #6, so a slow pad is never followed
straight by a lead. Every cycle goes pads -> in-between -> lead:

| Cycle | Presets (ATTACK/RELEASE) |
|---|---|
| 1-5 | Alpengluehen (60/60), Lagerfeuer (40/40), Talwind (11/15), Frostklang (9/13), Funkenflug (0/5) |
| 6-9 | Steinerne Ruhe (85/85), Bergecho (70/80), Kupferglanz (10/14), Silberpfad (7/10) |
| 10-13 | Mitternachtsblau (80/85), Ruhepuls (60/65), Sturmfront (12/16), Glasperle (0/10) |
| 14-17 | Sternenzelt (75/75), Daemmerlicht (55/60), Goldstaub (45/50), Klarheit (8/13) |

The plugin opens on Lagerfeuer (#2): the parameter defaults are its values, and the
processor now selects the factory preset the defaults equal, so the preset bar names
the sound heard (test `startup_program_matches_defaults`, shown failing without it).

**Glasperle, second pass.** Owner: "no longer piercing, but not quite glass anymore".
Halfway back: Expanse 0.50 -> 0.72 of the mix, FILTER 60 -> 72%; strongest 1.5-6 kHz
partial -15.3 dB (piercing was -12.4, first fix -16.6); onset 15-25 ms; loudness
matched (-16.94 LUFS). **Silberpfad** "sits on top of Lagerfeuer"; the **in-betweens**
"work well - the main place to be are harmonies".

**Loudness after the DSP changes** (`descriptors.py --presets`): median -16.90 LUFS;
Steinerne Ruhe trimmed 0.5 dB (it measured +1.0 LU); Sternenzelt -1.7 LU (EX-002).


## 2026-10-14: worship envelopes (brief A-009)

Owner's listening test: most presets sounded cinematic. ATTACK/RELEASE now run on one
exponential curve from 1% (0%) to 100% (100%) of each layer's designed times - the
designed envelope moved from the knob's 50% to its 100% - and the stored values were
re-expressed on it. Pads: a third of their former times. In-between and leads: the
same times as before (renders against the previous build: residual -69 to -119 dB).

| Preset | Family | ATTACK / RELEASE | Root attack / release (dry) | Before |
|---|---|---|---|---|
| Alpengluehen | pad | 79.2 / 79.2% | 0.84 / 0.92 s | 2.53 / 2.76 s |
| Lagerfeuer | pad | 73.1 / 73.1% | 0.64 / 0.70 s | 1.92 / 2.09 s |
| Steinerne Ruhe | pad | 86.7 / 86.7% | 1.19 / 1.30 s | 3.57 / 3.90 s |
| Bergecho | pad | 82.2 / 85.2% | 0.97 / 1.21 s | 2.90 / 3.64 s |
| Mitternachtsblau | pad | 85.2 / 86.7% | 1.11 / 1.30 s | 3.33 / 3.90 s |
| Ruhepuls | pad | 79.2 / 80.7% | 0.84 / 0.98 s | 2.53 / 2.95 s |
| Sternenzelt | pad | 83.7 / 83.7% | 1.04 / 1.13 s | 3.11 / 3.39 s |
| Daemmerlicht | pad | 77.7 / 79.2% | 0.79 / 0.92 s | 2.36 / 2.76 s |
| Goldstaub | pad | 74.6 / 76.1% | 0.68 / 0.80 s | 2.05 / 2.40 s |
| Talwind, Frostklang, Kupferglanz, Sturmfront, Klarheit | in-between | 47.7-71.6 / 77.5-89.8% | unchanged (0.20-0.59 / 0.85-1.50 s) | |
| Funkenflug, Silberpfad, Glasperle | lead | 0-41.8 / 29.8-59.6% | unchanged | |

Other layers scale alike (designed attack: Clearing and Expanse 2.6 s, Bloom 1.2 s).
Blend measured dry on the open chord: 10-90% swell 0.36-0.92 s on the pads (was
1.7-2.5 s), fall to -20 dB 0.30-1.03 s (was 0.9-2.7 s).

**Loudness** (`descriptors.py --presets`, four seeds): every preset within 0.01 LU of
the previous build, median -16.88 LUFS - the measured window is the sustained level,
which the envelope times do not touch. No volume changed. **Peaks**, typical playing
(three voicings, default velocity, four seeds): loudest preset -2.18 dBTP (Glasperle,
unchanged), every pad at or below -3.18 dBTP; some pads rise up to 1.3 dB (their
layers' attack peaks now fall closer together, decay times are not scaled), others
fall up to 0.6 dB. The limiter stays linear.
