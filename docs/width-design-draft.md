# WIDTH - design draft

**Status:** decided and built 2026-10-02 - see "Built" below. The draft that
led to it (2026-10-01, on `05c3f44`) follows unchanged after that section.
**Branch:** `sound-design-v2`.

## Built (2026-10-02)

Owner decisions, recorded in [`brief.md`](brief.md) A-006:

- **Option A2 is built, with WIDTH as macro 5.** One `widthMacro` parameter. Each
  pad's profile is a `widthProfile()` override in its layer header, with the
  starting values from the table below unchanged:

  | Pad | Opens at | Width at 100% |
  |---|---|---|
  | Root | 30% | 60% |
  | Clearing | 10% | 100% |
  | Expanse | 0% | 100% |
  | Bloom | 20% | 90% |

- **Macro 6 is DETUNE** (`detuneMacro`). It scales each stack's static detune
  and its baseline drift, 0.5x-2x on the `2^((v-0.5)*2)` curve, with 50% the
  designed sound. It eases out below C3 (the `unisonFor` register blend) and
  does not scale Root's saw edge and sub or the MOD wheel's extra drift. The
  drift is included because on Clearing, Expanse and Bloom it is most of the
  spread: their static detune is 0.3-1.6 cents, the drift about +/-7.
- **GUI**: the MACROS card holds six knobs in three rows (ATTACK/RELEASE,
  FILTER/REVERB, WIDTH/DETUNE) and the pad cards keep VOL only. Option G3 from
  the draft, chosen by the owner; the 2x2 card's 42 px knobs became 37 px.
- **Presets**: WIDTH = each preset's former Expanse width (Expanse's profile is
  the identity, so the widest pad stays where it was), DETUNE 50% everywhere.
- **No compatibility code**: the plugin is not used beyond testing (owner).
- A mix-pocket macro was proposed as macro 6 first, under the name BAND. It was
  rejected, partly for the name, which also reads as bandwidth. The playbook now
  has a naming rule (F.14.2, D.5.2).

Measured (seeded renders; commands in the session that built it):

| Check | Result |
|---|---|
| Null vs `05c3f44`, WIDTH 0 / DETUNE 50% (default patch, Sternenzelt, C1-C2 chord) | bit-exact |
| Null vs `05c3f44` with the old per-pad widths set to the profile's values: WIDTH 100% / 50% | bit-exact / -132.5 dBFS (decimal rounding of the inputs) |
| Preset loudness vs `descriptors-v2.json` | -0.01 to +0.29 LU; bank spread 2.97 -> 2.87 LU |
| Stereo at WIDTH 100% (corr / mono-sum loss) | Root 0.76 -> 0.91 / -0.55 -> -0.20 dB; Bloom 0.43 -> 0.53 / -1.47 -> -1.17 dB; Clearing, Expanse unchanged |
| DETUNE 0/50/100%: spread of one partial of C4, mean of 3 seeds | Root 3.6/5.9/10.3 c, Clearing 6.3/7.2/9.6, Expanse 2.6/5.2/10.0, Bloom 3.2/4.7/8.8 |
| DETUNE 0 or 100% vs 50%, loudness | within +/-0.7 LU on every pad |
| DETUNE 100%, C2 roughness (register.py) | Root 0.05 (unchanged), Clearing 0.35 -> 0.44, Bloom 0.31 -> 0.30; limits 0.4/1.2/1.0 |
| Tests | 19/19; `width_profile_staggered` and `detune_spreads_every_stack` both fail on a build with the profile ignored and DETUNE pinned to 1 |

Clearing is the pad DETUNE moves least, because its ensemble adds pitch
movement of its own. That is on the listening list in
`gate-status.md` (git `8a14117`), along with the profile itself.

Full descriptor baseline after the change: `docs/baselines/descriptors-v2-macros.json`.

---

## The problem

Horizon Pad has four WIDTH parameters, one per pad (`rootWidth` ... `bloomWidth`,
host params 9-12). The factory bank does not use them as four controls. Inside
each preset, all four pads get the same width, give or take 0.05:

| Preset | Root | Clearing | Expanse | Bloom | Spread |
|---|---|---|---|---|---|
| Lagerfeuer | 40 | 40 | 40 | 40 | 0 |
| Alpenglühen | 80 | 80 | 80 | 80 | 0 |
| Morgentau | 70 | 70 | 70 | 70 | 0 |
| Sternenzelt | 100 | 100 | 100 | 100 | 0 |
| Talwind | 85 | 85 | 85 | 85 | 0 |
| Mitternachtsblau | 45 | 45 | 50 | 40 | 10 |
| Bergecho | 100 | 100 | 100 | 90 | 10 |
| Steinerne Ruhe | 40 | 40 | 40 | 35 | 5 |
| Goldstaub | 85 | 85 | 95 | 75 | 20 |
| Tiefensog | 30 | 30 | 30 | 30 | 0 |
| Lichtnebel | 65 | 65 | 65 | 60 | 5 |
| Sturmfront | 95 | 95 | 90 | 100 | 10 |
| Dämmerlicht | 55 | 55 | 55 | 50 | 5 |
| Frostklang | 55 | 55 | 60 | 50 | 10 |
| Kupferglanz | 60 | 60 | 60 | 55 | 5 |
| Sternenstaub | 85 | 85 | 90 | 90 | 5 |
| Ruhepuls | 50 | 50 | 50 | 60 | 10 |
| Klarheit | 50 | 50 | 50 | 50 | 0 |

