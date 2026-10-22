# Exception log

Playbook 12.5: a failed measurement is not negotiable by the person who wrote the
code. Either fix it, or record a written, dated, named exception stating **what
failed, by how much, why shipping anyway is acceptable, and what would make it
unacceptable.**

Entries are append-only. Closing one means fixing the underlying failure, not
deleting the entry.

---

## EX-001 - DC offset above the -80 dBFS target when the output limiter engages

- **Date:** 2026-09-23
- **Raised by:** Claude, during the G4 output-stage re-stage
- **Rule / target:** Invariant #6 (DC blocker after asymmetric nonlinearities) and
  playbook 4.4's DC offset target of < -80 dBFS
- **Status:** CLOSED 2026-09-23 - fixed, see "Resolution" below

### What failed, and by how much

The re-staged output limiter ([PluginProcessor.cpp:735](../Source/PluginProcessor.cpp))
is odd-symmetric, but the summed four-layer signal is not, so when the limiter
engages it shaves asymmetric peaks asymmetrically and leaves a DC residue.

Measured with `HorizonPadSoundTool` (Release), 8 notes at velocity 1.0, all four
volumes, all four widths, FILTER and REVERB at 100%, 96 kHz, block 13:

| Volumes | Peak | DC offset | Limiter |
|---|---|---|---|
| 1.0 | -0.04 dBFS | **-92.4 dBFS** | engaged |
| 0.5 | -2.24 dBFS | -182.8 dBFS | at the knee |
| 0.25 | -6.93 dBFS | -128.2 dBFS | not engaged |

Across render lengths at volumes 1.0 the figure wanders between **-70.2 and
-92.3 dBFS** - it does not average away with a longer window, so it is a real
residue and not a measurement artefact. The worst observed, -70.2 dBFS, is
**9.8 dB above** the -80 dBFS target.

### Why shipping this way is acceptable for now

- It requires all twelve parameters at or near maximum with eight voices sounding.
  No factory preset engages the limiter at all: across 63 renders of the seven
  loudest presets in three voicings, the worst true peak was -2.64 dBTP, 0.7 dB
  below the knee.
- At -70 to -92 dBFS the offset is inaudible and consumes no meaningful headroom.
- The obvious fix - a DC blocker on the output - is not free: this instrument has a
  sub-oscillator an octave below the played note, so at MIDI 21 there is real
  musical content at 13.75 Hz. A highpass placed to remove DC sits close enough to
  that to deserve a listening pass rather than a blind patch.

### What would make it unacceptable

- Any factory preset reaching the limiter knee in normal playing (re-check after
  the preset loudness-matching pass - that pass raises quiet presets).
- The offset exceeding -60 dBFS under any setting.
- A user report of a click on host mute/unmute or on bounce boundaries, which is how
  output DC actually becomes audible.
- Stacking instances: DC sums coherently across instances, so a worship rig with
  four of them turns -70 dBFS into -58 dBFS.

### Why it was fixed rather than carried

The output gain was raised a further 2.5 dB when the factory bank was
loudness-matched later the same day, which drove the limiter harder and took the
offset to **-58.6 dBFS at 96 kHz** - past this entry's own stated trigger of
"exceeding -60 dBFS under any setting". The exception had invalidated itself, so
12.5 left only one option: fix it.

### Resolution

