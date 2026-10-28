# Brief: the Horizon Pad editor as sketch F (Oberli Audio)

Tier P, ux-ui-design-bob (editor) with sound-design-bob alongside (shared parameter table;
nothing in it changes here). Written 2026-10-27 from the design session that produced the
sketches; the owner picked **F, sky over the strip**.

This replaces the editor's look and layout. It changes no DSP, no parameter, no parameter ID,
no state format and no preset: the work is `Source/gui/`, `PluginEditor.*`, the snapshot tool,
fonts and docs. If anything turns out to need a state or ID change, stop and ask (state
versioning has been live since 2026-10-21).

## Sources

- **Sketches (Design canvas):** https://claude.ai/artifact/T6bGd6t8Nec1U7UfFL7Tgh, read files
  with the Artifact tool (`action: "read"`, `path: "project/<file>"`). The ones to build from:
  - `SkyDisplay.dc.html`: F, the main board (Lagerfeuer). Its props drive every F variant.
  - `FNight.dc.html` (Mitternachtsblau), `FGolden.dc.html` (Goldstaub), `FTurning.dc.html`
    (FILTER being dragged, preset edited), `FMenu.dc.html` (menu with GUI scale), `FDrawer.dc.html`
    (preset drawer, clickable).
  - Parts: `Header`, `Knob`, `Wheels`, `Output`, `Drawer`, `Sky`. `Sky.dc.html` is a JavaScript
    port of `HorizonScene` for the sketches only; the C++ stays the source of truth.
  - `About.dc.html` (E) is the About page on the back of the panel, in the Bone theme.
- **Design system "Oberli Audio":** https://claude.ai/artifact/GK9BTMQuZhZz8pxvhHsytp, read
  `project/README.md`, `project/tokens.json` and the cards `project/components/<Name>/README.md`
  for PluginWindow, PluginHeader, Knob, Slider, Meter, Menu, PresetDrawer, Tooltip, AboutBack,
  Button, Icon, Logo. Rounds 10 (wheels first on the left) and 11 (the Scene) were added for
  this editor.
- **Today's code:** `Source/gui/*` and `Source/PluginEditor.*`; each class comment says what it
  does and why. Keep every behaviour listed under "Keep" below.

## The window

1160 x 548 at 100 % (today: 1080 x 748). Charcoal theme, accent mango. Top to bottom:

| Part | Size | Content |
|---|---|---|
| Header | 48 px, `surface-raised`, 1 px `line` below, 16 px sides | `oberli-bone.svg` 18 px high (69 x 18), 1 px divider, "Horizon Pad" (`plugin-name`); right: A/B segmented + copy A to B, preset pill, version `0.1.0` (`meta`, `ink-subtle`), menu button |
| Body padding | 28 px all round | |
| Scene display | 1104 x 294, `radius-lg` 14, 1 px `line` border | the sky (HorizonScene) inside; preset name (22/28, 700) and description (13/19) at 24/20 px from its top-left after a recall |
| Gap | 24 px | |
| Control row | one row, groups split by 1 px `line` with 24 px either side | WHEELS (PITCH, MOD) - PADS (ROOT, CLEARING, EXPANSE, BLOOM) - MACROS (ATTACK, RELEASE, FILTER, REVERB, WIDTH, DETUNE) - OUTPUT |

Control row detail: section titles caps 11/14, 700, +0.12em, `ink-muted`, 16 px above the
controls. Knobs `knob-md` 48 px in 64 px columns, 16 px apart; label (10/12, 600, +0.1em,
`ink-muted`) 4 px under the knob, value (12/16, 500, tabular, `ink`) 8 px under the label.
Under each pad's value a 32 x 4 px level bar (well `surface-sunken`, fill `meter-level`) shows
that layer's output (`getLayerLevel()`), replacing the card glow. Wheels: two upright Sliders,
24 px wide, as tall as the knob (48 px) in the Strip, 4 px `line-strong` track, 14 px thumb
(`surface-panel`, 1.5 px `line-control` ring, short `accent` pointer), PITCH fills from a centre
detent and springs back, MOD fills from the bottom and holds. Output: the Meter card, two 8 px
wells 48 px tall with a 1 px `line` border, `meter-level` below -6 dBFS and `meter-hot` above,
a peak line in `ink`, "PEAK" and the dB value under it.

