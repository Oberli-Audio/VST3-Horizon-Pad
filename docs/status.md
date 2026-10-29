# Status - Horizon Pad 0.1.0 (2026-10-29)
Tier P, track I (also owns C: MIDI, pedal, wheels; E: shimmer, ensemble, reverb; F.5-F.6 mix and
output), at gate G6, hosts. Recipe D.8.10 (release) next. sound-design-bob 5.0.1, plugin-ui-bob
1.0.1, ship-bob 1.0.0; the scripts under `scripts/` are byte copies of those kits (2026-10-29).

This page replaced `docs/gate-status.md`, `docs/exceptions.md` and `docs/handoff.md` on
2026-10-29 (sound-design-bob D.9); their full records are in git at `8a14117`
(`git show 8a14117:docs/exceptions.md`).

## Gates
| Gate | State | Evidence (command -> number, test name, log) |
|---|---|---|
| G0 Brief | closed 2026-09-23 | `brief.md`, owner-confirmed; amendments A-001..A-011 (A-009 worship envelopes, A-010 the register C0-C7, A-011 ATTACK/RELEASE as a time) |
| G1 Algorithm | closed 2026-09-24 | `g1-algorithm.md` |
| G2 Prototype | closed 2026-09-24 | `g2-prototype.md`, `sound design/four_pads.dsp` |
| G3 Measure | closed 2026-09-24, one row short | `g3-measurements.md`; FILTER's response at 44.1 against 96 kHz not measured as a sweep (every filter is TPT/ZDF) |
| G4 Port | closed | Release build; constants documented beside the code |
| G5 Null test | closed, characterised and declared | `g5-null-test.md` |
| G6 Validate | open: hosts | `ctest --test-dir build-release --output-on-failure` -> 46/46 (2026-10-28, `3aa75fd`); `python3 tools/validate/validate.py --profile quick` -> PASS (validator 47/47, SDK 3.8.1; pluginval L5); CI on every push to main: validator and pluginval L10 at six rates on the downloaded macOS and Windows artefacts, the DSP suite, Windows ASan -> green on `8a14117` (2026-10-28); ASan + UBSan on the Mac clean (2026-10-26); RTSan 29/29 (2026-10-17, re-run owed on the grown suite). Hosts: Ableton Live 10.1 under Rosetta, editor F checked by the owner 2026-10-29 (100-200 %, keyboard focus and the space bar, menu and drawer in the plugin window, reopening: all fine); Ableton Live on Windows: owed |
| G7 Product | closed, release gate owed | README (ship-bob template), 17 presets in three families, `CHANGELOG.md`, `THIRD_PARTY_NOTICES.md`; `python3 scripts/ship/shipcheck.py docs .` -> OK (2026-10-29) |

CPU (rule 20): `perf.py run --harness build-release/HorizonPadSoundTool --arch x86_64 --presets
funkenflug --config 48000/64 --runs 3` -> p99.9 14.3 %, maxima moving between runs (2026-10-22,
M3 Pro, the x86_64 slice Ableton Live 10.1 runs under Rosetta); `docs/perf.md`. The friend at 64
samples is the dropout test; the 2017-era budget machine is still unmeasured.

## Listening debt
- 2026-10-17: the register ends, C0 against C1 and C7 against C6 (`register_bottom_root`,
  `register_bottom_bloom`, `register_bottom_blend`, `register_top_root`, `register_top_clearing`):
  music or a wobble? Root above C5 an octave low? Session: `python3 tools/listening/make_session.py
  --baseline build-release/HorizonPadSoundTool --candidate build-release/HorizonPadSoundTool --out
  build-listening --only <items>`.
- 2026-10-17: `bloom_movement`, Bloom's movement halved since DETUNE's default went to 0 %: lost
  life, or only beating?
- 2026-10-14: the pads at worship length (A-009), right or too abrupt; the in-betweens (Talwind,
  Sturmfront, Kupferglanz) release longer than the pads, keep?