- 7 of 18 presets use one value for all four pads. Root and Clearing are equal in
  all 18. The largest difference inside any preset is 20 points (Goldstaub).
- The overall amount does change from preset to preset (30 to 100). The *shape*,
  meaning how wide each pad is compared with the others, never changes.
- The cause is on record. Commit `80fbd2a` (2026-09-17) split the old shared WIDTH
  macro into four knobs and copied each preset's old value to all four pads. The
  comment in `Presets.cpp` says this was a starting point and was never tuned
  per layer.
- A small inconsistency: the default patch uses 50/60/70/45, while Lagerfeuer,
  which the default volumes follow, uses 40 across all four.

So 4 of the 12 knobs carry one knob's worth of information. We pay for per-layer
width in GUI space, host parameters, preset dimensions and the brief's
"12-dimensional space", but the bank does not use it. The brief's target user is
"playing a set on Sunday, not designing a patch". That player will not balance four
widths by hand either.

## What width should do in this instrument

Width has no source of truth to follow. `four_pads.dsp` is mono throughout: width
was added during the port, first as a Haas tap and, since v2, as per-oscillator
spread. It is a free design choice, and it should follow each pad's musical role:

- **Root** (foundation) stays near the centre. The mono-bass rule already keeps
  the side channel empty below 140 Hz (`FxChain`), so Root's width only affects
  its upper partials. A wide foundation also fights the guitar and lead vocal,
  which sit in the centre in the target genre's mixes.
- **Expanse** (air, shimmer) is the widest pad. It is the pad the listener hears
  "around" the band.
- **Clearing** (ensemble) is wide by nature, and its ensemble keeps it stereo even
  at WIDTH 0.
- **Bloom** (motion) sits in between: wide enough to surround the listener, not
  so wide that its tremolo smears.

A preset really has two width choices: **how wide overall**, which is a per-preset
choice, and **the shape across the pads**, which mostly follows from the roles
above. That points to one knob with a fixed shape.

## Options

### A. One WIDTH macro with a fixed per-pad profile (recommended)

One host parameter, `width`, 0-100%. Each pad keeps a fixed width profile of its
own, set as constants in its layer header in the same way `kOscSpread` and the
fixed ADSR timings are. WIDTH becomes the fifth macro and works exactly like
ATTACK and RELEASE: one global control that acts on each layer's own fixed design
values.

There are two ways to map the knob to each pad's width:

- **A1, proportional:** `w_pad = W * max_pad`. This is easy to reason about. Every
  preset keeps the same shape at a different size, so it only half answers the
  complaint.
- **A2, staggered, opening from the top (preferred):** each pad starts widening at
  its own point on the knob, so the shape changes as the knob turns.
  `w_pad = max_pad * clamp((W - start_pad) / (1 - start_pad), 0, 1)`.
  At low settings only the air is open and the foundation stays in the centre
  ("close", Lagerfeuer). At high settings everything spreads ("vast",
  Sternenzelt). One number then produces different shapes, which is the variety
  the bank is missing.

Starting values for A2. The ordering is the design claim; the numbers are for the
ear to tune:

| | start | max | W 25% | W 50% | W 75% | W 100% |
|---|---|---|---|---|---|---|
| Root | 30% | 60% | 0 | 17 | 39 | 60 |
| Clearing | 10% | 100% | 17 | 44 | 72 | 100 |
| Expanse | 0% | 100% | 25 | 50 | 75 | 100 |
| Bloom | 20% | 90% | 6 | 34 | 62 | 90 |

What changes:

- **Parameters:** 12 become 9. Params 1-8 (volumes, then macros) do not change, so
  the Launchkey mapping still works. WIDTH becomes param 9.
- **Presets:** each of the 18 gets one W value, chosen by listening. The current
  values are only a rough guide, because A2 makes the lower pads narrower at the
  same number. WIDTH measures level-flat (-0.3 to 0.0 dB), so the bank should not
  need loudness-matching again. Confirm that with `descriptors.py --compare`.