Exact values: `project/tokens.json` in the design system (charcoal #1C1B19, bone #EDE8DF,
mango #F09300, `surface-raised` #232220, `surface-sunken` #171614, `surface-panel` #2A2826,
`line` #3A3836, `line-strong` #4A4844, `line-control` #6B665D, `ink-muted` #BDB6AA,
`ink-subtle` #9C968B, `meter-hot` #F6BF26, `readout` #EDE8DF on `on-readout` #1C1B19).

## The Scene (HorizonScene)

Keep `HorizonScene` and its mapping exactly (FILTER time of day in OKLab, REVERB haze and lift,
WIDTH glow stretch, the springs, recall glide, sunrise from night). Change only where and how
big it draws:
- It paints into the display rectangle, not the whole window, with the 14 px corners built into
  its cache (as today's square corners are), so drawing stays a 1:1 copy with no clip.
- Scale to the display: ridge height `min(120, h * 0.3)` (88 px here), ridge blur and glow offset
  by that ratio; glow diameter by `w / 1080`. These are the sketch's numbers; tune by eye with the
  owner if needed.
- The window border it drew goes; the display's 1 px `line` border replaces it.
- Text on the sky (the recalled preset's name and description) must pass 4.5:1 over the whole
  FILTER range: measure with `snapshot_contrast.py` at FILTER 0, 12, 50 and 100 % (README, the
  Scene rule).

## Keep (behaviour that moves, not disappears)

| Today | In F |
|---|---|
| AnimatedKnob: drawing eases, parameter never does; glide wave on recall/A-B; sweep up on first open; double-click reset; host context menu; Tab and arrows; no focus on click; `setTitle` names | same, redrawn to the Knob card: cap 72 % of the box, 1.5 px ring, track at 46 % radius, pointer rect (100-unit master: x 43.88 y 19.4 w 12.24 h 24.48 rx 6.12), readout bubble above the cap while dragging (`readout`, `radius-sm`, `shadow-pop`), 2 px `ink` focus ring 3 px off the cap |
| TitleBanner `showPresetTitle()` fade-in | moves into the Scene display, top-left |
| Sunrise on open (sky from night, knobs sweep up) | kept; the logo's sun goes (the Oberli mark is never animated) |
| PadKnob glow with layer output | the 32 x 4 level bar under each pad value |
| PresetBar: prev/next, active preset, modified mark (`isActivePresetModified()`), A/B + copy | header pill (arrows inside, click the name opens the drawer, ` *` in `accent-ink` when edited), A/B segmented and a copy-A-to-B button left of it |
| PresetBar factory pills and user presets, delete | PresetDrawer: 380 px, below the header, categories Pads / In-between / Leads / Yours on the left, search and 28 px rows on the right, current row `surface-panel` with a 6 px accent dot, one click loads and keeps it open; delete on a Yours row |
| "+ Save preset" inline name field | menu "Save preset…" (same naming flow) |
| File drop import | unchanged |
| FooterBar SIZE menu (machine-local scale) | menu "GUI scale" submenu, current choice with a 6 px accent dot |
| FooterBar product label opens About | menu "About Horizon Pad" turns the panel to its back (AboutBack, Bone theme: diagnostics, Copy details, Licences) |
| OutputMeter dBFS, 20 ms RMS, peak hold | Meter card look, same measurement |
| WheelSlider + MIDI sync (PerformanceState) | upright Slider look, same behaviour |
| Tooltips from `Params::tooltipFor` | Tooltip card: `readout` fill, 600 ms delay, never while dragging |

## Goes

- The wordmark and mountain-sunrise emblem (TitleBanner): the header carries the name.
- Per-layer colours (green, gold, cyan, pink rings and DETUNE purple): the system has one accent.
- Knob hover warming and the swelling dot glow: no glows outside the Scene.
- "4 LAYERS - 6 MACROS" in the footer, and the footer itself.

## Fonts and the mark

- Rubik, SIL Open Font License, bundled through BinaryData and registered once as a Typeface.
  JUCE needs TTF or OTF; the design system's files are woff2, so take the static TTF instances
  from Rubik's official source, unmodified (no subsetting, so the licence's naming rule never
  applies). Add the licence to `THIRD_PARTY_NOTICES.md` and the About Licences view.
- Values need tabular figures. Check whether Rubik's default figures are already tabular and
  whether JUCE 8.0.16 can switch on `tnum`; if neither, draw value digits in fixed-width cells.
  Report which.
- The header mark `oberli-bone.svg` and the About lockup `oberli-audio-charcoal.svg` come from
  the system's Logo group (asset ids `faa024af69424027373bd878a2c9a3b0` and
  `a016d21cfe48e28e718202f25b6511fa`, read by id from the design system). Embed the SVG text and
  draw it as a Drawable: vector, no bitmap, used as is.

## Decisions to get from the owner first

1. **GUI scale steps:** the Menu card has 75, 100, 150, 200 %; today also offers 125 %.
2. **Label size:** the system's `label` is 10 px; ux-ui-design-bob's render rules ask for 11 px
   minimum. Either 11 px in the build (and a system change) or a dated exception.
3. **Tagline** "Wäg zom Läbe - a life of joy": no place in the window; About page, or dropped.
4. **CLIP LED:** the Meter card has one, but the limiter ceiling is -0.04 dBFS, so it could never
   light. Leave it out (a deviation from the card) or show it, always off.
5. **A/B and copy** are not on the PluginHeader card; once agreed, add them to the card.
6. **"Report a problem…"** needs a target (the GitHub issues page, or copy the diagnostics);
   ship-bob's feedback loop decides.
7. **Window size** 1160 x 548 versus today's 1080 x 748 (sketch G keeps today's size).

