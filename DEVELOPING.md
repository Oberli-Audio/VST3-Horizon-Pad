# Horizon Pad - developer notes

For building, testing and changing Horizon Pad. The friends' page is `README.md`.


A four-layer ambient pad synthesiser, built with JUCE and shipped as a VST3
instrument (a Standalone build is also produced for quick testing outside a
host).

Everything Horizon Pad makes is synthesised from scratch in DSP code. There is
no sample content, no audio files and no bitmap artwork anywhere in this
repository — the oscillators, the shimmer/pitch-shift engine and the entire
user interface (including the sunset banner and the mountain range) are
generated in code.

---

## The four layers

| # | GUI name | Class | Synthesis |
|---|----------|-------|-----------|
| 1 | **Root** | `WarmFoundationLayer` | Three detuned triangle oscillators + a band-limited (polyBLEP) saw edge + a sub-oscillator one octave down, run through a lowpass filter with a slow "breathing" cutoff modulation |
| 2 | **Clearing** | `AnalogEnsembleLayer` | A multi-oscillator unison stack summed with its own output through an LFO-swept delay line (2–11 ms, 0.11 Hz), i.e. a chorus/flanger built from the sweep itself rather than a fixed comb, into a lowpass |
| 3 | **Expanse** | `AiryChoirLayer` | A detuned oscillator stack through a swept bandpass, with a parallel shimmer send: highpass → a two-grain +1-octave pitch shifter (`OctaveShimmer`, the C++ equivalent of a granular `transpose`) → its own small reverb, blended back in |
| 4 | **Bloom** | `MotionPadLayer` | A detuned oscillator stack through a lowpass with an LFO-modulated cutoff and a tremolo, for a pad that visibly "breathes"/moves over time |

All four layers respond to MIDI note on/off and the sustain pedal (CC64) with
shared voice allocation (up to 8 voices, four-tier stealing: free slot →
oldest released-but-ringing slot → oldest pedalled note → oldest held key), so
a chord sounds coherent across every layer.
Each pad has its own designed attack and release (1.2–2.6 s). The pad presets
play them at worship length — swelling within a beat and clearing a chord
change, a third of the designed times — and the ATTACK/RELEASE macros span
from a fast, click-free ~20 ms up to the full designed times (see below).

Stereo width comes from one **WIDTH** macro. Each layer turns it into its own
width through a fixed profile, so the pads open in order as the knob turns:
Expanse (the air) from 0%, Clearing from 10%, Bloom from 15%, and Root (the
foundation) last, from 20%; at full WIDTH every pad reaches its full spread. The
width itself spreads each layer's detuned oscillators across the stereo field
with constant-power panning, mirrored on alternate voices so a chord's
oscillators interleave. Detuned oscillators are decorrelated, so the spread is
mono-safe by construction and does not change the level. The bass stays mono regardless: the side channel is high-passed at
140 Hz before the reverb, and the reverb itself only receives the band above
160 Hz. Every layer is also key-tracked and register-aware (see
`Source/dsp/LayerBase.h`), so it holds its character from the bass to the top
of the keyboard. The instrument's register is **C0 to C7** (MIDI 12-96), set by
listening: outside it the sound was no longer musical, so keys below C0 and above
C7 make no sound at all (`horizon::kRegisterLowestNote`, brief A-010).

## Global FX chain

Post layer-mix: a single shared **reverb send** (`Source/dsp/FxChain.{h,cpp}`,
`juce::dsp::Reverb`, driven genuinely in stereo). REVERB (0–100%) is the wet
send level; raising it also trims the dry level slightly (0.85× down to
~0.60×) so it reads as a wet/dry blend rather than a pure loudness increase.
A final `tanh` soft clip on the output is a safety net for pathological
automation, not part of the normal sound (headroom is trimmed well ahead of
it — factory presets peak in the −9 to −21 dB range on a 4-note chord).

There is no separate delay or global filter stage — FILTER is a macro that
scales each layer's own already-envelope-modulated cutoff curve instead (see
Parameters below), and each layer's own oscillator/filter chain provides its
own movement.

---

## Parameters

