# Changelog

What changed in each version of Horizon Pad, newest first. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/); versions follow SemVer,
0.y.z while friends test.

## [Unreleased]

## [0.1.0] - 2026-10-22

The first versioned build for friends.

### Added
- Presets are files now: each factory preset is a `.quellepreset` in
  `~/Library/Audio/Presets/Quelle Music/Horizon Pad/Factory` (Windows: `Documents\VST3 Presets\...`),
  rewritten when a new version first runs; your own presets live beside it in `User` and are never touched
  by an update. Presets you saved in earlier test builds move there on the first start.
- The parameter table (`parameters.csv`) generates the parameter IDs, tooltips and the manual page
  (`docs/parameters.md`).

### Changed
- **State format 2.** The ten parameters are saved in 0..100 (they were 0..1), with the format and sound
  version in every session and preset. Sessions saved with earlier test builds are migrated when they
  open; projects saved with 0.1.0 will not open correctly in older versions.
- Lagerfeuer is the Init preset and opens first in the bank.
- The host sees one program; the presets live in the plugin's own preset bar.
- The window size is remembered on your computer, no longer inside each session.
- Built with JUCE 8.0.16, whose VST 3 SDK is MIT-licensed. No sound change: all 17 factory presets null
  against the previous build within -130 dBFS.

### Fixed
- A host block larger than the size announced to the plugin rendered silence for the rest of that
  block; it now renders in full.