## Done when (Tier P)

- The snapshot tool (`tools/gui_snapshot/`, updated for the new layout) renders F at 100, 150 and
  200 % for Lagerfeuer, Mitternachtsblau, Goldstaub, a FILTER drag, the open menu, the open
  drawer and About, and they match the sketches.
- `snapshot_contrast.py` passes on all text over the sky (4.5:1) and `--metrics` on the renders;
  the code checklist in `audio-plugins.md` 15 (plugin-ui-bob since 2026-10-27) is answered line by line
  (focus, accessible names, double-click reset, drag sensitivity, host menu, scale, modified
  mark, units).
- `ctest --test-dir build-release --output-on-failure` and
  `python3 tools/validate/validate.py --profile quick` pass (a host-facing change).
- `docs/brand-sheet.md` is rewritten from the Oberli Audio tokens (it still lists the old
  sunset palette), and CLAUDE.md's GUI section describes the new editor.
- No grade 3 or 4 findings left; anything else listed with its grade.

Work in a git worktree; parallel sessions share the main checkout, so stage explicit paths only.

## Owner decisions (Björn, 2026-10-28)

1. **GUI scale:** 75, 100, 125, 150, 200 %. Keep 125 %; the Menu card gains it.
2. **Label size:** 11 px in the build; the design system's `label` token moves to 11 px.
3. **Tagline** "Wäg zom Läbe - a life of joy": one small line on the About page, under the
   description and after the brand line "Turn it and listen.".
4. **Meter LED:** a **LIMIT** LED instead of CLIP. Outlined when idle; lit in `meter-hot` while
   the output limiter works (signal past its knee, 0.80 = -1.9 dBFS, in the 20 ms meter
   window) and 1 s after. Needs one more meter atomic published by `processBlock` beside
   `outputLevel` (audio untouched: null-test bit-identical before/after; no parameter, ID or
   state change), plus a test that it lights past the knee and stays dark on every factory
   preset. Approved by the owner as the one processor change of this work.
5. **A/B and copy** stay in the header left of the preset pill, as sketched; the PluginHeader
   card gets an optional A/B group afterwards.
6. **"Report a problem…"** copies the About diagnostics block to the clipboard and says
   "Copied. Paste it into your message to Björn." Nothing opens, nothing is sent (ship-bob
   `feedback-loop.md`: friends report by message, no GitHub Issues).
7. **Window:** 1160 x 548 at 100 %.
