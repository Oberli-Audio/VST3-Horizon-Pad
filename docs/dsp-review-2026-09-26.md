# Critical review: guidelines, workflow and sound design - 2026-09-26

Scope: the bob Audio DSP playbook (v2.1: core card, reference, invariants, the
two skills), this project's DSP workflow (gates, tools, tests, the sound-designer
agent) and Horizon Pad's actual sound, measured. Written from `main` at `bf17ee8`.
Everything marked **measured** was measured with `HorizonPadSoundTool` (Release,
48 kHz, `--seed=7`) and the descriptor script now committed as
`tools/measure/descriptors.py`; the numbers are reproducible with it.

The short version: the process is excellent at proving the plugin is not *broken*
and has no way of saying whether it is *good*. Every rule, gate and test detects a
defect. None states or measures a sound-design target, so the design principles
the whole instrument was built on have quietly eroded, and nothing noticed.

---

## 1. Guidelines (the playbook and invariants)

| # | Weakness | Evidence |
|---|---|---|
| G-1 | **Defect-only verification.** The 23 rules, the Part 4 battery and every test answer "is anything wrong?". Nothing answers "is it the sound we designed?". Part 1.3's sound-design theory (the brightness/attack grid, contrast, movement) is stated but never turned into a measurement. | Measured below (S-1): the grid this instrument was designed on has collapsed, and every gate is green. |
| G-2 | **No mix context.** The brief's job statement is "sit UNDER acoustic guitar, piano and group vocals without fighting them". No gate, metric or test looks at where the energy sits relative to those instruments. | Measured (S-4): the layers put their energy in 80-500 Hz, the acoustic-guitar-body and piano-left-hand region, and nothing above 8 kHz. |
| G-3 | **One-note verification.** An instrument is played across its range and in chords. The battery and tests are almost all single notes at C4. Keyboard-range behaviour and polyphonic coherence are unmeasured. | Measured (S-2, S-5): 27 dB loudness span on one layer; Bloom's tremolo loses 60% of its depth in a chord. |
| G-4 | **The rule set is shaped by effects, not instruments.** Rules 5, 6, 7, 16, 17, 19, 20 are n/a here; there is no rule for key tracking, level across the range, sustain pedal, voice-count sufficiency or coherent polyphonic modulation. | The plugin has no sustain pedal (S-8). No gate asked. |
| G-5 | **Faust-first carries prototype workarounds into the product as design.** The drift LFO is a sine *because* Faust's `lfnoise` was unstable in single precision - a Faust-specific constraint the C++ port does not have. It was ported as if it were a sound-design decision. The prototype is also mono and single-voice, so it cannot express stereo or polyphonic design at all. | `four_pads.dsp` drift comment; S-6. |
| G-6 | **Port fidelity is checked in the wrong domain.** The G5 method compares steady-state band energy. Detune, beat rate and modulation differences are invisible to it. | Root's detune is +7/-6 cents in C++ and +1.2/-1.0 cents in the prototype (in the code since `03fbf52`, undocumented). G5 did not see it. |
| G-7 | **Listening is the bottleneck, and there is no instrument for it.** The process correctly demands a level-matched blind A/B, then provides no tool to run one. Seven items sit in listening debt. | `gate-status.md`, "The listening pass". |
| G-8 | **No quality regression baseline.** Tests guard three past bugs. A change that makes the sound worse without tripping a defect metric lands silently. | `tools/tests/dsp_tests.py`: 8 tests, none about timbre, balance, range or stereo. |

## 2. Workflow

| # | Weakness | Evidence |
|---|---|---|
| W-1 | Gate paperwork is thorough; the gates never had a sound target to hold the design to. G3's "metric table" is defect metrics only. | `g3-measurements.md` |
| W-2 | The sound-designer agent optimises defect metrics because those are the only metrics it has. A "make Root warmer" request has no definition of warmer. | `.claude/agents/sound-designer.md` |
| W-3 | Documentation drift in code comments: WIDTH's mono-safety comment says `kMonoSafetyBlend = 0.15`, the code says `0.30`. CLAUDE.md says the tests take ~30 s; they take 129 s. | `LayerBase.h`, CTest run |
| W-4 | Two implementations of the sound (Faust and C++) are maintained by hand and have already drifted (G-6). The playbook treats the prototype as permanent source of truth; for a shipped product it is really a frozen v1 reference. | G5 table, Root detune |

## 3. Sound design (measured)

C3-E4 chord (MIDI 48, 55, 60, 64), each layer solo, 8 s hold, steady state = last 3 s.

| Layer | Design intent (prototype header) | Centroid | Time to -3 dB of peak | 1-4 kHz | > 8 kHz |
|---|---|---|---|---|---|
| Root | dull, slow | 184 Hz | 1.86 s | -35.5 dB | -84 dB |
| Clearing | **bright**, medium | 323 Hz | 1.52 s | -14.9 dB | -54 dB |
| Expanse | **very bright**, **very slow**, "clear air above the other three" | 596 Hz | 1.70 s | -22.5 dB | -81 dB |
| Bloom | medium, medium | 261 Hz | 1.07 s | -16.4 dB | -61 dB |