Ten host-automatable parameters (all 0–100% of their travel; ATTACK and
RELEASE read as a time), in this fixed order — four
volumes, then six macros — so that a controller mapping its first 8 knobs to
a plugin's first 8 host parameters (e.g. a Launchkey in Ableton Live's generic
Device-knob mode) lands knobs 1–4 on the pad volumes and knobs 5–8 on
ATTACK/RELEASE/FILTER/REVERB:

| # | Parameter | ID |
|---|-----------|-----|
| 1 | Root volume | `rootVolume` |
| 2 | Clearing volume | `clearingVolume` |
| 3 | Expanse volume | `expanseVolume` |
| 4 | Bloom volume | `bloomVolume` |
| 5 | Attack | `attackMacro` |
| 6 | Release | `releaseMacro` |
| 7 | Filter | `filterMacro` |
| 8 | Reverb | `reverbMacro` |
| 9 | Width | `widthMacro` |
| 10 | Detune | `detuneMacro` |

- **Attack / Release** scale each layer's own designed attack/release time
  on one exponential curve: from 1% of it at the left end (a fast,
  click-free ~10–25 ms) to the designed time itself at the right. Every 10%
  of the knob multiplies the time by 1.58, so the short times get most of
  the travel. Knob and host show **Root's time** ("0.64 s", "22 ms"; Root
  goes to 2.20 s attack and 2.40 s release); Bloom is about half as long,
  Clearing and Expanse a little longer on the attack. You can type a time
  ("0.5", "500 ms") in the host.
- **Filter** scales each layer's own cutoff-over-time curve (darker below
  50%, brighter above), ramped per sample rather than stepped once per
  block, so sweeping it under host automation doesn't zipper.
- **Reverb** is the shared reverb send level (see FX chain above).
- **Width** is the stereo width of the whole instrument, 0% mono, applied to
  each pad through its own profile (see above).
- **Detune** scales how far apart each pad's stacked oscillators are tuned:
  their static detune and their slow pitch drift. 50% is the designed sound
  (bit-exact); 0% halves the spread, 100% doubles it. It eases out below C3,
  so low notes stay stable, and it leaves Root's saw edge and sub-octave and
  the MOD wheel's extra drift alone. Clearing responds least, because its
  ensemble adds pitch movement of its own.

All ten parameters are smoothed (a `SmoothedValue` ramp, not a raw value
applied instantaneously) somewhere on their path to audio, to avoid zipper
noise under host automation.

### Performance controls (not host parameters)

**PITCH** and **MOD**, matching a real keyboard's wheels: on-screen
draggable, and also driven by incoming MIDI pitch-bend / mod wheel (CC1).
PITCH is spring-loaded (snaps back to centre on release); MOD stays wherever
it's left. These are intentionally *not* automation lanes — see
`Source/dsp/PerformanceState.h`.

### The editor

- **Knobs** drag vertically, **Shift** for fine steps; while you drag, the
  value shows above the knob. **Double-click** resets to the default;
  **right-click** opens the host's own menu for that parameter (automation,
  MIDI mapping) - in the standalone, a "Reset to default" menu. **Tab**
  walks every control, the arrow keys move the focused one; a mouse click
  never takes keyboard focus, so the space bar always reaches the host's
  transport.
- **Presets**: the header's pill names the preset; its arrows step through
  the factory presets and then your own, and " *" after the name means you
  have changed the sound since. Click the name for the preset drawer: Pads,
  In-between, Leads and Yours, a search, and Export… for a file to give a
  friend. One click loads a preset and leaves the drawer open, so you can
  audition one after another; Esc or a click outside closes it.
- **A / B** (header): two versions of the sound side by side; the arrows
  button next to them copies the one you hear onto the other.
- **OUTPUT** reads in dBFS (RMS over 20 ms; the line holds the recent peak).
  It is a reference, not a control. **LIMIT** lights while the output
  limiter holds the sound under -0.04 dBFS; no factory preset reaches it.
- **Menu** (header, right): Load preset…, Save preset…, GUI scale (75 / 100
  / 125 / 150 / 200 %), Report a problem… (copies the details a report
  needs), About Horizon Pad. The GUI scale belongs to your computer: every
  Horizon Pad opens at the size you chose last, whichever project it is in.
- The sky follows the sound: FILTER sets the time of day, REVERB hazes the
  ridge, WIDTH spreads the glow; each pad's bar shows its own layer's level.
  The first time the editor opens on an instance the sky rises from night;
  after that it opens as it is.