- **GUI:** the pad cards lose their WIDTH sub-knob, which leaves the lower half of
  each card empty. Candidate layouts:
  - **G1:** the lower slot becomes a read-only width indicator: a small arc that
    shows that pad's current width from the profile. The grid stays balanced and
    the profile becomes visible.
  - **G2:** the WIDTH knob moves into the OUTPUT column, above the meter. Stereo
    image is a property of the output, so it belongs there.
  - **G3:** WIDTH becomes a fifth knob in MACROS. This breaks the 2x2 layout that
    lines up with VOL and WIDTH across the row. Not recommended.

  I recommend G2 + G1. The GUI is pixel-accurate to a design handoff, so this
  needs a design pass first, not an ad-hoc edit.
- **Brief:** `core_controls` says "WIDTH x4" (and still "Haas spread"), and
  `out_of_scope` says a preset is a point in 12 dimensions. Both need an
  owner-confirmed amendment (A-006), not an edit to the YAML. `must_do`'s "No
  per-layer tone block beyond WIDTH" would become "no per-layer tone block",
  which makes the instrument framing stronger.

### B. One WIDTH knob plus hidden per-preset shapes

Presets store a per-pad width shape that the user cannot see, and the knob scales
it. This gives the most variety, but it is hidden state. The knob position no
longer describes the sound, a user cannot rebuild a preset from what is on
screen, and the rule that a preset is a point in the visible knob space breaks.
**Not recommended.**

### C. Keep four knobs and voice the presets properly

No change to code, parameters, GUI or brief. A preset pass gives every preset a
real shape. For example, Lagerfeuer 20/35/60/40 and Sternenzelt 60/100/100/90.
This is the cheapest option and leaves the handoff GUI untouched. The cost stays:
four of the twelve knobs are spent on width, and "a bit wider" means turning four
knobs. This is the right choice if the GUI must stay as it is.

### D. WIDTH plus SPREAD (two knobs)

An amount knob and a shape knob ("narrow bass" to "even"). It offers more control
than A but adds a concept the target user does not need. **Not recommended.**

| | A2 | B | C | D |
|---|---|---|---|---|
| Host params | 9 | 9 | 12 | 10 |
| Shape varies per preset | yes, via the knob | yes, hidden | yes | yes |
| Knobs to turn for "wider" | 1 | 1 | 4 | 1 |
| GUI change | yes | yes | no | yes |
| Brief amendment | yes | yes | no | yes |
| Session/automation compatibility | breaks params 9-12 | breaks | none | breaks |

## Audition before building

A2 is only a mapping onto parameters that exist today. It can be heard **now**,
with no code change, by setting the four current widths to the profile values:

```bash
./build-release/HorizonPadSoundTool --preset=Lagerfeuer \
  --param=root-width=0.09 --param=clearing-width=0.33 \
  --param=expanse-width=0.40 --param=bloom-width=0.23 --out=lagerfeuer_a2.wav
```

Render three or four presets both ways and compare them in
`tools/listening/make_session.py` before committing to anything. If A2 does not
sound better than the uniform width, option C is the fallback, and those renders
are its first draft.

## If A is chosen: implementation sketch

- `Presets.h/.cpp`: `ParamID::width` replaces the four IDs, and `Preset::widths`
  becomes `float width`, with 18 values.
- Each layer header: `kWidthStart`, `kWidthMax`, documented in the class comment
  as design intent. `LayerBase::setWidth()` takes the global W and applies the
  layer's profile. Everything after that (`panGains`, Expanse's shimmer reverb
  width) is unchanged.
- `PluginProcessor`: parameter layout, cached pointer, A/B snapshot, and state
  loading. Old sessions and `UserPresets.xml` carry `w0..w3`, so map them to a W
  (the mean is good enough) when `width` is missing.
- GUI: `PadKnob`, `OutputMeter` column, `FooterBar` ("12 MACROS").
- Sound tool: `--param=width=`. Tests: the six tests that set a per-layer width
  (`shimmer_octave_present`, `width_is_rate_invariant`, the three presence tests,
  `width_mono_safe_and_level_flat`) switch to `width`. Add `width_profile_ordered`: at W 100%, Root's side/mid ratio
  is below Bloom's, Bloom's below Clearing's and Expanse's, and each pad's width
  rises steadily with W. `width_mono_safe_and_level_flat` keeps its assertions.
- README parameter table, CLAUDE.md ("Twelve host-automatable parameters", "4
  widths").
- `PluginProcessor`, `LayerBase`, `Presets.h` and `gui/` are outside the
  sound-designer agent's permission, so this is main-session work.

**Compatibility:** removing `rootWidth` ... `bloomWidth` breaks any host automation
lanes written against them. Whether that matters is the same open question as in
`sound-design-v2.md`: is v1 in users' hands? If not, this can land together with
v2's sound change, with no version flag.

## Decisions for the owner

1. One knob (A) or four voiced knobs (C)?
2. If A: staggered (A2) or proportional (A1) mapping, after the audition above.
3. Where the knob goes: G2 + G1, or another layout from a design pass.
4. Is v1 released? This decides whether old sessions need migration.
5. Brief amendment A-006 (WIDTH x4 to x1, 12 to 9 dimensions, and the stale
   "Haas spread" wording).