- 2026-10-07: `blend_high`, the C2 open fifth, Root C1/C2 start, `detune_wide`, Dämmerlicht (pass 2).
- 2026-10-07: presets: Glasperle still glass, Silberpfad over a pad, the in-betweens speaking in
  0.2-0.45 s, the first five.
- Carried: EX-001's 2 Hz DC blocker; G5's filter Q and Expanse's lowpass order; the shimmer blend
  16 dB under the prototype; G1's linear interpolation on the flanger and shimmer reads.

## Open exceptions
- EX-002 2026-09-23: G7 bank loudness, brief +/-1 LU of the bank median; Sternenzelt measured 1.94 LU below the median (-18.07 against -16.13 LUFS, median of 4 seeds, chord 48-55-60-64-67, 8 s, gate 6 s, 2026-10-22), the only preset outside; shipping is acceptable because its volumes are at their ceiling (Expanse 1.000) and matching it would pull the whole bank down 8.3 dB, and the owner kept it by ear 2026-10-03; **unacceptable if** Sternenzelt falls more than 3.0 LU below the bank median or a second preset leaves +/-1 LU on that measurement | re-checked when a preset or the gain structure changes. Friends: Sternenzelt plays about 2 dB quieter than the other presets.
- EX-004 2026-10-22: rule 2 (control signals at sample rate or in sub-blocks of 32 samples at most), D.5.1; seeded renders differing only in block size do not null: Sternenzelt 1024 against 256 samples -19.0 dBFS peak residual; per layer (REVERB 0, 1024 against 32) Expanse -15.6, Bloom -23.1, Root -60.5, Clearing -63.4 dBFS; part of it is WIDTH's smoothing advancing once per render call; shipping is acceptable because one block size is deterministic (`reset_determinism`), a host does not change its block size while playing and `automation_is_zipper_free` passes; **unacceptable if** the residual between 2048- and 32-sample blocks exceeds -10 dBFS on a factory preset, a step is heard when WIDTH or DETUNE is automated at 1024 or 2048, or a bounce sounds different from playback | re-checked when diagnosed (D.8.3, its own session). Friends: none
- EX-005 2026-10-22: the parameter table generates the layout (joint workflow); `createParameterLayout()` is hand-written because ATTACK and RELEASE show Root's time, which `paramgen.py juce` cannot express; shipping is acceptable because `parameter_table_matches_plugin` diffs the layout against `parameters.csv` on every run (`paramgen.py check --list-params`); **unacceptable if** that test reports any difference, as test `parameter_table_matches_plugin` | re-checked when paramgen gains a display column. Friends: none
- EX-008 2026-10-22: plugin-ui-bob `audio-plugins.md` 9 (a content component scaled by a transform, the host drives `setScaleFactor`); the GUI scale (75/100/125/150/200 %) goes through `setScaleFactor (hostScale * editorScale)` and the window cannot be dragged to size; shipping is acceptable because every step renders sharp (`HorizonPadGuiSnapshot --scale 1 / 1.5 / 2`, editor F 2026-10-28) and the host's scale is kept apart; **unacceptable if** a wrong window size or blur appears in Ableton Live 10.1 under Rosetta or on Windows at any step or after a DPI change, or a friend asks for a size between the steps | re-checked at every host pass. Friends: none

## Handoff
Task: housekeeping before 0.1.0 for the skill restructure of 2026-10-27. Plan: 1. [x] `scripts/`
refreshed from sound-design-bob 5.0.0 and ship-bob 1.0.0 2. [x] this page and `failures.md`
replace the gate, exception and handoff files 3. [x] `ship.toml` without the removed fields
4. [x] README feedback and Known issues 5. [x] `CLAUDE.md` on the new skills and rule numbers,
EX-007 closed 6. [ ] (next) the release gate for 0.1.0 (sound-design-bob `joint-plugin-workflow.md`,
Release gate), then ship-bob's six steps.
Working tree: clean after the housekeeping commit. Build: green. Tests: 46/46.
Open questions for the owner: Ableton Live on Windows (G6); the listening debt above.
Next command: `python3 scripts/dsp-kit/validate_plugin.py --help` (the unattended part of the release gate).

