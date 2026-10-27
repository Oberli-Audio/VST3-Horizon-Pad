# Horizon Pad — VST3 Handoff

Oberli Audio · ambient pad synth · **plugin built, compiled, and committed.**

Tagline: *"Wäg zom Läbe — a life of joy"*

GUI reference (live, click-through mockup): https://claude.ai/artifact/JfHpmQcecSdVZXkESLfwVQ
DSP source of truth (design reference — the shipped plugin is a manual C++ port of this, not a `faust2juce` build): `four_pads.dsp`
Audio references: `four_pads_full_blend_v5.mp3` (all four layers together) plus one solo file per layer (`pad1_warm_foundation.mp3`, `pad2_analog_ensemble_v2.mp3`, `pad3_airy_choir_v3.mp3`, `pad4_motion_pad.mp3`)
Repo: `github.com/Oberli-Audio/VST3-Horizon-Pad` (the account was `bjoernoberli` until 2026-10-26) — built plugin delivered as a git bundle (`horizon-pad-vst3-build.bundle`) alongside `HOW_TO_APPLY.md`, since this session's own `git push` is blocked by its repo-authorization policy; merge and push the bundle from your machine.

---

## 1. What this plugin is

A one-oscillator-section-per-mood pad synth: four independent pad layers, each with its own oscillators, envelope, and filter, blended purely by volume. Nothing shares a signal path between layers, so any combination fades in cleanly rather than mudding up — the four are deliberately spread across brightness and attack speed (see `four_pads.dsp` header for the full design rationale, drawn from *Synth Secrets* and *How To Make A Noise*).

| Layer | Character | Role | C++ class |
|---|---|---|---|
| Warm Foundation | dull, slow attack, low-mid body | the grounded "verse" layer | `WarmFoundationLayer` |
| Analog Ensemble | bright, medium attack, chorused mid (custom LFO-swept flanger) | the classic string-machine layer | `AnalogEnsembleLayer` |
| Airy Choir | very bright, very slow, high/air, long tail (shared shimmer bus: resonant bandpass → highpass → octave-up pitch shift → reverb) | the "digital texture" / shimmer layer | `AiryChoirLayer` |
| Motion Pad | medium brightness, medium attack, built-in tremolo + filter pulse | the "it's alive" layer | `MotionPadLayer` |

## 2. GUI (built exactly to the Claude Design draft)

Dark, warm-amber gradient background (dusk/sunset feel), serif italic wordmark "Horizon Pad", small mountain-sunrise glyph as the logo. One knob row, left to right:

- **Preset row**: five named presets — *Lagerfeuer, Alpenglühen, Morgentau, Sternenzelt, Talwind* — plus a disabled "+ Save preset" placeholder, and an **A/B** slot pair with a swap arrow (presentational for now — both slots mirror the same live state; independent snapshot storage is a follow-up).
- **PITCH** ("bend the sky") — vertical wheel, ±2 semitones, wired to MIDI pitch-bend as well as mouse drag.
- **MOD** ("a soft wind") — vertical wheel, 0–100%, wired to MIDI CC1 (mod wheel) as well as mouse drag.
- **Four big rotary knobs, one per pad layer** (all real host-automatable parameters):
  - **ROOT** (green) — "life grows" → Warm Foundation level
  - **CLEARING** (amber) — "light breaks in" → Analog Ensemble level
  - **EXPANSE** (blue) — "life opens up" → Airy Choir level
  - **BLOOM** (pink) — "life blooms" → Motion Pad level
- **MACROS** — four small knobs, two rows: **ATTACK** / **FILTER** (top), **WIDTH** / **REVERB** (bottom) — global tone-shaping controls that act across all four layers at once. (**WIDTH replaces the originally-planned DRIVE** — see "Design decision" below.)
- **OUTPUT** — vertical live level meter (read-only; a consequence of the mix, not a control).
- Below the knob row: the on-screen playable keyboard (**A S D F G H J K**, mouse + real keyboard), with the caption *"Click and hold, or play A–K on your keyboard. Eight knobs mirror a Launchkey 25 — four blend the pads, four shape the tone. Pitch and mod ride the wheels."*
- Footer: "HORIZON PAD · VST3" / "BUFFER A · 4 LAYERS · 8 MACROS", with a fixed-seed vector mountain-skyline silhouette.

## 3. Design decision: WIDTH instead of DRIVE

The GUI draft's fourth macro was originally labelled DRIVE, but `four_pads.dsp` never implemented saturation, and every pad layer in the plugin is inherently mono (matching the Faust design — nothing in the signal path pans). Rather than ship a knob with no effect, **WIDTH replaces DRIVE**: `FxChain` keeps every layer's output identical on both channels (exactly as the Faust source does) and creates the only real stereo difference by delaying the right channel alone through a short modulated tap (0–90 samples, smoothed), scaled by the WIDTH macro. At 0% the plugin is genuinely mono; at 100% it's the widest the effect gets without phase artifacts. This was chosen over DETUNE and SHIMMER-AMOUNT (the two other candidates considered) because width is audible on every layer at once and needs no per-layer plumbing.

