# Editor F: build review (2026-10-28)

Tier P, ux-ui-design-bob (Build) with sound-design-bob alongside. Brief, sources and the
owner's seven decisions: `docs/editor-f-brief.md`. Tokens and measured contrast:
`docs/brand-sheet.md`. Branch `editor-f`.

**Goal and assumptions.** Friends playing worship pads in a DAW, mostly from a MIDI keyboard,
occasionally reaching for the mouse: pick a preset, shape it with six macros, balance four
pads, see that the sound is alive. The design phase is final (the owner picked F from nine
sketches); this is the build, checked against the sketches and the design system.

## What works well, and was kept

- **The sky as a picture of the patch.** FILTER, REVERB and WIDTH still move it, on the same
  springs and in OKLab; it now sits in a frame and nothing else glows, so it reads as the one
  live display (round 11).
- **Motion only when the sound changes:** the recall wave, the sunrise on first open, the knob
  that follows a hand at once but glides for automation. Kept unchanged.
- **DAW knob behaviour** from the 2026-10-17 review: double-click reset, the host's menu, Tab and
  arrows, no focus on click (the space bar keeps reaching the host).
- **One row, read left to right as the sound is made:** wheels, the four pads, the macros, the
  output. With the old eight cards gone, the controls take 126 px of height instead of 346.

## Implemented

| Area | Before | Now |
|---|---|---|
| Window | 1080 x 748, the sky behind eight cards and a footer | 1160 x 548: header, the sky as a 1104 x 294 display, one control row |
| Look | sunset palette, per-layer colours, serif wordmark, glows | Oberli Audio Charcoal, mango accent, Rubik, flat chrome, no glows outside the sky |
| Knobs | ring with a glowing dot, 78 / 37 px | the Knob card at 48 px, readout bubble while dragging, Shift for fine |
| Pad output | a coloured glow around each pad card | a 32 x 4 px level bar under each pad's value |
| Presets | a scrolling row of pills, Browse menu, inline save field | header pill (arrows, name, " *"), the PresetDrawer (categories, search, Yours, Export…, Show folder), Save preset… card |
| A/B | a group at the row's end, green copy flash | segmented A/B and copy in the header; "Copied A to B" readout |
| Size, About | footer SIZE menu and product label | the menu: GUI scale 75-200 % (125 % kept), Report a problem…, About on the back of the panel (Bone) |
| Output | gradient meter, dB ticks | the Meter card's look on the same measurement (20 ms RMS, peak hold), "RMS", the LIMIT LED |
| Fonts | the system's sans and serif | Rubik 2.300, five static weights, OFL-1.1, bundled unmodified; `tnum` on values |

