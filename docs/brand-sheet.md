# Brand sheet: Quelle Music (Horizon Pad, the first plugin)

Filled from ux-ui-design-bob's `brand-sheet-template.md` on 2026-10-22 (Round 14, U.2),
with Horizon Pad's own values: they are the family's until a second plugin asks for a
change. Source of truth for the numbers: `Source/gui/HorizonLookAndFeel.h` (`Palette`)
and `Source/gui/HorizonScene.h` (the sky palettes and their measured contrast).

Family name: Quelle Music (open until the first brand release)
Owner: Björn
Last changed: 2026-10-22

## Palette tokens (dark, the only theme)

Contrast measured with WCAG 2 luminance, text on the card colour `cardBg` unless noted.

| Token (template) | Horizon Pad name | Hex | Role | Contrast |
|---|---|---|---|---|
| `bg` | `background` | #020306 | window edge behind the panel | - |
| `surface` | `cardBg` | #10141B | the eight cards | - |
| (panel) | sky gradient | #4A4F43 → #8A6A3C → #B35A24 → #CF4A1C | the sunset behind the cards (HorizonScene, follows FILTER) | - |
| `line` | `knobTrack` | #2A2E36 | unlit knob track | 1.36:1 vs `cardBg` (the lit arc and dot carry the value) |
| `ink` | `text` / `textKnobLabel` | #EBEFF5 / #E4E8EF | wordmark, card labels | 16.0:1 / 15.0:1 |
| `ink` (values) | `textValue` | #CACED4 | card value readouts | 11.7:1 |
| `muted` | `textDim` / `textFaint` | #7F8793 / #79818D | captions, tagline, secondary buttons | 5.1:1 / 4.7:1 |
| `accent` | `gold` | #F5AE39 | active preset border and fill, wheel thumbs, the logo's sun (the active pill's text is `presetTitle` cream) | 9.7:1 |
| per layer | Root / Clearing / Expanse / Bloom | #5BBD74 / #F0BB3B / #2FB5D8 / #ED76B3 | VOL rings and glows | 7.9 / 10.4 / 7.7 / 6.9:1 |
| per macro | Detune (the one not shared with a layer) | #A98BF0 | DETUNE ring | 6.7:1 |
| `modified` | `gold` at half alpha | - | the active preset after an edit | - |
| (footer) | `textFooter` | #F3E6C8 | footer strip on the sky | 3.2-3.8:1 by palette, 5.9:1 at night (EX-009) |
| `clip` | meter fill high | #FAC547 | the meter's top, no separate clip marker | - |
| meter | `meterFillLow` on `meterBg` | #B37903 on #040609 | output meter | 5.5:1 |

## Type
| Role | Face | Size at 1x | Weight | Notes |
|---|---|---|---|---|
| Label (card, macro, footer) | the system sans (JUCE default) | 10-13 px | Bold, caps | macro labels 10 px, footer 12.5 px |
| Value | the system sans | 11-13.5 px | Regular | |
| Card title | the system sans | 17 px | Bold | |
| Title (wordmark, preset title) | the system serif (`getDefaultSerifFontName()`), italic | 36 px | Regular | large text |

Font licence: none bundled - the editor uses the operating system's fonts, so no OFL file
ships (`notices.toml` lists no font). The template's "one bundled OFL face" is not met; a
bundled face is a design decision for the owner (it would make Windows and macOS match).

## Spacing, radii, window
- Spacing and corner radii are set per component in `Source/gui/` (pixel-accurate to the
  design handoff), not yet as shared tokens; the panel itself is square (it fills the window).
- Default window: 1080 x 748 logical px; SIZE 75 / 100 / 125 / 150 / 200 %, machine-local.

## Knobs and controls
- Vector ring knobs drawn in `HorizonLookAndFeel`, buffered as images at the screen's density.
- Sizes: one large VOL ring per pad card, six small rings in the MACROS card (layout in
  `PadKnob` and `MacrosPanel`).
- Anatomy: track in `knobTrack`, arc in the layer's or macro's colour from 7 o'clock, a dot at the
  end, the value under the knob; hover and drag warm the arc. Double-click resets, the host's
  right-click menu, Tab and arrow keys (since 2026-10-17).
- Buttons: pills with a 1 px border, gold when active; A/B and copy as round icon buttons.

## Preset bar and hero
- Preset bar under the wordmark: previous/next arrows, the pills, "+ Save preset", A/B and copy.
- Hero: the sunset panel (HorizonScene) - FILTER sets the time of day, REVERB the haze, WIDTH the glow.
- Logo: the sun over two peaks, gold, centred above the italic wordmark.

## Measured on the renders (2026-10-22, `snapshot_contrast.py`, 1x, Init preset)

| Where | Worst | Median | Verdict |
|---|---|---|---|
| Footer left, "HORIZON PAD 0.1.0 · VST3" | 3.54:1 | 4.81:1 | below 4.5:1 at the sky's brightest row (EX-009) |
| Inactive pill "Talwind" | 4.99:1 | 6.86:1 | AA |
| Active pill "Lagerfeuer", cream text since 2026-10-22 (was gold, 3.19:1) | - | 5.02:1 | AA at the default sky; 3.85:1 on Goldstaub's bright sky (EX-010) |
| Metrics: glow 1.1 %, halation 0 %, darkest surface #101018 | - | - | ok |
| Metric: palette colours | 18-19 | - | over 12: the sky gradient's steps count as colours |

Boxes holding tall letters or umlauts ("Alpenglühen", "Frostklang", "+ Save preset") read a
light grey "background" right above the glyph rows (1.4-1.7:1) while their median is 6.9:1:
the script appears to take the antialiased ascenders for background - reported as a probable
skill-tool bug, not counted here.

## Check before a release
- [ ] Palette pairs above recomputed when a token changes.
- [ ] Renders at 100 / 150 / 200 % (`HorizonPadGuiSnapshot --scale`) through `snapshot_contrast.py`.
- [ ] Nothing in the plugin overrides a token without a line in this sheet.
