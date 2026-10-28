# Failures - Horizon Pad

One row per fixed defect (sound-design-bob D.8.3, D.9.3), newest first, in the format of the
track failure libraries. Started 2026-10-29 from the regression tests and the records they
cite; a lesson that generalises also goes into the playbook's library, and the row stays.

| Symptom | Cause | Confirming measurement | Fixed in | Regression test |
|---|---|---|---|---|
| The Windows build fails (C2440) | A lambda inside a lambda capturing `this` in an init-capture: MSVC reads `this` as the outer lambda (`PresetDrawer`, delete button) | CI `Build (Windows)` on `3aa75fd` | `335d0cc`, 2026-10-28 | CI's Windows build |
| Editor outlines too faint on the header | `line-control` #6B665D reached 3.02:1 on ground but 2.79:1 on `surface-raised` | token contrast (WCAG 2), render pixel read | `8a14117`, 2026-10-28 (#716B62) | `docs/brand-sheet.md` token pairs |
| The silence test fails now and then on CI | One render's block time judged on a shared runner | the test's timing on CI | `d4da383`, 2026-10-27 (best of three) | `silence_after_note` |
| The VST3 wrapper answers MIDI-mapping queries out of range | A missing bounds check in JUCE 8.0.16's VST3 wrapper | UBSan trap; the Steinberg validator logs it as Info | 2026-10-25, `cmake/PatchJuce.cmake` | validator step of `validate.py` (fails on that Info line) |
| The plugin would not load on older macOS | CI built for macOS 14.0 (the runner's default deployment target) | each slice's minimum macOS by `otool`: 14.0 | 2026-10-22 (10.13 / 11.0) | `vst3_runs_on_macos_10_13` |
| Silence after the prepared length in a large host block | Layers and FX clamped to the block size `prepareToPlay` announced | a gap three quarters long at 4x the announced size (harness `--max-block`) | 2026-10-22 | `larger_block_than_prepared` |
| Pads swell and fade like a film score | Designed envelopes too long for worship playing | 10-90 % swell 1.7-2.7 s before, worst 0.92 s / 1.15 s after (A-009) | 2026-10-14 | `presets_have_worship_envelopes`, `attack_release_span` |
| Expanse's shimmer too bright at the top of the keyboard | The shimmer send flat above C5 | at C6, shimmer octave against the stack octave -13.1 dB, -22.1 dB with the roll-off | 2026-10-07 | `expanse_shimmer_recedes_at_top` |
| Root's bass notes start as a dull hum | The cutoff opening with the envelope, tracked into the bass | early centroid at C2, median of five seeds: 99 Hz, 146 Hz fixed | 2026-10-07 | `root_bass_starts_open` |
| The preset bar named program 0 while the defaults played | The bank was reordered; program 0 no longer equalled the parameter defaults | compared by value at construction | 2026-10-07 | `startup_program_matches_defaults` |
| Stereo bass under the reverb | The side high-pass ran before the send, so the reverb return rebuilt a side channel below the split | side -5.3 to -5.6 dB under the mid on Sternenzelt and Bergecho (`tools/measure/translation.py`) | 2026-10-03 | `bass_is_mono` |
| The voice-steal test failed about 2 runs in 10 on a correct build | The worst of six unseeded renders; the step metric swings 10 dB with saw phase | seeds 1-12: steal against free median -1.8 dB fixed, +7.9 dB without the declick | 2026-10-03 (seeded) | `voice_steal_declick` |
| Bloom's tremolo vanishes in chords | A random tremolo phase per voice averaged the pulse away | four-note chord: depth 44 % to 16 % | sound-design v2 (`05c3f44`), shallower 2026-10-07 | `bloom_tremolo_survives_chords` |
| Root 2.5 dB louder at REVERB 100 % | The bass-dry split was a two-pole complement, not power-complementary | loudness REVERB 0 against 100 %, three seeds | sound-design v2 (`05c3f44`) | `reverb_level_flat` |
| A click when a sounding voice is stolen | The envelope hard-reset mid-cycle on slot takeover | worst of eight runs stepped +3.8 dB relative to the signal | `bf17ee8`, 2026-09-24 | `voice_steal_declick` |
| The bass register rough, Expanse a drone when played low | Filters fixed in Hz, full unison detune everywhere | C1 Root roughness 40x its C4; Clearing's C2 triad 9.0 (Vassilakis); Expanse at C2 a 131 Hz drone | sound-design v2 (`05c3f44`), register pass | `low_register_stays_musical`, `keyboard_level_span` |
| Expanse loses its level up the keyboard | Every filter fixed in Hz around C4 | Expanse spanned 14.7 LU, -57 LUFS at C7 | sound-design v2 (`05c3f44`) | `keyboard_level_span` |
| WIDTH's image changes with the sample rate | WIDTH was a Haas delay of a flat 90 samples | image and mono comb moved by 4.3x between 44.1 and 192 kHz | sound-design v2 (`05c3f44`), oscillator spread | `width_is_rate_invariant` |
| Expanse's shimmer has no octave | `OctaveShimmer`'s grain read ran backwards at unity speed | the octave partial 73 dB down at C4 (G5) | `bf17ee8`, 2026-09-24 | `shimmer_octave_present` |
| DC offset when the output limiter works | The odd-symmetric limiter shaves the asymmetric mix unevenly | -58.6 dBFS at the extreme, -80.3 dBFS worst of six with the 2 Hz blocker (EX-001) | 2026-09-23 | `output_dc_and_knee` |