Removed, with where their function lives now: TitleBanner (name in the header, preset title on
the sky, sunrise kept, the logo's sun dropped), PresetBar (header pill and drawer), PadKnob and
MacrosPanel (KnobColumn), FooterBar (menu and About; the tagline on About).

## Done when (brief), with the evidence

| Item | Result |
|---|---|
| Renders at 100 / 150 / 200 % that match the sketches | Rendered with `HorizonPadGuiSnapshot` for Lagerfeuer, Mitternachtsblau, Goldstaub, a FILTER drag (FILTER 64 %, edited), the menu with its GUI scale submenu, the drawer, About and its licences, the Save card, and LIMIT lit. Each matches its sketch (SkyDisplay, FNight, FGolden, FTurning, FMenu, FDrawer, About E) in layout, sizes and colours; the differences are the owner's decisions and the deviations listed in the brand sheet. |
| `snapshot_contrast.py` on all text over the sky | Pass at 1x: Talwind (the longest description) at FILTER 0 / 12 / 50 / 100 %, WIDTH 40 and 100 %, REVERB 100 %: worst 4.58:1 (description at the brightest corner), name 5.28:1 or more, every box AA (`--tol 120`; the default 60 counted antialiased glyph edges as background). At 2x the golden-hour case passes at 4.64:1 (`--tol 200`). Without the shadow it was 4.45:1 at that corner. Re-measured 2026-10-29 with plugin-ui-bob 1.0.1's fixed script at its default tolerance: all 32 boxes AA at 1x and 2x, worst 4.58:1 and 4.64:1. |
| `--metrics` on the renders | Chrome (the sky masked): glow 0.3-0.6 %, halation 0 %, 2 palette colours, ground #1C1B19: pass. Whole render: palette 18-23 colours and 4.6 % saturated pixels on golden skies, all from the Scene's gradient, which round 11 allows. |
| `audio-plugins.md` 15 code checklist | Below, line by line. |
| `ctest --test-dir build-release --output-on-failure` | 46 of 46 passed (serial; the two new tests and the two validators included). One earlier run with `-j 4` failed `harness_contract_fast` (block timing under load); it passes alone and serially. |
| `python3 tools/validate/validate.py --profile quick` | PASS on the final build: build, ctest 44/44, Steinberg validator 47 passed (SDK 3.8.1), pluginval strictness 5 (editor tests included). |
| Brand sheet and CLAUDE.md | Rewritten from the Oberli tokens; CLAUDE.md's GUI section describes editor F; DEVELOPING.md and the README's feedback line follow. |
| No grade 3 or 4 findings | None open; the open findings below are all grade 1. |

### The processor changes, meter only (owner 2026-10-28)

- `isLimiting()`: a flag set when a sample passes the limiter's knee (0.80, -1.94 dBFS) in the
  20 ms window, for the LIMIT LED. Test `limit_led_only_past_knee` (the sound tool reports
  `limitingBlocks`): up at the limiter's extreme, down for the default patch and the edge
  presets.
- `getOutputChannelLevel()`: each channel's RMS over the same window, for the meter's two wells.
  Test `output_meter_per_channel` (the sound tool reports `meterRmsL/R`): equal to the render's
  last 20 ms per channel within 0.01 dB (measured 0.000002 dB), on a note 6.5 dB louder left.
- No parameter, ID, state or preset change. Null test, after each: 12 seeded renders of main
  (`d4da383`) and `editor-f` - the default patch, the limiter's extreme, Mitternachtsblau and
  Goldstaub, each at 48 k/512, 96 k/13 and 44.1 k/64 - are bit-identical. Both tests fail on
  main's build (the fields do not exist there).

### Tabular figures

Rubik's default figures are proportional (Regular: 0 is 635 units wide, 1 is 428); the font
has a `tnum` feature, and JUCE 8.0.16 switches it on with `FontOptions::withFeatureEnabled`.
Every value, the version and the menu's percentages use it, so no fixed-width cells were needed.

## Code checklist (plugin-ui-bob `audio-plugins.md` 15; ux-ui-design-bob's until 2026-10-27)

| Check | Answer |
|---|---|
| Keyboard focus | Every knob (`AnimatedKnob::bindParameter`), both wheels, every header button, the drawer's categories and search, the Save card and About's buttons take Tab focus; no click takes it (`setMouseClickGrabsKeyboardFocus (false)`), so the space bar reaches the host. The focus ring is the system's 2 px `ink`, 3 px off the control (`drawFocusRing`). The menu and the drawer's list take the arrow keys themselves, their highlight being the focus. |
| Accessible names | Knobs: "Root volume" … "Detune" (`setTitle`), read with the parameter's own text through the attachment. Wheels: "Pitch bend", "Mod wheel". Header: "Sound A", "Sound B", "Copy A to B", "Previous preset", "Preset: Lagerfeuer[, edited]. Opens the preset list.", "Next preset", "Menu". Menu rows and drawer rows by their words; "Output level"; the sky "Sky" with what it follows. |
| Double-click reset | Knobs to the parameter's default; wheels to 0. |
| Drag sensitivity | JUCE's rotary drag (250 px for the full range), vertical or horizontal; Shift for fine (velocity mode at 0.25); the scroll wheel in steps; wheels drag relative to where they are. |
| Host context menu | Right-click on a knob opens the host's own menu for that parameter; in the standalone, "Reset to default". |
| Resizing / scale | GUI scale 75 / 100 / 125 / 150 / 200 % in the menu, machine-local, through `setScaleFactor` on top of the host's scale (EX-008: no free resizing). Renders crisp at 1.5x and 2x (vector, caches at the pixel density). |
| Modified mark | " *" in `accent-ink` after the preset's name once the sound differs from it (`isActivePresetModified()`, compared); the diagnostics say "(edited)". |
| Units | The parameter's own text, as the host shows it: "48 %", "0.64 s", "22 ms"; pitch "+1.2 st" with a real minus; the meter "−19 dB". |

## Open findings

| Grade | Where | Problem | Rule / evidence | Fix |
|---|---|---|---|---|
| 1 | Knobs with keyboard focus | The focus ring (3 px off the cap, the card's geometry) covers the inner half of the value arc | Knob card; render `--knob-sheet` | Keep (pointer and value still show it), or move the ring outside the track in the system |
| 1 | Reopening the editor | Resident memory 45 → 66 MB over the first four opens, then flat (+1 MB from round 15 to 45, +0.03 MB over the last five of twenty) | `HorizonPadGuiSnapshot --reopen=20/45`; the old editor read 47 MB flat | Bounded warm-up (text and glyph caches; not the fonts - holding them made no difference). Re-check U.7 in a DAW. |
| 1 | Drawer, Yours | × deletes a preset in one click | Recoverable: it goes to the Trash (`moveToTrash`), and the tooltip says so | Undo in the drawer if friends ask |

Closed after the review: `snapshot_contrast.py` read antialiased glyph edges as background at 2x
(grade 1); plugin-ui-bob 1.0.1 fixed the script (2026-10-29) and the re-measure passes at its default
tolerance. The header's A/B group and preset pill outlines read 2.79:1 on
`surface-raised` (grade 2). The owner moved `line-control` (Charcoal) to #716B62 on 2026-10-28,
in the design system and the plugin: 3.01:1 on the header, 3.26:1 on ground.

## Still owed (owner side)

- The DAW checks of the release gate (joint-plugin-workflow.md) in Ableton Live on Windows: the
  editor at 100-200 %, keyboard focus and the space bar, the menu and drawer inside the plugin
  window, reopening. The Mac passed: Ableton Live 10.1 under Rosetta, all fine (owner
  2026-10-29).
- Done 2026-10-29: the design system follows the decisions (round 12): `label` 11 px, the Menu
  card with 125 %, the PluginHeader card with A/B and copy, the Meter card's LIMIT and RMS
  variant, and the copy and close icons.