- **S-1 The contrast grid has collapsed.** The design's own organising principle is
  four *different* points on brightness x attack. Attack: 1.1-1.9 s, all "slow".
  Brightness: 184-596 Hz, and no layer has air. The taming rounds (all four
  documented in `four_pads.dsp`) pulled the attacks together on purpose - "arrives
  noticeably later" was the owner's complaint - so the contrast now has to come
  from somewhere other than attack time: register, stereo image and motion.
- **S-2 No key tracking.** Every filter is fixed in Hz, voiced at about C4. Solo
  loudness from C2 to C7: Root -17 to -29 LUFS, Expanse **-43 to -55 LUFS with -29
  in the middle (a 27 dB span)**, Bloom -19 to -31. At C2, Root's centroid is 55 Hz
  - it is almost all sub. The same chord in the left hand and the right hand gives
  a different blend.
- **S-3 Mono voices, Haas width.** Every voice of every layer is mono; stereo comes
  only from WIDTH (one Haas tap per layer, which combs in mono and loses ~2 dB at
  full width - both already measured) and the reverb. The modern pad's width comes
  from decorrelated oscillators spread across the field, which is mono-safe by
  construction.
- **S-4 Energy sits in the mix's busiest region.** All four layers peak in 80-500 Hz;
  the reverb send is full-band, so the sub-oscillator's 33-130 Hz goes into the room
  too. For an instrument that must sit under acoustic guitar and piano, that is the
  wrong pocket.
- **S-5 Bloom's defining movement cancels in chords.** Tremolo depth at 3.2 Hz: 34%
  on one note, **14% on a four-note chord** - each voice's tremolo has a random
  phase, so a chord averages its own pulse away. Pads are played in chords.
- **S-6 Movement is periodic.** Every oscillator's drift is a sine at 0.08-0.37 Hz,
  +/-6.9 cents. The playbook's own guidance is "small, slow, *irregular*". The sine
  is a Faust workaround (G-5), not a design choice.
- **S-7 Clearing's ensemble is a slow mono flanger** (one delay swept 2-11 ms at
  0.11 Hz, per voice). The design cites Synth Secrets on string machines, whose
  ensemble is a multi-line chorus with a slow and a fast LFO in three phases and a
  stereo output. The per-voice delay line also violates rules 10 (linear
  interpolation) and 21 (buffer sized in samples: the 11 ms top of the sweep is
  clamped to ~10.7 ms at 192 kHz).
- **S-8 No sustain pedal, 8 voices.** CC64 is ignored. For a worship-keys pad,
  pedalled chord changes are the normal way to play; with a pedal, 8 voices steal
  constantly.
- **S-9 Open items carried in:** Expanse aliases at MIDI 96-108 (EX-003, naive
  triangle); the shimmer grain read is linearly interpolated (rule 10); Root's
  detune differs from the prototype (G-6).

## 4. What is being done about it

Split by role, as the playbook asks. The sound changes are on branch
`sound-design-v2`; they are **not** merged, because each one changes the sound
and needs your ears. `tools/listening/` makes that a 20-minute blind A/B.

| Role | Action | Addresses |
|---|---|---|
| Verifier | `tools/measure/descriptors.py`: timbre grid, keyboard range, mix pocket, stereo/mono, modulation spectrum, preset bank; JSON baselines and a compare mode | G-1, G-3, G-8, W-2 |
| Verifier | New tests: a presence test per defining feature (rule 22), keyboard level span, chord tremolo depth, mono compatibility, WIDTH level flatness, sustain pedal | G-3, G-8, S-5, S-8 |
| Verifier | `tools/listening/`: renders baseline vs candidate, loudness-matches them, and writes a blind A/B/X page with instant switching and exportable verdicts | G-7 |
| Implementer | Key tracking anchored at C4, so the voiced sound at C4 is unchanged; sub-octave faded below ~45 Hz | S-2 |
| Implementer | Per-oscillator stereo spread replaces the Haas tap; WIDTH keeps its knob and meaning (0 = mono) | S-3, S-1 |
| Implementer | Irregular (smoothed random) drift, RMS-matched to the old sine | S-6 |
| Implementer | Bloom's tremolo and filter LFO shared across voices | S-5 |
| Implementer | Clearing: stereo three-phase ensemble on the layer bus, Hermite interpolation, times in seconds | S-7 |
| Implementer | Expanse: key-tracked sweep, stereo shimmer reverb, polyBLAMP triangle, Hermite shimmer read | S-2, S-3, S-9 |
| Implementer | Reverb send: low cut and pre-delay; REVERB level flatness re-verified | S-4 |
| Implementer | Sustain pedal (CC64); same-note retrigger releases the old voice | S-8 |
| Producer | Playbook v2.2: sound-design targets as measurements, instrument rules, mix-context and range checks, prototype-constraint review, modulation-domain port fidelity, listening tooling | G-1 to G-8 |

Not changed, flagged for a decision: Root's detune (7 vs 1.2 cents) is a character
call, not a defect; it is on the listening list.

The outcome of each action, with numbers, is in
[`sound-design-v2.md`](sound-design-v2.md).
