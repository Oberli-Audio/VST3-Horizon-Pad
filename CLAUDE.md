# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

Horizon Pad (Oberli Audio; Quelle Music until 2026-10-25, briefly bob Audio until 2026-10-27; GitHub account Oberli-Audio) — a four-layer ambient pad synthesiser VST3/Standalone
plugin, built with JUCE 8.0.16 (C++17, pinned by commit; 8.0.11+ bundles the MIT VST 3 SDK). Everything is synthesised in DSP code:
no samples, audio files, or bitmap artwork anywhere in the repo — oscillators,
the shimmer/pitch-shift engine, and the entire GUI (including the sunset
banner and mountain range) are drawn/generated in code.

## Build commands

JUCE is pulled in automatically via CMake `FetchContent` — no submodule init needed.
Configure applies one local fix to it, `cmake/PatchJuce.cmake` (a bounds check in the
VST3 wrapper's MIDI mapping, found under UBSan 2026-10-25, unfixed upstream), and stops
if a JUCE bump removes the line it patches.

```bash
# Configure + build the VST3 (macOS, Ninja)
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --target HorizonPad_VST3

# Windows
cmake -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --target HorizonPad_VST3 --config Release
```

Bundle output: `build/HorizonPad_artefacts/Release/VST3/Horizon Pad.vst3`
(Debug config → `.../Debug/VST3/...`).

For local dev iteration on macOS, `tools/install_plugin.sh` builds Release
(`build-release/`) and copies straight to `~/Library/Audio/Plug-Ins/VST3/`
(per-user folder, no admin password prompt, safe to run unattended).
`--debug` installs the Debug build instead - only for a debugger: it costs
~6x the audio-thread time of Release (measured 2026-10-10), so never judge a
host's CPU meter on it.

Faster iteration against a local JUCE checkout instead of re-fetching:
`cmake -B build -DFETCHCONTENT_SOURCE_DIR_JUCE=/path/to/JUCE`. Configure only checks such a
checkout for the patch and prints the command that applies it.

### Offline DSP sanity/analysis tool

```bash
cmake --build build --target HorizonPadSoundTool -j 8
./build/HorizonPadSoundTool --help
```

`HorizonPadSoundTool` (`tools/sound_tool/Main.cpp`) links directly against the
real plugin code and renders offline — no audio device, host, or editor
needed. It prints one JSON object to stdout with peak/RMS levels, NaN/Inf/
clipping/click detection, DC offset, envelope trajectory, and spectral
analysis. This is the fastest way to verify a DSP change didn't introduce
clipping, NaN/Inf, or zipper artifacts, without opening a DAW. Key flags:
`--solo=root|clearing|expanse|bloom`, `--notes=60`, `--preset=<name>`,
`--param=<id>=<value>`, `--out=<wav path>`, `--list-presets`. Applied in
order preset → solo → param, each overriding the last.

### The Faust prototype

`sound design/four_pads.dsp` is the sound-design source of truth every layer
class cites by name, and `sound design/g5_layers.dsp` exposes the same layers
dry for null testing (via Faust's `library()`, so it holds no copied DSP).
`./tools/faust_render/build.sh` compiles either into a headless offline WAV
renderer — no audio device needed. `sound design/reference-renders/` holds the
signed-off renders to A/B against. These are design references, not product
assets: nothing under `sound design/` is compiled, linked or loaded at runtime.

### Validating a structural change

```bash
git clone --depth 1 https://github.com/steinbergmedia/vst3sdk.git
cd vst3sdk && git submodule update --init --depth 1 -- base cmake pluginterfaces public.sdk && cd ..
cmake -B vst3sdk-build -S vst3sdk -DCMAKE_BUILD_TYPE=Release \
  -DSMTG_ENABLE_VST3_PLUGIN_EXAMPLES=OFF -DSMTG_ENABLE_VSTGUI_SUPPORT=OFF \
  -DSMTG_ENABLE_VST3_HOSTING_EXAMPLES=OFF
cmake --build vst3sdk-build --target validator
./vst3sdk-build/bin/Release/validator "build/HorizonPad_artefacts/Release/VST3/Horizon Pad.vst3"
```

**One command for the whole host-compatibility run** (playbook G6, D.5.7):

```bash
python3 tools/validate/validate.py                   # full: build, DSP tests, Steinberg validator,
                                                     # pluginval L10 at 6 rates x 7 blocks, RTSan suite
python3 tools/validate/validate.py --profile quick   # L5, for any host-facing change
python3 tools/validate/validate.py --profile soak    # nightly/Tier F: L10 x5 randomised, fresh logged seed
python3 tools/validate/validate.py --only pluginval --seed 0x...   # reproduce a fuzz failure
ctest --test-dir build-release -L validation         # the validators as CTest tests
```

Logs and `summary.json` land in `build-validation/<timestamp>/`. The Steinberg
validator is built once from the VST3 SDK commit pinned in
`tools/validate/versions.json` (with the Command Line Tools only, the script sets
`XCODE_VERSION`); pluginval is found in /Applications or fetched with
`--fetch-pluginval`. First full run 2026-10-06: all pass in ~2 min (pluginval L10 16 s);
CI runs both on every push to main (L10, both platforms). The validator step also fails
when the MIDI mapping answers an out-of-range controller: the validator logs that only as
Info, but it is the JUCE defect `cmake/PatchJuce.cmake` fixes.

### Tests

```bash
cmake --build build-release --target HorizonPadSoundTool -j 8
ctest --test-dir build-release --output-on-failure
```

Twenty-five tests (~70 s), `tools/tests/dsp_tests.py`, registered by CMake (plus the two validators, label `validation`). They
drive the real DSP through `HorizonPadSoundTool` rather than unit-testing
classes: latency, seeded-reset determinism, the 36 sample-rate x block-size
combinations, 60 s of silence, all 17 presets; regression guards, one per bug
measurement has caught (`shimmer_octave_present`, `width_is_rate_invariant`,
`voice_steal_declick`, `reverb_level_flat`); a presence test per defining
feature (`root_sub_present`, `clearing_ensemble_present`,
`bloom_tremolo_survives_chords`, `width_profile_staggered`,
`detune_spreads_every_stack`); and instrument-level contracts
(`keyboard_level_span`, `width_mono_safe_and_level_flat`, `sustain_pedal_holds`,
`low_register_stays_musical`, `bass_is_mono`, `attack_release_span`,
`presets_have_worship_envelopes`, `register_is_c0_to_c7`); and the control path
through real MIDI and host-style automation (`velocity_curve`, `pitch_bend_range`,
`mod_wheel_adds_movement`, `automation_is_zipper_free`, rule 30). **Add a regression test whenever a
DSP defect is fixed, and show it failing on the build that has the defect**
(a `git worktree` of the previous commit). Needs python3 + numpy + scipy; CMake
skips registration if they are missing, so a plain plugin build never fails
without them.

`tools/measure/` holds the measurement scripts: G3/G5 (alias floor, metric
battery, reverb decay, port-vs-prototype null test), `descriptors.py` (is it the
sound we designed: layer grid, keyboard, mix pocket, stereo, movement, presets -
four seeds per figure, `--save`/`--compare` against `docs/baselines/`) and
`register.py` (roughness, pitch salience, harmonic content and low side energy
per note over the register C0-C7, and low voicings), and `translation.py` (playbook F.7 / rule 34:
mono sum, phone speaker, -14 LUFS normalisation, AAC/MP3 round trip, stereo bass;
baseline `docs/baselines/translation-v2.json`; uses the playbook's `dspkit` from
`~/bob-skills/sound-design-bob/scripts/dsp-kit` (override with `DSPKIT`), plus `afconvert` and `lame`). `tools/listening/make_session.py` renders a
blind, loudness-matched A/B/X page (baseline build vs candidate) for the
listening passes the measurements cannot settle.

The tool's `--param` flag is repeatable (it silently kept only the last one
before 2026-09-26 - G5 was re-run, see `docs/g5-null-test.md`); the applied
state is echoed under `activeParams`, and scripts should assert on it.
`--pedal=down,up` sends the sustain pedal.

**Real-time safety (rule 35).** `-DHORIZON_RTSAN=ON` builds the sound tool with
RealtimeSanitizer; every `processBlock` then runs as a real-time context, and any
allocation, lock or system call aborts with a stack trace. Needs Homebrew LLVM (Apple
clang has no RTSan) and, as found with JUCE 8.0.4 (not yet re-checked on 8.0.16), two workarounds - the SDK's libc++ headers
and a private JUCE copy without one deprecated constructor clang 23 rejects (playbook
D.1.6):

```bash
SDK=$(xcrun --show-sdk-path)
cp -R build-release/_deps/juce-src /tmp/juce-rtsan   # then delete the two-line
#   template AudioPluginInstance (const short channelLayoutList[numLayouts][2]) constructor
#   in modules/juce_audio_processors/processors/juce_AudioPluginInstance.h
cmake -B build-rtsan -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo -DHORIZON_RTSAN=ON \
  -DCMAKE_C_COMPILER=/opt/homebrew/opt/llvm/bin/clang -DCMAKE_CXX_COMPILER=/opt/homebrew/opt/llvm/bin/clang++ \
  "-DCMAKE_CXX_FLAGS=-nostdinc++ -isystem $SDK/usr/include/c++/v1" -DFETCHCONTENT_SOURCE_DIR_JUCE=/tmp/juce-rtsan
cmake --build build-rtsan --target HorizonPadSoundTool
export RTSAN_OPTIONS=external_symbolizer_path=/opt/homebrew/opt/llvm/bin/llvm-symbolizer  # default symbolizer hangs
HORIZON_RTSAN_SELFTEST=1 ./build-rtsan/HorizonPadSoundTool --notes=60 --hold=1   # must abort (proves the check is live)
python3 tools/tests/dsp_tests.py --tool ./build-rtsan/HorizonPadSoundTool       # all 19 clean, 2026-10-03
```

**Memory and UB safety (D.1.6 release gate, EX-006).** The `asan` preset (`build-asan/`,
Ninja, RelWithDebInfo, arm64, no LTO, no dSYM step) sets `HORIZON_ASAN`: the plugin, its
JUCE modules and the sound tool built with `-fsanitize=address,undefined
-fno-omit-frame-pointer` (MSVC: `/fsanitize=address`, no UBSan). The malloc trap stays in -
it allocates through `malloc_default_zone()`, which ASan replaces, so the two coexist (a
render is clean and the trap's self-test still aborts, 2026-10-25). pluginval must be
built from source with ASan (DEVELOPING.md); the signed download cannot load the runtime.
Before every tag, all clean:

```bash
cmake --preset asan && cmake --build build-asan --target HorizonPad_VST3 HorizonPadSoundTool
export ASAN_OPTIONS=halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1
caffeinate -i env PLUGINVAL=<pluginval-asan>/Builds/asan/pluginval_artefacts/Release/pluginval.app/Contents/MacOS/pluginval \
  python3 scripts/dsp-kit/validate_plugin.py --profile full --sanitizer asan \
  --plugin "build-asan/HorizonPad_artefacts/RelWithDebInfo/VST3/Horizon Pad.vst3"
caffeinate -i env HORIZON_PRESET_ROOT="$PWD/build-asan/test-presets" \
  python3 tools/tests/dsp_tests.py --tool ./build-asan/HorizonPadSoundTool
```

The Steinberg validator is not built with ASan, and ASan will not start when a host only
`dlopen`s it ("Interceptors are not working"), so `validate_plugin.py` (since
sound-design-bob 4.16.0) inserts the runtime the plugin links into the validator process
alone - Python, an Apple binary, may not load it ("Sanitizer load violates platform policy").

### CI

`.github/workflows/build.yml`: builds VST3 on macOS (universal binary) +
Windows (x64), packages an unsigned (ad-hoc signed) macOS `.pkg` and a Windows
installer folder, zips both into one cross-platform release artifact, runs the
DSP test suite (`ctest -LE validation`), and validates the **downloaded artefacts** on
macOS and Windows with `tools/validate/validate.py`: the Steinberg validator from the
pinned SDK commit and pluginval L10 at all six sample rates - both blocking (G6),
logs uploaded per platform. `asan-windows` builds the sound tool with MSVC
`/fsanitize=address` and runs the DSP tests against it, blocking, and the draft release
waits for it (the Windows row of the sanitizer matrix; the Mac's ASan run is local, above).
Not yet in CI: the RTSan job (needs Homebrew LLVM and the JUCE workarounds above).

## Architecture

### Signal path

`PluginProcessor` owns four independent `LayerBase`-derived synth layers plus
one shared `FxChain`. Each `processBlock` call: MIDI is turned into centralized
voice allocation (see below), each layer renders into its own scratch buffer
(`layerBuffer`) with its own oscillators/filter/envelope, the four layer
outputs are summed with per-layer smoothed gain, then the mix passes through
`FxChain` (a single shared stereo `juce::dsp::Reverb` send) and a final `tanh`
soft-clip safety net.

### The four layers (`Source/dsp/`)

| GUI name | Class | `LayerIndex` | Character |
|---|---|---|---|
| Root | `WarmFoundationLayer` | `warmFoundation` | Detuned triangle stack + polyBLEP saw edge + sub-osc (−1 oct), lowpass with "breathing" cutoff |
| Clearing | `AnalogEnsembleLayer` | `analogEnsemble` | Unison saw stack into a lowpass, then a wet-only stereo 3-tap ensemble on the layer bus (0.6 + 5.5 Hz, bass below 200 Hz bypasses it) |
| Expanse | `AiryChoirLayer` | `airyChoir` | Detuned triangle stack through swept bandpass + parallel shimmer send (highpass → `OctaveShimmer` 2-grain +1-octave pitch shift → its own stereo reverb); below C4 pinned near C5 (register pinning) |
| Bloom | `MotionPadLayer` | `motionPad` | Detuned stack through lowpass with LFO-modulated cutoff + tremolo — deliberately never static |

Each layer's class doc comment is the authoritative description of its
design intent (why triangle vs saw, why a filter opens with the envelope,
detune amounts, etc.) — read it before changing behavior; don't "fix"
something the comment explains is deliberate. The `sound design/` folder at
the repo root may hold additional design notes/references.

`LayerBase.h` is the shared machinery all four layers inherit: fixed
(non-user-editable) per-layer ADSR timings from the original Faust sound
design, the global macros applied to every layer each block
(`attackTimeScale`, `releaseTimeScale`, `brightness` — ramped per-sample via
`smoothedBrightness` to avoid filter zipper noise — plus WIDTH and DETUNE),
stereo WIDTH (one macro, turned into each layer's own width by its fixed
`widthProfile()` - Expanse opens from 0%, Clearing 10%, Bloom 15%, Root 20%, all
reaching full spread at 100% (since 2026-10-07; Root stopped at 60% before); the width spreads each voice's detuned oscillators across the
field, constant-power, mirrored on odd voices; mono-safe and level-flat - it
replaced a Haas tap on 2026-09-26 and four per-pad WIDTH knobs on 2026-10-02),
DETUNE (`makeDetuneRamp()`/`driftDepth()`: scales each stack's static detune
and baseline drift, 0.5x-2x with 50% bit-exact, eased out below C3; Root's saw
edge and sub and the MOD wheel's drift are not scaled), and the register helpers every layer uses:
`keyTrack` (asymmetric key tracking anchored at C4, the voicing note),
`unisonFor` (partners and drift tighten below C3), smoothed-random `Drift`,
`polyBlampTriangle`. `FxChain.h/.cpp` holds the shared reverb send: a one-pole
split at 160 Hz keeps the bass dry and only the band above is sent (20 ms
pre-delay) and crossfaded equal-power; the side channel of the finished mix -
dry and reverb return - is high-passed at 140 Hz, 24 dB/oct, **last** (mono bass,
rule 26; it ran first until 2026-10-03, and the reverb return rebuilt stereo bass).

### The register

C0-C7 (MIDI 12-96; C4 = 60). Set by the owner's listening (brief A-010, 2026-10-17):
outside it the sound is not musical, so `noteOn` ignores the key - no voice, no sound.
`horizon::kRegisterLowestNote` / `kRegisterHighestNote` in `HorizonTypes.h`; every
measurement that sweeps notes (`register.py`, `alias_check.py`, the listening items)
stays inside it.

### Voice allocation

Centralized in `PluginProcessor`, not per-layer: up to `kMaxVoices` (8) voice
slots, four-tier stealing (free slot → oldest released-but-ringing slot →
oldest note held only by the sustain pedal → oldest held key). CC64 holds
released keys until pedal-up; striking a key that is already sounding releases
its old voice first. Every layer is told about the same note in the same voice
slot each block, so a chord stays coherent across all four layers.

### Parameters and thread-safety model

Ten host-automatable APVTS parameters (all 0-100, raw values in the host and
the state since state version 2) in fixed order: 4 volumes, 6 macros
(Attack/Release/Filter/Reverb/Width/Detune, `MacroIndex`). `parameters.csv` is
the source: it generates `Source/Params.h` (IDs, tooltips, smoothing) and
`docs/parameters.md`; the layout itself is hand-written (EX-005) and checked
against it by `parameter_table_matches_plugin`. DEVELOPING.md's Parameters
section has the full semantics of each macro; the friends' README is D.9.9's. All are smoothed
(`juce::SmoothedValue`) somewhere on their path to audio.

- Host parameters live in an `AudioProcessorValueTreeState`; audio thread
  reads them via cached `std::atomic<float>*` pointers (no string lookups,
  no locks).
- PITCH/MOD are performance controls (`Source/dsp/PerformanceState.h`), not
  host parameters — two atomics written by MIDI pitch-bend/CC1 or the
  on-screen wheel, read once per block. PITCH is spring-loaded; MOD holds.
- A/B buffers (two full 10-parameter snapshots) stay in sync via an APVTS
  `Listener::parameterChanged()` callback, which can fire from any thread.
- User presets (`UserPresetStore`, `UserPresets.xml`) are message-thread-only
  — created/read/deleted exclusively from GUI actions, so no extra sync is
  needed.
- For the editor, the audio thread publishes output and per-layer RMS over a
  20 ms window (atomics; the mix itself is untouched, null-tested
  bit-identical) and the processor bumps a preset-recall and an A/B-switch
  counter (release/acquire, after the active-preset fields are set) so the
  editor can tell a recall from automation.

See the class doc comment at the top of `Source/PluginProcessor.h` for the
full thread-safety model before touching processor state.

### GUI (`Source/gui/`)

A 1080×748 layout, pixel-accurate to a design handoff — every child paints
its own precise layout rather than scaling a shared "design surface"; the
footer's SIZE menu scales the whole editor 75-200% with a transform on top of
the host's own scale (`setScaleFactor`; a machine-local preference in
`EditorSettings.xml`, never in the session, since 0.1.0 - EX-008 records that the
skill wants a content-component transform and host resizing instead). Since the UI review of
2026-10-17 every knob behaves like a DAW knob (`AnimatedKnob::bindParameter`:
double-click reset, the host's right-click menu, Tab and arrow keys, an
accessible name) and no control takes keyboard focus on a click, so the
space bar keeps reaching the host. ATTACK/RELEASE read as Root's time
(`dsp/EnvelopeMacro.h`, the same text in the host); OUTPUT reads dBFS; the
active preset dims to half gold once the sound is edited away from it
(`isActivePresetModified()`, compared, not tracked). `HorizonLookAndFeel` centralizes
palette/typography/the ring knob painter; other classes are one widget each
(`PadKnob` = one layer's VOL knob, `MacrosPanel` = the six macros in three
rows, `WheelSlider` =
PITCH/MOD, `PresetBar`, `OutputMeter`, `FooterBar`, `TitleBanner`), and
`HorizonScene` is the sunset panel behind them all.

**Motion (2026-10-07, owner's picks from a proposal menu).** Things move
only because the sound changed: `HorizonScene` follows FILTER (time of day:
50% is the designed palette, 0% dusk-blue, 100% golden hour), REVERB (haze on
the ridge; 20% is the designed blur) and WIDTH (the glow's spread; 40% is the
designed glow); knobs (`AnimatedKnob`) glide after a preset recall or A/B
switch in a left-to-right wave, but follow a hand on the knob at once, and
warm on hover/drag; a recalled preset's name and description fade in under
the wordmark; each pad card glows with its layer's real output; the first
time an instance's editor opens the sky rises from night, the logo's sun
comes up and the knobs sweep up (reopening shows it as it is). The
value a knob shows is eased; the parameter never is. One frame callback
(`advanceFrame`, driven by `VBlankAttachment`, capped near 60 fps, with a
30 Hz timer fallback) replaces the old 15 Hz polling timer, and each widget
repaints only what changed. Painting is kept cheap (measured 2026-10-14 on a
Retina M3 Pro): the panel is cached at the screen's pixel density and drawn
1:1 (full redraw 21 -> 3.7 ms; a 1x cache stretched through a clip was 96% of
all painting), knobs are buffered images, the pad glows are pre-rendered
sprites, and motion that runs by itself (pad glows, meter) repaints at about
30 fps and only for visible steps. The panel fills the window edge to edge,
square: the design's rounded corners left the host's background showing in
a rectangular plugin window. The palettes keep the panel text near the
design's own contrast at the bright end (measured numbers in `HorizonScene.h`)
and above it everywhere darker - check a change with
`HorizonPadGuiSnapshot` (`tools/gui_snapshot/`, target not built by default),
which renders the editor offscreen to PNG at any state.

## Tier P process

The project runs at playbook **Tier P**. `docs/gate-status.md` is the one-page
view of where each gate stands and what is outstanding; the per-gate artefacts
sit beside it (`brief.md`, `g1-algorithm.md`, `g2-prototype.md`,
`g3-measurements.md`, `g5-null-test.md`), and `docs/exceptions.md` is the
playbook 12.5 log. **A failed measurement is either fixed or gets a dated,
named exception** — it is not negotiable by whoever wrote the code. `brief.md`
is owner-confirmed: do not edit the YAML, add a dated amendment beneath it.

## DSP work

Invariants load automatically for `Source/**` and `*.dsp` (`~/.claude/rules/dsp-invariants.md`, installed from `~/bob-skills/rules/`). For design, porting, measurement, testing, review or release, and for any defect, use the **sound-design-bob** skill (synced from claude.ai; source and playbook in `~/bob-skills`). It carries the DSP playbook (v3.6 at 2026-10-17) in its `references/`: the core card `dsp-playbook-core.md` (read whole; section 0 maps tasks to recipes), the reference split into `dsp-selection.md` (S, algorithm choice), `dsp-foundations.md` (F), `dsp-instruments.md` (I), `dsp-effects.md` (E), `dsp-control.md` (C), `dsp-analysis.md` (A) and `dsp-development.md` (D: real-time safety, performance, testing, CI, host contract, licensing, agent workflow, task recipes) - grep them, never read them whole - and `dsp-sound-design-playbook.md` as the index. Its `scripts/dsp-kit/` holds the tested measurement library (`dspkit.py`), the harness contract new products follow, a test scaffold and the document templates. For the plugin editor, ux-ui-design-bob runs alongside on the same tier, gates and parameter table.

The playbook is at v3.6 (2026-10-17): 37 rules tagged by track. Horizon Pad is an instrument (track I) that also owns control (C: MIDI, pedal, wheels), processing (E: the shimmer, ensemble, reverb), mix and output (F.5-F.6). Its v2 sound-design pass is the playbook's instrument worked example (I.10); see `docs/dsp-review-2026-09-26.md` (findings) and `docs/sound-design-v2.md` (changes, measurements, listening list). Audited 2026-10-17 (`docs/dsp-review-2026-10-17.md`, resolution at its end): event timing (29) passes - MIDI splits the block at sample offsets; control mappings (30) are tested through real MIDI; the output limiter's 1x `tanh` (33) is documented as deliberately unoversampled with a rate comparison beside it; the translation battery (34) passes on every preset; the suite runs clean under RTSan (35); worst-case block time (37) is measured with `--profile` (p99 6% of the deadline at 64 samples, M3 Pro) - on the budget machine it is still owed. Frozen IDs with a versioned state (36) is **not yet applicable**: Horizon Pad is unreleased and testing-only, so parameters, state and presets change freely with no migration or version flags until the owner announces a release; at that point the state gains a version and golden state files (D.5.3). Rule 32 (bypass) does not apply to an instrument.
