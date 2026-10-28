# Brand sheet: Oberli Audio (Horizon Pad, the first plugin)

Rewritten 2026-10-28 for editor F from the Oberli Audio design system
(https://claude.ai/artifact/GK9BTMQuZhZz8pxvhHsytp, `project/tokens.json` v1 and the
component cards, rounds 1-11), in the shape of ux-ui-design-bob's `brand-sheet-template.md`.
The design system is the source; this sheet is what Horizon Pad builds from it and what was
measured. In code: `Source/gui/HorizonLookAndFeel.h` (`Tokens`, `type`, `icons`, `logo`,
`drawKnob`) and `Source/gui/HorizonScene.cpp` (the sky's palettes).

Family name: Oberli Audio (since 2026-10-27; it was Quelle Music, then for two days bob Audio)
Owner: Björn
Last changed: 2026-10-28

## Theme and accent

- **One theme per plugin, by kind:** instruments are Charcoal, effects are Bone. Horizon Pad
  is an instrument: Charcoal, built and tested in that theme only, no theme switch.
- **One accent per plugin:** Horizon Pad's is mango (#F09300), the brand's own colour; no other
  plugin takes it. Accent only on what is set or active: pointer, value arc, the selected A/B
  segment, the current-preset dot, the primary button.
- **The back of the panel (About) is the other theme:** Bone.

## Palette tokens (Charcoal)

Contrast is WCAG 2 luminance, computed from the hex values (text needs 4.5:1, control
outlines and graphical parts 3:1).

| Token | Hex | Role in Horizon Pad | Contrast |
|---|---|---|---|
| `ground` (charcoal) | #1C1B19 | window, the strip, the corners outside the sky | - |
| `surface-sunken` | #171614 | preset pill, search and name fields, meter well, level bars | - |
| `surface-raised` | #232220 | header, menu, preset drawer, Save card | - |
| `surface-panel` | #2A2826 | knob cap, wheel thumb, hovered and current rows | - |
| `line` | #3A3836 | hairlines: header bottom, group dividers, the sky's border, meter well border | decorative only |
| `line-strong` | #4A4844 | unfilled knob and wheel track | paired with the accent arc |
| `line-control` | #6B665D | outline of an operable control: knob cap ring, A/B group, preset pill, menu and drawer edges, LIMIT idle | 3.02:1 on ground, **2.79:1 on surface-raised** (open, see below) |
| `ink` (bone) | #EDE8DF | values, names, the focus ring, the title on the sky | 14.1:1 on ground, 13.0:1 on raised |
| `ink-muted` | #BDB6AA | labels, section titles, B, drawer categories | 8.6:1 on ground, 7.9:1 on raised |
| `ink-subtle` | #9C968B | version, menu trailing value, placeholders, LIMIT idle | 5.4:1 on raised, 5.0:1 on panel, 5.9:1 on ground |
| `accent` (mango) | #F09300 | pointer, value arc, selected A, current dot, primary button | 7.3:1 on ground, 6.2:1 on the cap |
| `accent-ink` | #F09300 | the edited mark " *" | 7.7:1 on the pill |
| `on-accent` | #1C1B19 | text on the accent | 7.3:1 |
| `meter-level` | #EDE8DF | meter below -6 dBFS, level bars | 14.8:1 on its well |
| `meter-hot` | #F6BF26 | meter from -6 dBFS, the lit LIMIT LED | ground text on it 10.2:1 |
| `readout` / `on-readout` | #EDE8DF / #1C1B19 | drag readout, tooltip, confirmations | 14.1:1 |
| `mod`, `clip` | #8FB8FF, #FF5A4E | not used: Horizon Pad has no modulation ranges and nothing clips | - |

Bone (About): ground #EDE8DF, surface-raised #F6F2EB, line #D3CCC0, line-control #77726A,
ink #1C1B19, ink-muted #45423E (9.0:1 on raised), ink-subtle #5C574F (5.9:1 on bone).

## The Scene (round 11)

One display per plugin may draw the sound's state with gradients, glow and blur. Horizon
Pad's is the sky (`HorizonScene`), framed as a 1104 x 294 display (`radius-lg`, 1 px `line`):
FILTER sets the time of day (night at 0 %, as designed at 50 %, golden hour at 100 %, mixed in
OKLab), REVERB the ridge's haze, WIDTH the low glow's spread. It never takes the accent. Its
palettes are in `HorizonScene.cpp`:

| Stop | Night (0 %) | As designed (50 %) | Golden hour (100 %) |
|---|---|---|---|
| top | #182032 | #4A4F43 | #514E37 |
| upper (55 %) | #34354F | #8A6A3C | #9C752E |
| lower (82 %) | #5E3640 | #B35A24 | #B45A1C |
| bottom | #8A3B26 | #CF4A1C | #CC4C16 |
| glow | #E2825A at 15 % | #F5B464 at 22 % | #FFC36A at 20 % |

Text on it (a recalled preset's name and description) is `ink` on a soft `ground` shadow (65 %,
10 px blur): `ink` alone reached 4.45:1 at the brightest corner of the range.

## Type

Rubik, bundled (Google Fonts' static instances, OFL-1.1, unmodified, `assets/fonts/`). CSS px,
which JUCE takes as the point height.

| Style | Size / line | Weight | Tracking | Use |
|---|---|---|---|---|
| `plugin-name` | 15 / 20 | 700 | - | "Horizon Pad" in the header |
| `section` | 11 / 14 | 700 | +0.12 em, caps | WHEELS, PADS, MACROS, OUTPUT, DETAILS |
| `label` | **11** / 12 | 600 | +0.1 em, caps | under knobs and wheels, RMS, LIMIT |
| `value` | 12 / 16 | 500, tabular | - | values, readout, the meter |
| `control` | 12 / 16 | 500 | - | pill, buttons, menu, drawer |
| `meta` | 11 / 14 | 500, tabular | - | the version |
| `title` | 22 / 28 | 700 | - | the preset's name on the sky |
| `small` | 13 / 19 | 400 | - | its description, About notes |
| `body` | 15 / 23 | 400 | - | About |
| `eyebrow` | 12 / 16 | 700 | +0.14 em, caps | About |
| `display` | 56 / 56 | 800 | -0.02 em | "Horizon Pad" on About |

- `label` is 11 px, not the system's 10 (owner 2026-10-28: the render rules' minimum); the
  design system's token follows. CLEARING at 11 px is wider than its 64 px column and spills
  8 px into the gaps on each side.
- Tabular figures: Rubik's default figures are proportional (0 is 635 units, 1 is 428); its
  `tnum` feature is switched on through JUCE 8's `FontOptions::withFeatureEnabled`. Minus signs
  are U+2212.

## Spacing, radii, window

- 4 px base: header sides 16, gaps between header items 12, body padding 28, sky to strip 24,
  knob columns 64 px wide and 16 apart, groups split by a 1 px `line` with 24 either side, the
  section title 16 above its controls, a label 4 under its knob, the value 8 under the label.
- Radii: `radius-xs` 4 (meter well, LED), `radius-sm` 6 (readout, tooltip, rows), `radius-md` 10
  (buttons, A/B, fields, menu), `radius-lg` 14 (the sky, About's card), `radius-pill` (preset pill).
- `shadow-pop` (0 8 24 px, black 45 %) only on what floats: menu, drawer, Save card, readout.
- Window: 1160 x 548 at 100 % (owner 2026-10-28). GUI scale 75 / 100 / 125 / 150 / 200 %, a
  machine-local preference, through `setScaleFactor` (EX-008). Everything is vector or cached
  at the screen's pixel density.

## Knobs and controls

- The Knob card, vector (`drawKnob`): cap 72 % of the box in `surface-panel` with a 1.5 px
  `line-control` ring, track and arc at 46 % radius (3 px; 2 px at `knob-sm`), the pointer from
  the 100-unit master (x 43.88 y 19.4 w 12.24 h 24.48 rx 6.12), 270 degrees from 7:30. All ten
  are `knob-md` 48 px. No hover glow.
- Interaction: vertical or horizontal drag, Shift for fine, the scroll wheel, double-click to
  the default, the host's right-click menu, Tab and the arrow keys; a click never takes keyboard
  focus. While dragging, the readout above the cap shows the parameter's own text.
- Focus: a 2 px `ink` ring 3 px off the control, on every focusable element.
- Wheels: the Slider card upright, 24 x 48 px, PITCH from a centre detent and springing back,
  MOD from the bottom and holding.
- Icons: the Icon card's thin lines (16 px grid, 1.5 px, round), `ink`, never the accent.

## Header, presets and the back of the panel

- PluginHeader: the name-only mark (`oberli-bone.svg`, 69 x 18), a hairline, "Horizon Pad";
  right: A/B segmented with copy (owner 2026-10-28; to be added to the PluginHeader card), the
  preset pill (arrows, the name, " *" when edited, opens the drawer), the version, the menu.
- Menu: Load preset…, Save preset… | GUI scale › | Report a problem… (copies the diagnostics,
  owner 2026-10-28), About Horizon Pad.
- PresetDrawer: Pads, In-between, Leads (from presets.csv), Yours; search; Export… and Show
  folder at the foot of the categories.
- About: the back of the panel in Bone, the full lockup (`oberli-audio-charcoal.svg`),
  "Turn it and listen.", the tagline "Wäg zom Läbe - a life of joy" (owner 2026-10-28), the
  DETAILS card (the fourteen diagnostics) or the licences.

## Deviations from the cards (each the owner's or measured)

| Card | Card says | Horizon Pad | Why |
|---|---|---|---|
| tokens `label` | 10 px | 11 px | owner 2026-10-28; the system's token follows |
| Menu | GUI scale 75, 100, 150, 200 % | adds 125 % | owner 2026-10-28; the Menu card gains it |
| PluginHeader | no A/B | A/B and copy left of the pill | owner 2026-10-28 |
| Meter | PEAK under the wells, a CLIP LED | RMS (each well a channel's 20 ms RMS, the number both channels'), a LIMIT LED lit while the limiter works | the measurement is RMS with a peak-hold line, as before; nothing clips, the limiter holds -0.04 dBFS (owner 2026-10-28) |
| PluginWindow | output column 64 px | 93 px | the LIMIT LED sits beside the meter, in the room the row leaves |
| About (sketch E) | "Report a bug" button | "Licences" | Report a problem… in the menu copies the same details |

## Measured on the renders (2026-10-28, `snapshot_contrast.py`, editor-f)

| Where | Result |
|---|---|
| Preset name and description on the sky, Talwind (longest description), FILTER 0 / 12 / 50 / 100 %, WIDTH 40 and 100 %, REVERB 100 %, 1x, `--tol 120` | worst 4.58:1 (description, FILTER/WIDTH/REVERB 100 %); name 5.28:1 or more; all 16 boxes AA |
| Same at 2x | golden hour 4.64:1 with `--tol 200`; at 2x the script still reads antialiased glyph edges as background at night (a "background" of #6B6C72 where the sky is #2C2D40) |
| `--metrics`, chrome (the sky masked in `ground`) | glow 0.3-0.6 %, halation 0 %, palette 2 colours, darkest surface ground: ok |
| `--metrics`, whole render | palette 18-23 colours and, on golden skies, 4.6 % saturated pixels: the Scene's gradient (round 11), not the chrome |
| Token pairs above | every text pair 4.5:1 or more; `line-control` on `surface-raised` 2.79:1 (open) |

Open, for the design system: `line-control` reaches 3:1 on `ground` but 2.79:1 on
`surface-raised`, where the header's A/B group and preset pill sit. Their letters, arrows and the
selected fill identify them; a `line-control` a step lighter for raised surfaces (#716B62 gives
3.0:1) would close it. Grade 2.

## Check before a release
- [ ] Token pairs recomputed when a token changes.
- [ ] Renders at 100 / 150 / 200 % (`HorizonPadGuiSnapshot --scale`) through `snapshot_contrast.py`,
      the sky's title at FILTER 0, 12, 50 and 100 % (`--title`).
- [ ] Nothing in the plugin overrides a token without a line in this sheet.