A one-pole DC blocker at 2 Hz, sample-rate aware, placed after the limiter because
the limiter is what creates the offset. State lives per channel and is cleared in
both `prepareToPlay` and `releaseResources` (invariant #12).

**The first attempt made things worse and is worth recording.** With the blocker
alone, DC fell to -147 dBFS but peaks rose to **+0.26 to +0.52 dBFS** and the
renders reported true clipping. The blocker subtracts a slowly-varying *local* mean,
so on asymmetric material it pushes a sample the limiter had just placed at the
ceiling back over it - the fix had broken the one guarantee the output stage exists
to make. A hard clamp at kCeiling was added as the final step.

Measured after the full chain (limiter -> DC blocker -> clamp), same pathological
extreme as the table above. **Six runs per configuration**: this metric swings
20-40 dB run to run, because each voice seeds its oscillator phase from system
entropy and the waveform's asymmetry - the thing the limiter acts on - changes with
it. A single render of it is not a result.

| Sample rate / block | Peak | DC worst of 6 | DC median | Clipping |
|---|---|---|---|---|
| 48 kHz / 512 | -0.04 dBFS | -87.1 dBFS | -92.3 dBFS | none |
| 96 kHz / 13 | -0.04 dBFS | **-80.3 dBFS** | -89.6 dBFS | none |

**This is a pass, but a marginal one - state it that way.** The worst observed run
sits at -80.3 dBFS against a -80 dBFS target, i.e. on the line, not inside it. What
the fix bought is a 22 dB improvement on the worst case (-58.6 -> -80.3 dBFS) and
the removal of the trigger condition that invalidated the original exception.

The clamp is why it is not better than that: it reintroduces a little DC of its own,
because asymmetric clipping does. Moving the blocker's corner up would buy a few
more dB (roughly 6 dB at 4 Hz) at a cost of -0.35 dB on the deepest sub instead of
-0.09 dB. That trade was not taken: the residue only exists at the single most
pathological setting in the product, -80 dBFS is inaudible, and playbook 7 is
explicit that the last 20-40 dB of a measurement is where effort stops paying.

On any factory preset or the default patch the figure is -140 dBFS or lower, because
none of them reach the limiter at all.

**Still owed:** a listening pass on the lowest octave. The blocker is -0.09 dB at
13.75 Hz and -0.04 dB at 20 Hz, so it should be inaudible, but it is a filter in the
output path of an instrument with a sub-oscillator and that claim is calculated, not
heard.

---

## EX-002 - Sternenzelt sits ~4.8 LU below the loudness-matched bank

- **Date:** 2026-09-23
- **Raised by:** Claude, during the G7 preset loudness-matching pass
- **Rule / target:** Playbook 5.3 - "every preset checked at the same output
  loudness"; brief target of +/-1 LU
- **Status:** OPEN - accepted for now, one-line fix available if the owner prefers it

### What failed, and by how much

17 of the 18 factory presets match to **-18.0 LUFS within +/-1 LU** (measured
spread 1.04 LU across three voicings spanning the worship-keys register).
**Sternenzelt lands at about -22.8 LUFS, roughly 4.8 LU below the rest.**

That figure needs a qualifier: Sternenzelt is also the least repeatable preset in
the bank. Over 4 renders per voicing its per-render standard deviation reaches
1.4 LU, against 0.2-0.8 LU for a stable preset like Klarheit or Morgentau, and its
loudness varies 3.9 LU across the three voicings against ~1.1 LU for those two. A
single measurement of it can land anywhere in a ~3 LU window, so any number quoted
for this preset is a mean, not a value.

It cannot be raised. Preset loudness is set by the four pad volumes, which are
capped at 1.0, and Sternenzelt is quiet precisely because one layer does nearly all
the work: its balance is 0.10 / 0.10 / 0.62 / 0.17, with Expanse already at 0.90 of
full. That left **+0.92 dB** of available gain before clamping, against the ~4.8 dB
it needed. The volumes are now scaled to that maximum - Expanse sits at 1.000 - so
there is nothing left to give.

### Why shipping this way is acceptable

The alternative was measured, not assumed. Forcing Sternenzelt into the match means
making it the target, which drags every other preset down by a further **8.3 dB**,
with volume scale factors reaching x0.38. That trades one quiet preset for a quiet
instrument.

Sternenzelt is also the patch where this matters least: "Vast and celestial -
Expanse fills the whole sky" is an atmospheric wash, not a preset anyone plays a
verse on. The instability above is itself part of the reason: so little of it is
fundamental that "how loud is Sternenzelt" does not have one answer to match to.

### What would make it unacceptable

- A second preset needing the same exemption - two is a pattern, and the fix then
  belongs in the gain structure rather than in an exception.
- User reports of reaching for the volume knob every time they load it.
- Any future preset whose character depends on a single layer near 1.0, which would
  hit the same ceiling.

### The fix, if it is preferred

Rebalance Sternenzelt: raise Root, Clearing and Bloom enough to make up the 5.3 dB
while keeping Expanse at 1.0. That reaches the target, and costs some of the purity
the preset is named for - Expanse would fall from 62% of the mix to roughly half. It
would also steady the measurement, since more of the mix would be fundamental. It is a one-line change to the preset table plus a listening pass, and it is
the owner's call because it is a character decision, not a level one.

---

### Owner listening, 2026-10-03

The owner listened to Sternenzelt on the `sound-design-v2` branch (with the WIDTH
macro's staggered profile) and decided: **"Sternenzelt is ok as is."** No
rebalance into the loudness match; the exception stays accepted, now on the
owner's ear rather than on the calculation. The trigger above still applies.

### Remeasured, with a numeric trigger (2026-10-22, Round 14 S.11)

Since the 2026-10-07 rematch Sternenzelt sits **1.94 LU below the bank median**
(-18.07 against -16.13 LUFS; median of 4 seeds, chord 48-55-60-64-67, 8 s, gate 6 s),
the only preset outside +/-1 LU; the next lowest is Goldstaub at -0.67 LU.

- **Unacceptable if:** Sternenzelt falls more than 3.0 LU below the bank median, or a
  second preset leaves +/-1 LU, on that measurement.

Friends: Sternenzelt plays about 2 dB quieter than the other presets.

## EX-003 - Expanse aliases at the top two octaves

- **Date:** 2026-09-24
- **Raised by:** Claude, during the G3 alias measurement
- **Rule / target:** Brief `quality_targets.alias_floor_db`
- **Status:** OPEN - accepted, with a named fix if a listening pass disagrees

### What failed, and by how much

Measured by rendering the same seeded note at 48 kHz and at 192 kHz and
comparing spectra below 20 kHz at matched FFT bin width (see
[`g3-measurements.md`](g3-measurements.md) for why the brief's own ASR/NMR
metric does not work on this instrument).

Expanse, relative to its own peak:

| MIDI | Layer peak | Loudest alias | Absolute |
|---|---|---|---|
| 84 and below | - | -68 dB or better | below -93 dBFS |
| 96 | -41.0 dBFS | **-27.1 dB** | -68.1 dBFS |
| 108 | -56.6 dBFS | **-17.5 dB** | -74.1 dBFS |

The cause is known and is in the code: Expanse runs its oscillator stack at
`freq * 2`, an octave above the played note, and `LayerBase::triangleWave()`
is a naive triangle with no polyBLEP correction. At MIDI 108 that puts the
stack at 8372 Hz, whose upper odd harmonics fold back into the passband. No
other layer is close: Root uses the same naive triangle but at the played
pitch behind a 350-1250 Hz lowpass, and Clearing and Bloom use polyBLEP saws.

### Why shipping this way is acceptable

- **Absolute level.** -68 and -74 dBFS. In a four-layer mix where the other
  three sit near -10 dBFS in the same region, the alias is 55-65 dB down and
  inside any reasonable masking estimate.
- **The register is not played.** The target genre is worship and CCM keys.
  MIDI 96-108 is C7-C8, above the top of essentially any part this instrument
  exists to play. Everything from MIDI 21 to 84 measures -68 dB or better.
- **The layer is nearly silent there anyway.** Expanse's bandpass is fixed in
  absolute Hz and does not track the note, so at MIDI 96+ the layer produces
  -41 to -57 dBFS. The alias is loud *relative to* a layer that has almost
  stopped contributing.
- **Playbook 7 ranks this.** "Aliasing in the top two octaves: obvious to
  inaudible matters enormously; inaudible to -100 dB matters not at all."
  Nothing here is in the obvious category.

### What would make it unacceptable

- Any alias above **-60 dBFS absolute**, at any note.
- A preset or future layer that makes Expanse loud above MIDI 96 - the
  exception is written on the fact that it is not.
- Expanse's bandpass ever being made to track the note, which would move its
  passband up with the alias content.
- A listening pass hearing it. It has not been listened to; the numbers above
  are calculated, not heard.

### The fix, if it is preferred

Give `triangleWave()` a polyBLAMP correction at its two corners - the
integrated-BLEP counterpart of the existing `polyBlepSaw()`, about ten lines,
and it leaves the waveform untouched except within one sample of each corner.
It is not done here because it changes the harmonic content of both Root and
Expanse, and because Expanse has already been changed once today (the shimmer
fix in [`g5-null-test.md`](g5-null-test.md) brought its octave back 57 dB).
Two brightness changes to one layer without ears is how a pad ends up harsh.
It belongs in the same sound-design pass as the G5 items.

### EX-003 resolution (2026-10-07)

Closed by listening. In the owner's blind A/B of 2026-10-06 (`expanse_top`, Expanse
solo C7-E7-G7) X was identified correctly and the v2 build was preferred; the note on
v1 reads "not usable". The polyBLAMP triangles (2026-09-26) and, since 2026-10-07,
v1's two-pole safety lowpass leave the top octave clean enough to hear and use.

---

## EX-004 - The output depends on the host's block size

- **Date:** 2026-10-22
- **Raised by:** Claude, Round 14 (S.5, while building the harness's `--max-block`)
- **Rule / target:** rule 18 (control signals at sample rate or in sub-blocks of 32
  samples at most), D.5.1
- **Status:** OPEN - to diagnose in its own session (D.8.3)

### What failed, and by how much

Seeded renders that differ only in block size do not null. Sternenzelt, full mix,
1024-sample blocks against 256: **-19.0 dBFS** peak residual. Per layer (REVERB 0,
1024 against 32-sample blocks): Expanse **-15.6**, Bloom **-23.1**, Root -60.5,
Clearing -63.4 dBFS. With WIDTH held at its default (Lagerfeuer, no ramp): Expanse
-30.3, Bloom -41.2 dBFS. So part of it is the WIDTH smoothing, which advances once per
render call (`LayerBase::smoothedWidth.skip`) and which Expanse's shimmer reverb reads
once per call; the rest is not yet identified.

### Why shipping this way is acceptable for now

Within one block size every render is deterministic (`reset_determinism`), a host
does not change its block size while playing, `automation_is_zipper_free` passes, and
no step or zipper has been heard. The difference is between two block sizes, not a
glitch in one.

### What would make it unacceptable

- A step or zipper audible when WIDTH or DETUNE is automated at a 1024 or 2048 buffer.
- The residual between 2048- and 32-sample blocks above -10 dBFS on any factory preset.
- A friend reporting that a bounce sounds different from playback at another buffer size.

---

## EX-005 - The parameter layout is written by hand, checked against the table

- **Date:** 2026-10-22
- **Raised by:** Claude, Round 14 (S.1)
- **Rule / target:** joint workflow / D.1.2: `parameters.csv` generates the layout
- **Status:** OPEN - waits for paramgen's display column (the owner fixes the skill)

### What failed

The generated `Source/Params.h` is compiled in (IDs, tooltips, smoothing), but
`createParameterLayout()` is written by hand: ATTACK and RELEASE show Root's time
(`dsp/EnvelopeMacro.h`), which `paramgen.py juce` cannot express, and its `percent`
format at precision 0 prints "47.7 %" in C++ while its Python round trip prints "48 %".

### Why shipping this way is acceptable

`parameter_table_matches_plugin` diffs the plugin's layout against the CSV on every
test run (IDs, order, names, 0..100 ranges, defaults, steps, automatable, through
`paramgen.py check --list-params`) and fails on a stale `Params.h` or manual.

### What would make it unacceptable

- paramgen gaining a display column: switch to the generated layout in the release after.
- Any difference reported by that test.

---

## EX-006 - pluginval has not run under AddressSanitizer

- **Date:** 2026-10-22
- **Raised by:** Claude, Round 14 (S.6)
- **Rule / target:** the Tier P release gate (sound-design-bob): pluginval L10 under ASan
- **Status:** OPEN

### What failed

Not run: it needs pluginval built from source with ASan and an ASan build of the
plugin (`validate_plugin.py --sanitizer asan`, whose own help calls it Tier F; the
skills disagree). Neither build exists in this repo yet.

### Why shipping this way is acceptable for now

pluginval strictness 10 passes at all six sample rates on the Release bundle, the
Steinberg validator 47 of 47, the RTSan suite was clean (2026-10-03), and since this
round the malloc trap runs in every Release test.

### What would make it unacceptable

- Any crash report from a friend's host.
- The skills settling ASan at Tier P: then an `asan` preset and pluginval from source
  before the next shared build.

---

## EX-007 - Blocks over the deadline at 32 samples under Rosetta, idle CPU above 2 %

- **Date:** 2026-10-22
- **Raised by:** Claude, Round 14 (S.7)
- **Rule / target:** rule 37 and D.2.1: p99.9 under 25 %, recurring maximum under 50 %,
  no block over the deadline; D.2.5: idle under 2 % after the tail
- **Status:** OPEN

### What failed, and by how much

`perf.py run` on the x86_64 slice under Rosetta (Funkenflug, the heaviest preset in a
17-preset survey; 3 runs of 60 s; M3 Pro):

| fs / block | p50 | p99 | p99.9 | max | reading | over deadline |
|---|---|---|---|---|---|---|
| 48000 / 64 | 8.1 % | 9.5 % | 10.5 % | 53.4 % | scheduling | 0 |
| 44100 / 32 | 11.4 % | 17.0 % | 24.7 % | 3339.5 % | scheduling | **35** |

Idle after the tail: **2.1 %** p50 (2 % budget); both reverbs keep running on silence.

### Why shipping this way is acceptable for now

p99.9 is inside the budget at both block sizes, and every maximum moves between runs
(scheduling, not the DSP): the harness renders on a normal-priority thread, which the
OS preempts (see the note in `tools/sound_tool/Main.cpp`); a host's audio thread is not.

### What would make it unacceptable

- p99.9 above 25 % or a recurring (DSP) maximum above 50 % at 44100/32 under Rosetta.
- Idle above 4 %.
- A friend reporting dropouts at a buffer of 64 samples or more.

Friends: If Live 10 crackles at a 32-sample buffer on an Apple-silicon Mac, set the buffer to 64 or more.


---

## EX-008 - The editor scales through setScaleFactor and cannot be dragged to size

- **Date:** 2026-10-22
- **Raised by:** Claude, Round 14 (U.1)
- **Rule / target:** ux-ui-design-bob `audio-plugins.md` 9: a content component in the design's
  coordinates scaled by a transform, the editor's own constrainer (`setResizable (true, true)`,
  `setResizeLimits`, fixed aspect ratio); `setScaleFactor` is the host's DPI notification and the
  plugin never drives it
- **Status:** OPEN

### What failed

The SIZE menu (75 / 100 / 125 / 150 / 200 % since this round, machine-local since 0.1.0) applies
the user's scale through `AudioProcessorEditor::setScaleFactor (hostScale * editorScale)`, and the
window is not resizable by its corner.

### Why shipping this way is acceptable for now

Every size renders sharp (`HorizonPadGuiSnapshot --scale 1 / 1.5 / 2`, checked 2026-10-22), the
host's own scale is kept apart in `setScaleFactor()`, and five fixed sizes cover a 13" laptop to a
large screen. Moving the whole editor into a transformed content component is a rebuild of its
layout and caching, verifiable only in hosts.

### What would make it unacceptable

- A wrong window size or blur in Live 10.1 under Rosetta or Live on Windows, at any SIZE step or
  after the host's own DPI change.
- A friend asking for a size between the steps.

---

## EX-009 - Footer text below 4.5:1 at the bright end of the sky

- **Date:** 2026-10-22
- **Raised by:** Claude, Round 14 (U.2)
- **Rule / target:** U.2: every text over 4.5:1 (3:1 for large text); the footer is 12.5 px bold
- **Status:** OPEN - the UI review of 2026-10-17 set the footer's own bar at 3:1

### What failed, and by how much

`snapshot_contrast.py` on the 1x render of the Init preset: footer left ("HORIZON PAD 0.1.0 ·
VST3") worst 3.54:1, median 4.81:1, against #B76140 (the sunset's brightest row behind it).
`HorizonScene.h` records 3.8 -> 3.2:1 from the designed palette to golden hour, 5.9:1 at night.

### Why shipping this way is acceptable for now

The UI review of 2026-10-17 lifted the footer from 2.1-2.2:1 to cream and judged >= 3:1 enough
for a strip that repeats what the editor shows elsewhere (the product name is also the wordmark;
the SIZE step is in its menu). Raising it to 4.5:1 means a darker band behind the footer or a
different sky, a design decision.

### What would make it unacceptable

- Any palette taking the footer below 3.0:1.
- The footer carrying information shown nowhere else (since this round the version line opens
  the About box, which shows the version at 11.7:1 on a card).

---

## EX-010 - The active preset pill's gold text is 3.19:1

- **Date:** 2026-10-22
- **Raised by:** Claude, Round 14 (U.2)
- **Rule / target:** text 4.5:1 (visual-design.md 4); the pill text is about 13 px
- **Status:** OPEN - a fix is proposed, the owner decides (a visible change to an approved design)

### What failed, and by how much

The active pill draws gold text (#F5AE39) on its gold-tinted fill over the sky: 3.19:1 against
#735F41 on the Init render (`snapshot_contrast.py`, worst = median). Inactive pills measure
4.99-6.86:1.

### Why shipping this way is acceptable for now

The active pill is also marked by its gold border and fill, so its state does not depend on
reading the text, and the same name is shown again, larger, under the wordmark on every recall.

### What would make it unacceptable

- Any palette taking it below 3.0:1.

### The fix, if it is preferred

Cream text (#F3E6C8, the preset title's colour) on the active pill, keeping the gold border and
fill: about 4.9:1 on the same background by calculation, to be confirmed on the renders.