## Decisions
- 2026-10-29: editor F passes the owner's host check in Ableton Live 10.1 under Rosetta on the Mac (100-200 %, keyboard focus and the space bar, menu and drawer, reopening); Windows still owed.
- 2026-10-29: EX-007 (blocks over the deadline at 32 samples under Rosetta, idle CPU) closed under rule 20 (was 37): Tier P judges 48 k/64 on the slice the host runs, where p99.9 is 14.3 % with maxima moving between runs; `over_deadline` is reported, never judged; 44.1 k/32 is Tier F; idle 1.2 % (budget 2 %). Its Known issue left the README with it.
- 2026-10-29: the project documents follow sound-design-bob D.9: `docs/brief.md`, this page, `docs/failures.md`; the gate artefacts `g1-algorithm.md`, `g2-prototype.md`, `g3-measurements.md`, `g5-null-test.md` stay as the evidence the Gates table cites.
- 2026-10-28: `line-control` (Charcoal) #6B665D -> #716B62, in the Oberli Audio design system and the plugin: control outlines 3.01:1 on the header (2.79:1 before).
- 2026-10-28: editor F built from the Oberli Audio design system and merged (`docs/editor-f-brief.md`, `docs/editor-f-review.md`): window 1160 x 548, GUI scale 75-200 % with 125 %, 11 px labels, tagline on About, the LIMIT LED and two meter wells (meter-only atomics, null-tested bit-identical), A/B in the header, Report a problem copies the diagnostics.
- 2026-10-28: EX-009 (footer text 3.54:1 on the sky) and EX-010 (the active preset pill's text) closed: editor F removed the footer and the pills; the only text on the sky passes 4.5:1 over the FILTER range.
- 2026-10-27: Oberli Audio: manufacturer code Obrl, `com.oberliaudio.horizonpad`, `.oberlipreset`; older `.bobpreset` and `.quellepreset` files still import.
- 2026-10-26: EX-006 (pluginval had not run under AddressSanitizer) closed: ASan + UBSan clean on the Mac, ASan clean on Windows in CI; the JUCE VST3 wrapper's MIDI-mapping bounds check found on the way is `cmake/PatchJuce.cmake`.
- 2026-10-22: EX-011 (the bank's loudness span) closed by the owner: the brief's +/-1 LU is a window around the bank median, not a 1 LU span; presetlint follows (the refreshed copy in `scripts/dsp-kit/`).
- 2026-10-21: VERSION 0.1.0; the state is versioned from here (state version 2, values 0..100 with a v1 migration, golden states in `tools/tests/state`); ask before an ID or state change.
- 2026-10-17: the register is C0-C7 (A-010), silent outside, by the owner's listening.
- 2026-10-14: worship envelopes (A-009): ATTACK/RELEASE run 1 % to 100 % of each layer's designed time; every preset swells within 1.2 s and clears within 1.4 s.
- 2026-10-07: EX-003 (Expanse aliasing at MIDI 96-108) closed by listening: the owner preferred v2's top octave in the blind A/B (polyBLAMP triangles, v1's two-pole safety lowpass).
- 2026-10-07: 17 presets in three families (Pads, In-between, Leads), Lagerfeuer the init.
- 2026-10-06: formats VST3 and Standalone; AU and CLAP declined by the owner.
- 2026-09-23: EX-001 (DC offset at the limiter's extreme) closed: a 2 Hz DC blocker after the limiter, -80.3 dBFS worst of six (test `output_dc_and_knee`).
- 2026-09-22: promoted from Tier S to P, re-entered at G0.