## 4. Parameter map: GUI control → implementation

| GUI control | Parameter ID | Implementation |
|---|---|---|
| ROOT | `rootVolume` | `WarmFoundationLayer` output level |
| CLEARING | `clearingVolume` | `AnalogEnsembleLayer` output level |
| EXPANSE | `expanseVolume` | `AiryChoirLayer` output level |
| BLOOM | `bloomVolume` | `MotionPadLayer` output level |
| PITCH | *(performance wheel, not a host parameter)* | `PerformanceState::pitchBendSemitones`, ±2 st, read by every layer's oscillators once per block |
| MOD | *(performance wheel, not a host parameter)* | `PerformanceState::modAmount`, 0–1 (currently reserved for a future "movement depth" macro — not yet wired into the DSP; the wheel is fully functional and MIDI-synced, ready for that hookup) |
| ATTACK (macro) | `attackMacro` | scales every layer's ADSR attack time by `2^((v-0.5)*2)` — 50% = exactly the original validated Faust timing |
| FILTER (macro) | `filterMacro` | scales every layer's filter cutoff/brightness by the same `2^((v-0.5)*2)` curve, 50% = unchanged |
| WIDTH (macro) | `widthMacro` | `FxChain`'s right-channel delay tap, 0–90 samples |
| REVERB (macro) | `reverbMacro` | `FxChain`'s shared reverb send level, default 28% |
| OUTPUT | *(read-only meter)* | post-mix RMS, smoothed for display |
| A/B, presets | `AudioProcessorValueTreeState` program state | `Source/presets/Presets.cpp`, 5 factory programs |

## 5. Presets (final values)

Root / Clearing / Expanse / Bloom / Attack / Filter / Width / Reverb, all 0–100%:

- **Lagerfeuer** (campfire — warm, close, grounded): 75 / 30 / 15 / 25 — attack 40, filter 30, width 40, reverb 20
- **Alpenglühen** (alpenglow — warm light spreading): 45 / 65 / 55 / 35 — attack 60, filter 65, width 80, reverb 45
- **Morgentau** (morning dew — fresh, delicate): 30 / 40 / 70 / 55 — attack 50, filter 55, width 70, reverb 55
- **Sternenzelt** (starry sky — vast, celestial, night): 15 / 15 / 90 / 25 — attack 75, filter 80, width 100 (full stereo), reverb 75
- **Talwind** (valley wind — movement, breeze): 40 / 40 / 40 / 75 — attack 30, filter 50, width 85, reverb 35

These are a first ear-tuning pass, not final mix values — worth revisiting once you're auditioning the real plugin in a DAW rather than the Faust prototype.

## 6. What's built vs. what's left

**Built and compiled clean** (verified with a full CMake+Ninja Release build, JUCE 8.0.4 via FetchContent, on this session's Linux environment): the complete DSP port (all four layers, the shared shimmer bus, `FxChain`), all 8 host parameters, MIDI pitch-bend/mod-wheel handling, the on-screen keyboard, all 9 GUI components matching the mockup, and the 5 factory presets. The resulting `Horizon Pad.vst3` exports the required `GetPluginFactory`/`ModuleEntry`/`ModuleExit` symbols.

**Not yet done, because this environment has no DAW or audio device:**
- A real load-and-play test in an actual host (Ableton, Logic, Reaper, etc.) — please do this before considering the plugin finished. Watch in particular for: overall gain staging (the output trim was set by ear against the original Faust renders, not against the compiled plugin), the Airy Choir shimmer bus's CPU cost at high polyphony, and whether the WIDTH effect is audible enough at low settings.
- macOS/Windows builds — only Linux was compiled here; the existing `.github/workflows/build.yml` should build both unchanged, but hasn't been run against this commit yet.
- Wiring MOD into the DSP (currently a fully functional, MIDI-synced wheel with no audio effect yet — see the parameter map above).
- "+ Save preset" (button is present but disabled) and independent A/B snapshot storage (currently both slots mirror the same state).

## 7. Reference files

- `four_pads.dsp` — the validated 4-layer Faust prototype this plugin's DSP was ported from
- `four_pads_full_blend_v5.mp3` — all four layers blended at the prototype's default levels
- `pad1_warm_foundation.mp3`, `pad2_analog_ensemble_v2.mp3`, `pad3_airy_choir_v3.mp3`, `pad4_motion_pad.mp3` — each layer solo'd
- Older iteration audio has been moved to `_Archive/` in the sound design folder — kept for history, not needed for the build