### A/B buffers and user presets

Two independent snapshots (**A** / **B**) of all ten parameters, switchable
and copyable from the preset row — handy for comparing a tweak against where
you started. Beyond the factory presets below, **"+ Save preset"** writes to
a small on-disk library (`UserPresets.xml` next to the plugin's app-data
folder) shared across every instance of the plugin, independent of the
host's own per-track program list.

---

## Factory presets

**17** factory programs (`Source/presets/Presets.cpp`) in three families: **pads**
(swelling in ~0.6-1.2 s and clearing a chord change - a third of the original design's
envelope times since 2026-10-14), **in-between** (pads that speak in 0.2-0.45 s - at
home in harmonies) and **leads** (onset 15-165 ms, release 0.2-0.6 s with the room).
They are ordered in **repeating cycles**, each from the longest attack to the
shortest, so browsing never jumps from a slow pad straight to a lead; the first cycle
holds the five most different sounds in the bank. The plugin opens on **Lagerfeuer**
(#2), the default patch. Method and numbers:
[`docs/preset-curation-2026-09-23.md`](docs/preset-curation-2026-09-23.md).

All are loudness-matched to -16.9 LUFS (the bank median on the same chord
measurement): 16 of 17 within +/-0.6 LU; Sternenzelt sits ~1.7 LU lower, an accepted
exception confirmed by ear (EX-002 in [`docs/status.md`](docs/status.md), Open exceptions).
Every preset is checked by the test suite (`all_presets_safe`) for clipping, NaN/Inf
and peaks above 0 dBFS on an eight-note chord.

| # | Preset | Family | Character |
|---|--------|--------|-----------|
| 1 | Alpenglühen | pad | Warm light spreading wide across the peaks |
| 2 | Lagerfeuer | pad | Warm, close and grounded - the campfire pad (the default patch) |
| 3 | Talwind | in-between | Movement and breeze - a moving synth that answers in a breath |
| 4 | Frostklang | in-between | Cold, bright and sharp - a frozen ring that speaks at once |
| 5 | Funkenflug | lead | Bright saw lead - sparks flying, cuts through the band |
| 6 | Steinerne Ruhe | pad | Stillness carved in stone - minimal, slow and sparse |
| 7 | Bergecho | pad | A vast mountain echo - huge, cinematic space |
| 8 | Kupferglanz | in-between | Warm copper shine - a present poly synth that speaks quickly |
| 9 | Silberpfad | lead | Soft, singing lead to play over the pads |
| 10 | Mitternachtsblau | pad | A deep midnight drone, barely lit |
| 11 | Ruhepuls | pad | A slow resting pulse, with subtle motion underneath |
| 12 | Sturmfront | in-between | A dramatic storm front - big, wide chords that hit sooner |
| 13 | Glasperle | lead | Glassy bell lead - plucks bright, then sings softer |
| 14 | Sternenzelt | pad | Vast and celestial - Expanse fills the whole sky |
| 15 | Dämmerlicht | pad | Warm dusk light, gently settling |
| 16 | Goldstaub | pad | Golden dust catching the light - bright and airy |
| 17 | Klarheit | in-between | Clear, present and simple - quick enough for rhythm parts |

Each preset's full ten-value parameter set lives in
[`Source/presets/Presets.cpp`](Source/presets/Presets.cpp).

---

## Building

Requirements: **CMake ≥ 3.22** and a C++17 compiler. JUCE 8.0.16 is pulled in
automatically by CMake `FetchContent` — there is no submodule to initialise and
nothing to install first. Configure applies one local fix to it,
[`cmake/PatchJuce.cmake`](cmake/PatchJuce.cmake): JUCE's VST3 wrapper answered a host's
question about MIDI controller 130 and above by reading past an array (found under
UBSan, 2026-10-25; unfixed upstream on that date).

```bash
git clone https://github.com/Oberli-Audio/VST3-Horizon-Pad.git
cd VST3-Horizon-Pad

cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --target HorizonPad_VST3 --config Release
```

The bundle lands at:

```
build/HorizonPad_artefacts/Release/VST3/Horizon Pad.vst3
```

### macOS

```bash
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --target HorizonPad_VST3
cp -R "build/HorizonPad_artefacts/Release/VST3/Horizon Pad.vst3" ~/Library/Audio/Plug-Ins/VST3/
```

Or, for local dev iteration, `tools/install_plugin.sh` builds the Release
config and copies it straight to `~/Library/Audio/Plug-Ins/VST3/` (the
per-user folder every major DAW, including Ableton Live, scans — no admin
privileges or password prompt needed). `tools/install_plugin.sh --debug`
installs a Debug build for a debugger; it is several times heavier on the
audio thread, so don't read a DAW's CPU meter with it.

The `.vst3` is a **universal binary** (arm64 + x86_64 in one bundle) by
default — CMake sets `CMAKE_OSX_ARCHITECTURES="arm64;x86_64"` automatically
unless you override it. For a faster single-arch dev build, pass your own
architecture explicitly, e.g. `-DCMAKE_OSX_ARCHITECTURES="arm64"`.

### Windows

```powershell
cmake -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --target HorizonPad_VST3 --config Release
```

Then copy the `.vst3` folder to `C:\Program Files\Common Files\VST3\`.

### Faster iteration with a local JUCE checkout

```bash
cmake -B build -DFETCHCONTENT_SOURCE_DIR_JUCE=/path/to/JUCE
```

Configure does not modify your checkout: it stops if the checkout lacks the local fix
and prints the command that applies it (`cmake -DJUCE_DIR=/path/to/JUCE -P cmake/PatchJuce.cmake`).

### Offline DSP sanity checks

`HorizonPadSoundTool` (`cmake --build build --target HorizonPadSoundTool`)
renders the real DSP offline — no audio device, no host, no editor — and
prints peak/RMS/dB, NaN/Inf/clipping detection, DC offset, envelope and
spectral analysis as JSON. Run `./build/HorizonPadSoundTool --help` for the
full flag list (choosing a preset, soloing a layer, overriding any parameter,
rendering at any sample rate/block size, writing a WAV for manual listening).
This is the fastest way to verify a DSP change didn't introduce clipping,
NaN/Inf, or a stepping/zipper artifact, without opening a DAW.

### Offline GUI snapshots

`HorizonPadGuiSnapshot` (`cmake --build build-release --target HorizonPadGuiSnapshot`,
not built by default) renders the real editor offscreen to PNG, with real
audio behind its meters: any preset or parameter state, held notes, a preset
recall at a chosen moment, frame sequences of an animation, and a sheet of
the ring knob's hover and drag states. It echoes the rendered state as JSON.
Run it with `--help`.

---

## Testing and validating

The project runs at playbook **Tier P**; [`docs/status.md`](docs/status.md)
is the one-page view of where each gate stands (with the open exceptions, the
listening debt, the handoff and the decisions), [`docs/failures.md`](docs/failures.md)
lists every fixed defect with its regression test, and the gate artefacts sit beside
them in [`docs/`](docs/).

### Test suite

```bash
cmake --build build-release --target HorizonPadSoundTool -j 8
ctest --test-dir build-release --output-on-failure
```

Twenty-five tests, about a minute, driving the real plugin DSP through
`HorizonPadSoundTool` ([`tools/tests/dsp_tests.py`](tools/tests/dsp_tests.py)).
Five assert end-to-end properties — zero reported latency, bit-identical
renders from a seeded reset, all 36 sample-rate × block-size combinations
clean, no NaN or denormal storm after 60 s of silence, all 17 factory presets
safe. The rest guard a bug that measurement caught, a layer's defining
feature, or an instrument-level contract:

| Test | Guards against |
|---|---|
| `shimmer_octave_present` | Expanse's +1 octave shimmer silently not transposing |
| `width_is_rate_invariant` | WIDTH's stereo image changing with the sample rate |
| `voice_steal_declick` | Voice stealing cutting a sounding voice mid-cycle |
| `reverb_level_flat` | REVERB changing the loudness (a non-power-complementary bass split did) |
| `root_sub_present` | Root's sub-octave missing |
| `clearing_ensemble_present` | Clearing's ensemble missing or mono |
| `bloom_tremolo_survives_chords` | Bloom's tremolo averaging away in chords |
| `keyboard_level_span` | A layer vanishing away from C4 |
| `width_mono_safe_and_level_flat` | WIDTH collapsing in mono or changing the level |
| `sustain_pedal_holds` | The sustain pedal being ignored |
| `low_register_stays_musical` | Roughness in the bass register; Expanse leaving its register |
| `register_is_c0_to_c7` | A key outside the register C0-C7 making a sound, or C0/C7 themselves falling silent |
| `bass_is_mono` | Stereo content below ~100 Hz (at WIDTH and DETUNE 100%) |
| `width_profile_staggered` | WIDTH opening every pad at once instead of air first, foundation last |
| `detune_spreads_every_stack` | DETUNE not reaching a pad's stack, or changing the level |
| `attack_release_span` | ATTACK/RELEASE leaving their curve: 0% fast, 50% a tenth, 100% the designed times |
| `presets_have_worship_envelopes` | A preset swelling or fading like a film score (> 1.2 s / 1.4 s, dry) |

The suite needs `python3` with `numpy` and `scipy`. Without them CMake skips
test registration, so a plain plugin build never fails for want of them.

### Measurement tools

[`tools/measure/`](tools/measure/) holds the G3/G5 measurement scripts — alias
floor by sample-rate comparison, the metric battery, reverb decay per octave
band, and the port-vs-prototype null test. Each prints a markdown table and is
runnable on its own. `descriptors.py` measures whether it is the sound that was
designed (layer contrast, keyboard range, mix pocket, stereo, movement, preset
bank) against the baselines in [`docs/baselines/`](docs/baselines/), and
`register.py` measures how musical each layer stays over the whole register, C0 to C7.
The sound tool's `--profile` reports worst-case block time against the
deadline (rule 37), and `--bend`, `--mod` and `--automate` drive the wheels
and host-style automation through the real MIDI and parameter paths.
[`tools/listening/make_session.py`](tools/listening/make_session.py) builds a
blind, loudness-matched A/B/X listening page comparing two builds.

### Plugin-format validators

Both pass against the current build (2026-09-24):

```bash
# pluginval — strictness 10, exit 0
pluginval --strictness-level 10 --validate "build-release/HorizonPad_artefacts/Release/VST3/Horizon Pad.vst3"

# Steinberg validator — 47 tests passed, 0 failed
git clone --depth 1 https://github.com/steinbergmedia/vst3sdk.git
cd vst3sdk && git submodule update --init --depth 1 -- base cmake pluginterfaces public.sdk && cd ..
cmake -B vst3sdk-build -S vst3sdk -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DSMTG_ENABLE_VST3_PLUGIN_EXAMPLES=OFF -DSMTG_ENABLE_VSTGUI_SUPPORT=OFF \
  -DSMTG_ENABLE_VST3_HOSTING_EXAMPLES=OFF
cmake --build vst3sdk-build --target validator
./vst3sdk-build/bin/Release/validator "build-release/HorizonPad_artefacts/Release/VST3/Horizon Pad.vst3"
```

On a Mac with only the Command Line Tools installed, the SDK's configure step
fails its Xcode version check. Add `-DXCODE_VERSION=16.0` and set
`XCODE_VERSION=16.0` in the environment — it needs both, the cache variable
for the comparison and the environment variable to skip the `xcodebuild`
probe.

### AddressSanitizer and UBSan (release gate)

Before every tag the plugin runs under AddressSanitizer and UndefinedBehaviorSanitizer
(playbook D.1.6; EX-006, closed 2026-10-26, in [`docs/status.md`](docs/status.md), Decisions). The
`asan` CMake preset builds the plugin and `HorizonPadSoundTool` with
`-fsanitize=address,undefined -fno-omit-frame-pointer` into `build-asan/` (RelWithDebInfo,
arm64 only, no LTO).

pluginval has to be built from source with ASan — the signed download cannot load the
sanitizer runtime. Build it once, outside this repository, at the version pinned in
[`tools/validate/versions.json`](tools/validate/versions.json):

```bash
git clone --branch v1.0.4 --depth 1 https://github.com/Tracktion/pluginval.git pluginval-asan
cd pluginval-asan && git submodule update --init --depth 1
cmake -B Builds/asan -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_CXX_FLAGS="-fsanitize=address -fno-omit-frame-pointer" \
  -DCMAKE_C_FLAGS="-fsanitize=address -fno-omit-frame-pointer" \
  -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=address" .
cmake --build Builds/asan --config Release --target pluginval -j 8
# -> Builds/asan/pluginval_artefacts/Release/pluginval.app/Contents/MacOS/pluginval
```

Then, back in this repository (each step must be clean - any sanitizer report stops it):

```bash
cmake --preset asan && cmake --build build-asan --target HorizonPad_VST3 HorizonPadSoundTool
export ASAN_OPTIONS=halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1

# the Steinberg validator and pluginval L10 at six sample rates (timeouts scaled 10x)
caffeinate -i env PLUGINVAL=/path/to/pluginval-asan/Builds/asan/pluginval_artefacts/Release/pluginval.app/Contents/MacOS/pluginval \
  python3 scripts/dsp-kit/validate_plugin.py --profile full --sanitizer asan \
  --plugin "build-asan/HorizonPad_artefacts/RelWithDebInfo/VST3/Horizon Pad.vst3"

# the DSP test suite against the ASan sound tool
caffeinate -i env HORIZON_PRESET_ROOT="$PWD/build-asan/test-presets" \
  python3 tools/tests/dsp_tests.py --tool ./build-asan/HorizonPadSoundTool
```

The Steinberg validator is not built with ASan, so `validate_plugin.py` inserts the runtime
the plugin links into the validator process alone (on macOS, Python is an Apple binary,
which may not load it). On Windows, CI's `asan-windows` job builds the sound tool with
MSVC's `/fsanitize=address` (MSVC has no UBSan; CMake copies the runtime DLL beside it) and
runs the test suite against it.

### The Faust prototype

`sound design/four_pads.dsp` is the sound-design source of truth the C++ was
ported from, and the layer classes cite it by name. It builds into an offline
renderer that needs no audio device:

```bash
./tools/faust_render/build.sh
./build/faust/four_pads_render --dur=10 --gate-off=4 --freq=261.63 --out=blend.wav
```

`sound design/g5_layers.dsp` exposes the same four layers dry, one per output
channel, for the null test. It imports them with Faust's `library()` primitive
rather than copying them, so it cannot drift from the prototype.

### Known gaps

- **Two hosts, two platforms (Tier P) is 1 of 2.** Ableton Live 10.1 under
  Rosetta on the Mac is confirmed (editor F tried by the owner 2026-10-28);
  Ableton Live on Windows is outstanding, the last open item in G6. A third
  host, Reaper, belongs to Tier F.
- **The CPU budget is not yet checked on its own machine.** On an Apple M3
  Pro, eight voices at every pad's full volume take 6% of the block deadline
  at p99 (64 and 128 samples, 2026-10-17); the budget is written for a
  2017-era dual-core i5, roughly 3-4x slower. See A-004 in
  [`docs/brief.md`](docs/brief.md).
- **Listening items are open** - the ends of the register (C0, C7) per
  layer, and Bloom's movement since DETUNE's default went to 0%. The list
  and the blind A/B page that plays them are in
  [`docs/status.md`](docs/status.md), Listening debt.
- **Open exceptions**, accepted with numeric triggers: EX-002 (Sternenzelt sits
  below the loudness-matched bank), EX-004 (block-size dependence), EX-005 (the
  hand-written parameter layout), EX-008 (scaling through `setScaleFactor`). See
  [`docs/status.md`](docs/status.md).
- **Code-signing / notarization**: the macOS `.pkg` is unsigned (no Apple
  Developer ID on this project) — Gatekeeper will warn on first launch (see
  Installing below for the workaround). This needs the project owner's own
  Apple Developer credentials to resolve; out of scope for a code change.
- **No free resizing**: the window scales in five steps (GUI scale in the menu, 75-200%),
  not by dragging its corner.
- 64-bit (double-precision) audio processing is not implemented —
  `supportsDoublePrecisionProcessing()` is not overridden, so it defaults to
  `false`; hosts running a double-precision graph will process this plugin
  in single precision.

---

## Installing

Prebuilt installers are produced by CI on every push to `main` (see
[Continuous integration](#continuous-integration) below) - download them from
the latest successful **Build** workflow run's Artifacts.

### macOS - `Horizon Pad.pkg`

Double-click and follow the prompts; it installs to
`/Library/Audio/Plug-Ins/VST3/Horizon Pad.vst3` (system-wide, so it asks for
your password). The package is **unsigned and not notarized** - there's no
Apple Developer ID on this project yet - so Gatekeeper will warn on first
launch.

What that warning looks like depends on the macOS version:

- **If a dialog offers "Open" or "Open Anyway" directly** (older macOS): click
  it and continue.
- **If the dialog only offers "Move to Trash" and "Cancel"** (current macOS -
  Sonoma/Sequoia and later removed the direct bypass button): click
  **Cancel** - do *not* move it to the Trash - then go to
  **System Settings > Privacy & Security**, scroll down to the "Security"
  section, and you'll see *"Horizon Pad.pkg" was blocked to protect your Mac*
  with an **Open Anyway** button next to it. Click it, confirm once more in
  the popup that appears, then double-click the `.pkg` again to install.

The CI artifact ships `INSTALL-INSTRUCTIONS.txt` right next to the `.pkg`
with these same steps spelled out - handy since it's a plain text file with
no Gatekeeper warning of its own, so it's always readable even before you've
gotten past the warning on the `.pkg` itself.

To uninstall, delete
`/Library/Audio/Plug-Ins/VST3/Horizon Pad.vst3` yourself - `pkgbuild`
packages don't register an uninstaller.

The `.pkg` carries a `postinstall` script (`packaging/macos/scripts/postinstall`)
that runs on your Mac, with the same admin privileges as the install itself,
right after Installer.app copies the plugin into place. It re-signs the
installed bundle ad-hoc and verifies it (the true last step of the whole
distribution chain, after every possible copy/zip/extract along the way),
and - importantly - exits with a failure if anything is missing or invalid.
Installer.app only shows "The installation was successful" when that script
actually confirms the plugin landed correctly; a broken install now shows a
real failure dialog instead of a false "success".

To build the `.pkg` yourself from a local build:

```bash
packaging/macos/build-pkg.sh "build/HorizonPad_artefacts/Release/VST3/Horizon Pad.vst3" dist
```

### Windows - `install.bat`

Unzip the Windows installer package (it contains `Horizon Pad.vst3`,
`install.bat` and `uninstall.bat` together - keep them side by side) and
double-click `install.bat`. It self-elevates (one UAC prompt) and copies the
plugin to `C:\Program Files\Common Files\VST3\`. Run `uninstall.bat` the same
way to remove it. There's no signed installer here either - Windows
SmartScreen may still show a warning the first time; click **More info > Run
anyway**.

### Both platforms at once

The `HorizonPad-CrossPlatform-Release` CI artifact bundles the macOS `.pkg`
and the Windows installer folder together in one zip, for handing a single
file to someone who might be on either OS.

---

## Continuous integration

[`.github/workflows/build.yml`](.github/workflows/build.yml):

1. **`build`** - builds the VST3 on macOS (universal binary) and Windows
   (x64) runners, uploads each as a raw artefact.
2. **`package-macos`** - turns the macOS build into an unsigned `.pkg`
   (`packaging/macos/build-pkg.sh`).
3. **`package-windows`** - bundles the Windows build with
   `packaging/windows/install.bat` / `uninstall.bat` into an installer
   folder.
4. **`package-release`** - zips both installers together into one
   `HorizonPad-CrossPlatform-Release` artifact.
5. **`validate`** - runs the Steinberg validator against the macOS build, in
   parallel, non-blocking (`continue-on-error: true`).

---

## Repository layout

```
CMakeLists.txt                     JUCE CMake API, FetchContent, juce_add_plugin
Source/
  PluginProcessor.{h,cpp}          APVTS, voice allocation/stealing, state,
                                    A/B buffers, programs
  PluginEditor.{h,cpp}             1160x548 layout (editor F, scaled by GUI scale): header,
                                    sky, control row, overlays; the frame loop
                                    that drives every animation
  dsp/
    HorizonTypes.h                 kNumLayers/kMaxVoices/kNumGlobalParams etc.
    PerformanceState.h             Lock-free PITCH/MOD (not host parameters)
    LayerBase.{h,cpp}              Shared voice/envelope/filter/width machinery
    WarmFoundationLayer.{h,cpp}    Root
    AnalogEnsembleLayer.{h,cpp}    Clearing
    AiryChoirLayer.{h,cpp}         Expanse
    MotionPadLayer.{h,cpp}         Bloom
    OctaveShimmer.h                Two-grain +1-octave pitch shifter (Expanse's shimmer send)
    FxChain.{h,cpp}                Shared stereo reverb send
  presets/
    Presets.{h,cpp}                17 factory programs (9 pads, 5 in-between, 3 leads) + parameter IDs
    UserPresetStore.{h,cpp}        On-disk user preset library (message-thread only)
  gui/                             Editor F, the Oberli Audio design system (docs/editor-f-brief.md)
    HorizonLookAndFeel.{h,cpp}     Tokens, Rubik, icons, the Oberli marks, the knob painter
    Buttons.{h,cpp}                Icon and text buttons, the focus ring
    HorizonScene.{h,cpp}           The sky: FILTER sets the time of day,
                                    REVERB the haze, WIDTH the glow's spread
    SkyDisplay.{h,cpp}             The sky's framed display, the recalled preset's title
    Motion.h                       Spring and follower used by every animation
    AnimatedKnob.{h,cpp}           The Knob card's knob, gliding to new values
    KnobColumn.{h,cpp}             A knob with its label, value and (pads) level bar
    WheelSlider.{h,cpp}            PITCH/MOD wheels
    OutputMeter.{h,cpp}            Output RMS in dBFS and the LIMIT LED (read-only)
    ControlStrip.{h,cpp}           The row: WHEELS, PADS, MACROS, OUTPUT
    PluginHeader.{h,cpp}           Mark, name, A/B and copy, preset pill, version, menu button
    HeaderMenu.{h,cpp}             The menu, with the GUI scale submenu
    PresetDrawer.{h,cpp}           Presets by category, search, your own, export
    SavePresetCard.{h,cpp}         "Save preset…": the name field
    ReadoutBubble.{h,cpp}          A knob's drag readout; short confirmations
    AboutPanel.{h,cpp}             The back of the panel: details, licences
assets/                            The editor's bundled data (UiData): Rubik (OFL), the Oberli marks (SVG)
tools/
  install_plugin.sh                Local dev build+install to ~/Library/Audio/Plug-Ins/VST3
  sound_tool/Main.cpp              HorizonPadSoundTool - offline DSP render+analysis CLI
  gui_snapshot/Main.cpp            HorizonPadGuiSnapshot - the editor rendered offscreen to PNG
  tests/dsp_tests.py               The CTest suite (G6)
  measure/                         G3/G5 measurement scripts
    alias_check.py                 Alias floor, by 48 vs 192 kHz comparison
    g3_metrics.py                  Rate/block matrix, CPU, envelope, denormals
    reverb_edr.py                  Reverb decay per octave band
    null_test.py                   Port vs prototype, per layer
  faust_render/                    Offline renderer for the Faust prototype
    render_arch.cpp                Faust architecture file: headless WAV out
    build.sh                       faust + c++ -> build/faust/*_render
sound design/
  four_pads.dsp                    The Faust prototype - sound-design source of truth
  g5_layers.dsp                    The same four layers, dry, for the null test
  Horizon_Pad_VST3_Handoff.md      Control-to-GUI mapping from the original handoff
  reference-renders/               Signed-off renders: per layer + the full blend
docs/
  status.md                        One page: gates, listening debt, open exceptions, handoff, decisions
  failures.md                      Every fixed defect: symptom, cause, measurement, regression test
  brief.md                         G0 sonic brief + dated amendments
  g1-algorithm.md                  G1 algorithm audit against the playbook
  g2-prototype.md                  G2 prototype artefact
  g3-measurements.md               G3 metric table
  g5-null-test.md                  G5 port-vs-prototype characterisation
  preset-curation-2026-09-23.md    How the bank went from 30 to 18
  dsp-audit-2026-09-20.md          DSP invariant audit
packaging/
  macos/build-pkg.sh               Builds the unsigned .pkg installer
  macos/scripts/postinstall        Runs on-Mac after install: re-signs + verifies
  macos/INSTALL-INSTRUCTIONS.txt   Gatekeeper walkthrough, shipped beside the .pkg
  windows/install.bat              Self-elevating installer (Common Files\VST3)
  windows/uninstall.bat            Self-elevating uninstaller
.github/workflows/build.yml        Build matrix, installer packaging, validator job
```

---

© Oberli Audio. All rights reserved.
