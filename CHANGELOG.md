# Changelog

What changed in each version of Horizon Pad, newest first. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/); versions follow SemVer,
0.y.z while friends test.

## [Unreleased]

## [0.1.0] - 2026-10-22

The first versioned build for friends.

### Added
- Presets are files you can share: Browse in the preset row lists every preset by kind (Pad, In-between, Lead) and yours, imports a .bobpreset file a friend sent you (or an older .quellepreset), and exports your current sound as one.
- An About box (click the version in the bottom left) shows the version and copies the details to paste into a report.
- The window comes in five sizes, up to 200 %, and every Horizon Pad on your computer opens at the size you chose last.
- Factory presets are written to `~/Library/Audio/Presets/bob Audio/Horizon Pad/Factory` (Windows: `Documents\VST3 Presets\...`) when a new version first runs; your own presets live in `User` beside it and an update never touches them. Presets saved in earlier test builds move there on the first start.
- The parameter table (`parameters.csv`) generates the parameter IDs, the tooltips and `docs/parameters.md`.

### Changed
- **Horizon Pad is made by bob Audio** (it was Quelle Music). The DAW lists it under bob Audio as a new plugin, so projects saved with earlier test builds no longer find it: insert it again and pick your sound from the preset row. Your own presets and the window size are copied from the old `Quelle Music` folders on the first start. Preset files are now `.bobpreset`; older `.quellepreset` files still import.
- **State format 2:** the ten parameters are saved in 0..100 (they were 0..1), with the format and sound version in every session and preset; projects saved with 0.1.0 will not open correctly in older versions.
- Lagerfeuer is the Init preset and opens first in the bank.
- The active preset's pill is darker, with its gold border and a cream name, so it reads on every sky (gold on gold was hard to read).
- The host sees one program; the presets live in the plugin's own preset row.
- Built with JUCE 8.0.16, whose VST 3 SDK is MIT-licensed. No sound change: all 17 factory presets null against the previous build within -130 dBFS.

### Fixed
- The macOS plugin needed the newest macOS of the machine that built it (14.0 from the release build); it runs on macOS 10.13 and newer again.
- A host block larger than the size announced to the plugin rendered silence for the rest of that block; it now renders in full.
- A VST3 host asking which parameter a MIDI controller beyond the 130 the plugin maps belongs to (program change, poly pressure) got a wrong answer read from past the end of a table; it now hears that there is none.
