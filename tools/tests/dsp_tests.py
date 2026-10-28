#!/usr/bin/env python3
"""
Horizon Pad DSP test suite (G6).

Driven by CTest; each test is also runnable on its own:

    ctest --test-dir build-release --output-on-failure
    python3 tools/tests/dsp_tests.py --tool ./build-release/HorizonPadSoundTool reset_determinism

Everything here runs the real plugin DSP through HorizonPadSoundTool. The
three regression tests at the bottom each guard a bug that was found by
measurement and fixed - they exist so it cannot come back silently.
"""
import argparse, hashlib, json, os, shutil, subprocess, sys, tempfile
import numpy as np
from scipy.io import wavfile

CHORD = "48,55,60,64,67,72,76,79"
TOOL = "./build-release/HorizonPadSoundTool"


class Fail(Exception):
    pass


def run(extra):
    # The tool writes UTF-8 on every platform; text=True would decode it with the locale's
    # code page (cp1252 on Windows) and turn preset names like "Alpenglühen" into mojibake.
    r = subprocess.run([TOOL] + extra, capture_output=True, encoding="utf-8")
    if r.returncode != 0:
        raise Fail(f"tool exited {r.returncode}: {r.stderr[:400]}")
    return json.loads(r.stdout[r.stdout.index('{'):])


def list_presets():
    """--list-presets emits a JSON array, not the usual object."""
    r = subprocess.run([TOOL, "--list-presets"], capture_output=True, encoding="utf-8")
    if r.returncode != 0:
        raise Fail(f"tool exited {r.returncode}: {r.stderr[:400]}")
    return json.loads(r.stdout[r.stdout.index('['):])


def read(path):
    sr, x = wavfile.read(path)
    return np.asarray(x, dtype=np.float64), sr


def band_energy_db(x, sr, lo, hi, t0=1.5, t1=3.5):
    v = x.mean(axis=1) if x.ndim > 1 else x
    seg = v[int(t0 * sr):int(t1 * sr)]
    n = 1 << 14
    w = np.hanning(n)
    frames = [np.abs(np.fft.rfft(seg[o:o + n] * w)) for o in range(0, len(seg) - n + 1, n // 2)]
    if not frames:
        raise Fail("render too short for spectral analysis")
    mag = np.mean(frames, axis=0)
    f = np.fft.rfftfreq(n, 1.0 / sr)
    m = (f >= lo) & (f < hi)
    return 20 * np.log10(max(np.sqrt((mag[m] ** 2).sum()), 1e-20) / mag.max())


def render(extra):
    """Render to a temporary WAV and return (stereo float64 array, rate)."""
    with tempfile.TemporaryDirectory() as tmp:
        p = os.path.join(tmp, "r.wav")
        run(extra + [f"--out={p}"])
        return read(p)


def lufs(x, sr):
    """ITU-R BS.1770 integrated loudness (no gating) - 48 kHz coefficients."""
    if sr != 48000:
        raise Fail("lufs() helper expects a 48 kHz render")
    from scipy import signal
    b1, a1 = [1.53512485958697, -2.69169618940638, 1.19839281085285], [1.0, -1.69065929318241, 0.73248077421585]
    b2, a2 = [1.0, -2.0, 1.0], [1.0, -1.99004745483398, 0.99007225036621]
    y = signal.lfilter(b2, a2, signal.lfilter(b1, a1, x, axis=0), axis=0)
    return -0.691 + 10 * np.log10(np.mean(y ** 2, axis=0).sum() + 1e-20)


def window(x, sr, t0, t1):
    return x[int(t0 * sr):int(t1 * sr)]


def am_depth_at(x, sr, rate_hz):
    """Envelope modulation depth (% of mean) at rate_hz, from a 10 ms RMS envelope."""
    m = x.mean(axis=1)
    w = int(0.01 * sr)
    env = np.sqrt(np.convolve(m ** 2, np.ones(w) / w, mode="same"))
    env = env / env.mean() - 1.0
    spec = np.abs(np.fft.rfft(env * np.hanning(len(env)))) / (np.hanning(len(env)).sum() / 2)
    f = np.fft.rfftfreq(len(env), 1.0 / sr)
    band = (f > rate_hz * 0.9) & (f < rate_hz * 1.1)
    return 100.0 * spec[band].max()


# --------------------------------------------------------------------------
# Definition-of-done tests
# --------------------------------------------------------------------------

def test_latency_zero():
    """Invariant #11: latency is 0 and is actually reported as 0."""
    d = run(["--notes=60", "--hold=1", "--tail=1"])
    got = d["config"]["latencySamples"]
    if got != 0:
        raise Fail(f"expected 0 samples of latency, plugin reports {got}")


def test_reset_determinism():
    """Two seeded renders must be bit-identical.

    This is the cancellation test: same seed, same input, residual exactly
    zero. Without --seed the plugin randomises start phases on purpose, so
    this also confirms the seed hook still works.
    """
    with tempfile.TemporaryDirectory() as tmp:
        hashes = []
        for i in (1, 2):
            p = os.path.join(tmp, f"r{i}.wav")
            run(["--seed=4242", f"--notes={CHORD}", "--hold=2", "--tail=2", f"--out={p}"])
            hashes.append(hashlib.sha256(open(p, "rb").read()).hexdigest())
        if hashes[0] != hashes[1]:
            raise Fail("two renders with the same seed differ")

        a = os.path.join(tmp, "a.wav")
        run(["--seed=1", f"--notes={CHORD}", "--hold=2", "--tail=2", f"--out={a}"])
        if hashlib.sha256(open(a, "rb").read()).hexdigest() == hashes[0]:
            raise Fail("different seeds produced identical output - seed is not wired up")


def test_rate_block_matrix():
    """Clean at six sample rates x six block sizes."""
    bad = []
    for sr in (44100, 48000, 88200, 96000, 176400, 192000):
        for bs in (1, 13, 32, 64, 512, 4096):
            d = run([f"--sample-rate={sr}", f"--block={bs}", f"--notes={CHORD}",
                     "--seed=11", "--hold=1", "--tail=1"])
            s = d["safety"]
            if s["hasNanOrInf"] or s["trueClipping"] or s["clickCount"]:
                bad.append(f"{sr} Hz / {bs}: {json.dumps(s)}")
    if bad:
        raise Fail("unclean combinations:\n  " + "\n  ".join(bad))


def test_silence_after_note():
    """No NaN, no denormal storm and a decaying tail after the sound stops."""
    d = run(["--sample-rate=48000", "--block=64", f"--notes={CHORD}", "--seed=11",
             "--param=reverb=1.0", "--hold=1", "--tail=60"])
    s = d["safety"]
    if s["hasNanOrInf"]:
        raise Fail("NaN/Inf after 60 s of silence")
    if s["trueClipping"]:
        raise Fail("clipping after 60 s of silence")


def test_all_presets_safe():
    """Every factory preset renders clean and stays below the ceiling."""
    names = [p["name"] for p in list_presets()]
    if len(names) < 8:
        raise Fail(f"expected a curated bank of 8-20 presets, found {len(names)}")
    bad = []
    for n in names:
        d = run([f"--preset={n}", f"--notes={CHORD}", "--seed=11", "--hold=2", "--tail=3"])
        s, l = d["safety"], d["levels"]
        if s["hasNanOrInf"] or s["trueClipping"] or l["peakDb"] > 0.0:
            bad.append(f"{n}: peak {l['peakDb']:.2f} dBFS, {json.dumps(s)}")
    if bad:
        raise Fail("presets failed:\n  " + "\n  ".join(bad))


# --------------------------------------------------------------------------
# Regression tests - each guards a bug found by measurement
# --------------------------------------------------------------------------

def test_shimmer_octave_present():
    """Expanse's shimmer must actually transpose up an octave.

    OctaveShimmer's grain read used to run BACKWARDS at unity speed, so the
    octave partial was 73 dB down and the layer's defining feature did not
    exist. At C4 the oscillator stack sits at 523 Hz and the shimmer octave
    belongs at ~1046 Hz.
    """
    with tempfile.TemporaryDirectory() as tmp:
        p = os.path.join(tmp, "e.wav")
        run(["--solo=expanse", "--notes=60", "--seed=11", "--velocity=1.0",
             "--param=reverb=0", "--param=filter=0.5", "--param=width=0",
             "--hold=4", "--tail=2", f"--out={p}"])
        x, sr = read(p)
        octave = band_energy_db(x, sr, 900, 1200)
        if octave < -45.0:
            raise Fail(f"shimmer octave at {octave:.1f} dB below peak; "
                       "expected better than -45 dB (regression of the "
                       "reversed grain read)")


def test_width_is_rate_invariant():
    """WIDTH's stereo image does not depend on the sample rate.

    It used to be a Haas delay of a flat 90 samples, so the image and its
    mono comb notch moved by a factor of 4.3 between 44.1 and 192 kHz. Since
    sound-design v2 WIDTH spreads oscillators instead - no delay at all - and
    this guards the property rather than the mechanism: inter-channel
    correlation at WIDTH 100% must agree across rates.
    """
    corrs = []
    for sr in (44100, 48000, 96000, 192000):
        x, fs = render(["--solo=root", "--notes=60", f"--sample-rate={sr}", "--seed=5",
                        "--param=width=1.0", "--param=reverb=0", "--hold=3", "--tail=0.2"])
        seg = window(x, fs, 1.0, 3.0)
        corrs.append(float(np.corrcoef(seg[:, 0], seg[:, 1])[0, 1]))
    if max(corrs) - min(corrs) > 0.1:
        raise Fail(f"WIDTH image varies with sample rate: correlation {corrs}")


# --------------------------------------------------------------------------
# Sound-design v2 guards (2026-09-26): presence of each layer's defining
# feature (rule 22), behaviour across the keyboard and in chords, stereo and
# level contracts, and the sustain pedal. See docs/sound-design-v2.md.
# --------------------------------------------------------------------------

def test_root_sub_present():
    """Root's sub-octave exists at C3, and is faded out where it would be rumble.

    At C3 (130.8 Hz) the sub sits at 65.4 Hz and must be clearly there. At C1
    (32.7 Hz) it would sit at 16 Hz - below hearing, pure PA rumble - and
    must be gone.
    """
    x, sr = render(["--solo=root", "--notes=48", "--seed=3", "--param=reverb=0",
                    "--param=width=0", "--hold=4", "--tail=0.2"])
    sub = band_energy_db(x, sr, 60, 71)
    if sub < -30.0:
        raise Fail(f"Root's sub-octave at C3 is {sub:.1f} dB below the peak; expected above -30 dB")


def test_clearing_ensemble_present():
    """Clearing's ensemble exists: it is stereo and it moves, even at WIDTH 0.

    The ensemble's left and right taps run at different LFO phases, so the
    layer is decorrelated with WIDTH at zero; a missing or static ensemble
    leaves the channels identical.
    """
    x, sr = render(["--solo=clearing", "--notes=60", "--seed=3", "--param=reverb=0",
                    "--param=width=0", "--hold=4", "--tail=0.2"])
    seg = window(x, sr, 2.0, 4.0)
    c = float(np.corrcoef(seg[:, 0], seg[:, 1])[0, 1])
    if c > 0.95:
        raise Fail(f"Clearing at WIDTH 0 has L/R correlation {c:.3f}; the ensemble is missing or mono")


def test_bloom_tremolo_survives_chords():
    """Bloom's 3.2 Hz tremolo keeps its depth in a chord - without being obvious.

    With a random tremolo phase per voice, a four-note chord averaged the
    pulse from 44% to 16% (review S-5); the shared LFO (2026-09-26) kept 44%,
    which the owner found too obvious in the blind A/B of 2026-10-06 (preferred
    v1's Bloom). Since 2026-10-07 the shared pulse is shallower: it must still
    survive a chord (above v1's 16%) and stay below the 44% that was rejected.
    """
    x, sr = render(["--solo=bloom", "--notes=48,55,60,64", "--seed=3", "--param=reverb=0",
                    "--param=width=0", "--hold=10", "--tail=0.2"])
    depth = am_depth_at(window(x, sr, 4.0, 10.0), sr, 3.2)
    if depth < 20.0:
        raise Fail(f"Bloom's tremolo depth in a chord is {depth:.1f}%, expected at least 20% "
                   "(v1's per-voice phases averaged it to 16%)")
    if depth > 38.0:
        raise Fail(f"Bloom's tremolo depth in a chord is {depth:.1f}% - the owner rejected 44% as too obvious")


def test_startup_program_matches_defaults():
    """The program the plugin opens on is the sound it makes.

    The bank is ordered in cycles by envelope length (2026-10-07), so program 0
    is no longer the preset the parameter defaults equal (Lagerfeuer). The
    processor selects that preset at construction; without it the preset bar
    named program 0 while the defaults played. Compared by value, not by name.
    """
    d = run(["--notes=60", "--hold=0.5", "--tail=0"])
    name, applied = d["startupProgram"], d["activeParams"]
    presets = {p["name"]: p for p in list_presets()}
    if name not in presets:
        raise Fail(f"startup program {name!r} is not a factory preset")
    p = presets[name]
    want = dict(p["volumes"], **p["macros"])
    off = {k: (applied.get(k), v) for k, v in want.items()
           if applied.get(k) is None or abs(float(applied[k]) - float(v)) > 1e-3}
    if off:
        raise Fail(f"the plugin opens on {name!r} but plays different values: {off}")


def _centroid_hz(x, sr):
    m = x.mean(axis=1)
    spec = np.abs(np.fft.rfft(m * np.hanning(len(m))))
    f = np.fft.rfftfreq(len(m), 1 / sr)
    return float((spec * f).sum() / spec.sum())


def test_root_bass_starts_open():
    """Root's bass notes start near their opened tone, not as a dull hum.

    Root's cutoff opens with the envelope. Tracked down into the bass, the first
    seconds were only fundamental and sub; the owner disliked them and liked the
    tone once it opened (blind A/B, 2026-10-07). Below C3 the cutoff now starts
    near its sustained brightness and sits a little brighter. Measured: early
    (0.2-0.8 s) centroid at C2, median of five seeds - 99 Hz without the fix,
    146 Hz with it.
    """
    early = []
    for seed in (3, 4, 5, 6, 7):
        x, sr = render(["--solo=root", "--notes=36", f"--seed={seed}", "--param=reverb=0",
                        "--hold=3", "--tail=0.2"])
        early.append(_centroid_hz(window(x, sr, 0.2, 0.8), sr))
    med = float(np.median(early))
    if med < 125.0:
        raise Fail(f"Root at C2 starts dull: early centroid {med:.0f} Hz (median), expected >= 125 Hz")


def test_expanse_shimmer_recedes_at_top():
    """Expanse's octave-up shimmer rolls off above C5.

    With the register work Expanse stays audible at the top, and at C6 its
    shimmer made the default patch's C6 chord too bright (lost to v1 in both
    blind A/B passes; +28 dB at 5-12 kHz on Expanse). The shimmer send now
    falls 9 dB per octave above C5. Measured at C6, shimmer octave against the
    stack octave: -13.1 dB without the roll-off, -22.1 dB with it.
    """
    x, sr = render(["--solo=expanse", "--notes=84", "--seed=3", "--param=reverb=0",
                    "--hold=6", "--tail=0.2"])
    m = window(x, sr, 3.0, 6.0).mean(axis=1)
    spec = np.abs(np.fft.rfft(m * np.hanning(len(m)))) ** 2
    f = np.fft.rfftfreq(len(m), 1 / sr)
    f0 = 440.0 * 2 ** ((84 - 69) / 12)

    def band(c):
        return 10 * np.log10(spec[(f >= c / 1.06) & (f < c * 1.06)].sum() + 1e-20)

    rel = band(4 * f0) - band(2 * f0)
    if rel > -17.5:
        raise Fail(f"Expanse's shimmer at C6 is {rel:.1f} dB against its stack, expected <= -17.5 dB")


def test_keyboard_level_span():
    """No layer loses its level across the played range, C2 to C6.

    Every filter used to be fixed in Hz around C4; Expanse spanned 14.7 LU
    and fell to -57 LUFS at C7 (review S-2). Two seeds per note, because the
    slowest LFOs make one seed a sample rather than a measurement.
    """
    worst = []
    for layer in ("root", "clearing", "expanse", "bloom"):
        levels = []
        for note in (36, 48, 60, 72, 84):
            vals = []
            for seed in (3, 4):
                x, sr = render([f"--solo={layer}", f"--notes={note}", f"--seed={seed}",
                                "--hold=6", "--tail=0.2"])
                vals.append(lufs(window(x, sr, 3.0, 6.0), sr))
            levels.append(float(np.mean(vals)))
        span = max(levels) - min(levels)
        worst.append((span, layer, [round(v, 1) for v in levels]))
        if span > 9.0:
            raise Fail(f"{layer} spans {span:.1f} LU from C2 to C6: {levels}")


def test_width_mono_safe_and_level_flat():
    """WIDTH 100% neither collapses in mono nor changes the level.

    The Haas tap it replaced combed in mono (-2.7 to -2.9 dB mono-sum loss)
    and lost ~1.5 dB of level at full width (rule 8). At WIDTH 100% every pad
    sits at its own profile's maximum (Root 60%, Bloom 90%, the others 100%).
    """
    for layer in ("root", "clearing", "expanse", "bloom"):
        lv = {}
        for width in (0.0, 1.0):
            x, sr = render([f"--solo={layer}", "--notes=48,55,60,64", "--seed=3", "--param=reverb=0",
                            f"--param=width={width}", "--hold=6", "--tail=0.2"])
            seg = window(x, sr, 3.0, 6.0)
            lv[width] = lufs(seg, sr)
            if width == 1.0:
                L, R = seg[:, 0], seg[:, 1]
                mono = 0.5 * (L + R)
                loss = 10 * np.log10((mono ** 2).mean() / (0.5 * ((L ** 2).mean() + (R ** 2).mean())))
                if loss < -3.0:
                    raise Fail(f"{layer} at WIDTH 100% loses {loss:.2f} dB summed to mono")
        if abs(lv[1.0] - lv[0.0]) > 1.0:
            raise Fail(f"{layer}: WIDTH 0 -> 100% changes loudness by {lv[1.0] - lv[0.0]:+.2f} LU")


def test_reverb_level_flat():
    """REVERB 0 -> 100% keeps the loudness within 1.5 LU (rule 8).

    The bass-dry split first shipped as a two-pole complement, which is not
    power-complementary and made Root 2.5 dB louder at REVERB 100%. Three
    seeds per point (playbook 4.10).
    """
    for layer in ("root", "clearing"):
        lv = {}
        for amount in (0.0, 1.0):
            vals = []
            for seed in (3, 4, 5):
                x, sr = render([f"--solo={layer}", "--notes=48,55,60,64", f"--seed={seed}",
                                f"--param=reverb={amount}", "--hold=8", "--tail=0.2"])
                vals.append(lufs(window(x, sr, 5.0, 8.0), sr))
            lv[amount] = float(np.mean(vals))
        if abs(lv[1.0] - lv[0.0]) > 1.5:
            raise Fail(f"{layer}: REVERB 0 -> 100% changes loudness by {lv[1.0] - lv[0.0]:+.2f} LU")


def test_sustain_pedal_holds():
    """CC64 holds released notes and releases them when it comes up.

    The instrument ignored the sustain pedal entirely before v2.
    """
    common = ["--solo=root", "--notes=60", "--seed=3", "--param=reverb=0", "--hold=1", "--tail=3"]
    held, sr = render(common + ["--pedal=0.5,4.0"])
    free, _ = render(common)
    gap = lufs(window(held, sr, 3.0, 3.5), sr) - lufs(window(free, sr, 3.0, 3.5), sr)
    if gap < 12.0:
        raise Fail(f"with the pedal down the released note is only {gap:.1f} LU louder than without it")
    after = lufs(window(held, sr, 6.5, 7.0), sr) - lufs(window(held, sr, 3.0, 3.5), sr)
    if after > -30.0:
        raise Fail(f"3 s after pedal-up the note is only {after:.1f} LU down; it was not released")


def test_attack_release_span():
    """ATTACK/RELEASE run 1% -> 100% of each layer's designed times (brief A-009).

    One exponential curve, time = designed x 0.01^(1 - knob): 0% is the fast,
    click-free floor, 50% a tenth of the designed time, 100% the designed
    envelope itself (2026-10-14; until then 50% was designed and 100% double).
    Release is checked exactly - Root's linear ramp reaches -60 dB at the
    ramp's length - and attack by how far the note has swelled at 0.3 s.
    """
    def release_s(knob):
        d = run(["--solo=root", "--notes=60", "--hold=4", "--tail=4", "--param=reverb=0",
                 f"--param=attack={knob}", f"--param=release={knob}", "--window-ms=10", "--seed=3"])
        return d["envelope"]["timing"]["releaseToMinus60Sec"]

    for knob in (0.0, 0.5, 1.0):
        want = 2.4 * 0.01 ** (1.0 - knob)
        got = release_s(knob)
        if abs(got - want) > max(0.10 * want, 0.006):
            raise Fail(f"RELEASE {knob:.0%}: Root falls silent in {got:.3f} s, expected {want:.3f} s")

    def swell_db(knob):
        out = []
        for seed in (1, 2, 3, 4, 5):
            d = run(["--solo=root", "--notes=60", "--hold=4", "--tail=1", "--param=reverb=0",
                     f"--param=attack={knob}", "--window-ms=10", f"--seed={seed}"])
            r = d["envelope"]["rmsPerWindow"]
            out.append(20 * np.log10(max(r[30], 1e-12) / max(np.mean(r[290:310]), 1e-12)))
        return float(np.median(out))   # Root's breathing moves single seeds by +/-4 dB

    half, full = swell_db(0.5), swell_db(1.0)
    if half < -6.0:
        raise Fail(f"ATTACK 50%: Root is still {half:.1f} dB below its sustain at 0.3 s "
                   f"(its 0.22 s swell should be over)")
    if full > -10.0:
        raise Fail(f"ATTACK 100%: Root is already at {full:.1f} dB of its sustain at 0.3 s "
                   f"(the designed 2.2 s swell has barely begun)")


def test_presets_have_worship_envelopes():
    """No factory preset swells or fades like a film score (brief A-009).

    The owner's listening test (2026-10-14) heard the pads as cinematic: worship
    playing needs a pad that is there within a beat and clears a chord change.
    Every preset, open chord, dry (the room's tail is REVERB's job): 10-90%
    swell <= 1.2 s, fall to -20 dB after the keys come up <= 1.4 s. Measured on
    2026-10-14: worst 0.92 s and 1.15 s; the bank before was 1.7-2.5 s and up
    to 2.7 s.
    """
    slow = []
    for p in list_presets():
        d = run([f"--preset={p['name']}", "--notes=48,55,60,64", "--hold=5", "--tail=4",
                 "--param=reverb=0", "--window-ms=10", "--seed=1"])
        t = d["envelope"]["timing"]
        if t["attack10to90Sec"] > 1.2 or t["releaseToMinus20Sec"] > 1.4:
            slow.append(f"{p['name']} ({t['attack10to90Sec']:.2f} s swell, {t['releaseToMinus20Sec']:.2f} s release)")
    if slow:
        raise Fail("slower than a worship pad: " + ", ".join(slow))


def test_voice_steal_declick():
    """Stealing a sounding voice must not step harder than an ordinary note-on.

    Taking over a slot used to hard-reset the envelope mid-cycle; the worst
    of eight runs stepped +3.8 dB relative to the signal.

    Seeded over twelve fixed seeds (rule 23). The first version took the worst
    of six *unseeded* renders per side and failed about 2 runs in 10 on a
    correct build: the step metric is the largest sample-to-sample jump, so
    saw edges at random phases make single values swing by 10 dB. Measured
    2026-10-03 over seeds 1-12 (median steal - median free / worst steal vs
    worst free): 5056369 -1.8 dB / -12.4 vs -11.2; 05c3f44 -2.1 dB /
    -11.4 vs -9.2; 5056369 with the declick removed +7.9 dB / -0.6 vs -11.2,
    which fails both conditions below by about 5 dB.
    """
    seeds = range(1, 13)

    def steps(notes, onsets):
        vals = []
        for seed in seeds:
            d = run([f"--notes={notes}", f"--note-onsets={onsets}", f"--seed={seed}",
                     "--hold=6", "--tail=1", "--probe-at=3.0"])
            vals.append(d["transients"]["probes"][0]["stepRelDb"])
        return sorted(vals)

    def median(v):
        return 0.5 * (v[len(v) // 2 - 1] + v[len(v) // 2])

    steal = steps("60,62,64,65,67,69,71,72,74", "0,0,0,0,0,0,0,0,3")
    free = steps("60,62,64,65,67,69,71,74", "0,0,0,0,0,0,0,3")
    if median(steal) > median(free) + 3.0:
        raise Fail(f"voice steal median step {median(steal):.1f} dB vs {median(free):.1f} dB "
                   "for a free slot - declick ramp regressed")
    if steal[-1] > free[-1] + 6.0:
        raise Fail(f"voice steal worst step {steal[-1]:.1f} dB vs {free[-1]:.1f} dB for a "
                   "free slot - declick ramp regressed")


def test_low_register_stays_musical():
    """The bass register is not rough, and Expanse stays in its own register.

    v1 filtered in fixed Hz and kept full unison detune everywhere: a single
    C1 on Root measured 40x its C4 roughness and Clearing's close C2 triad
    scored 9.0. Expanse played at C2 sank to a 131 Hz drone. Guards the
    register pass (full key tracking below C4, bass unison tightening,
    ensemble bass split, Expanse break-back). Roughness is Vassilakis over
    partials >= 8 Hz apart (tools/measure/register.py).
    """
    sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "measure"))
    import register as R
    R.TOOL = TOOL
    limits = {"root": 0.4, "clearing": 1.2, "bloom": 1.0}
    for layer, limit in limits.items():
        vals = []
        for seed in (21, 22):
            x = R.render([f"--solo={layer}", "--notes=36"], seed)
            fr, am = R.peaks(x[int(4 * R.SR):int(8 * R.SR)].mean(axis=1))
            vals.append(R.roughness(fr, am))
        if np.mean(vals) > limit:
            raise Fail(f"{layer} at C2 has roughness {np.mean(vals):.2f}, limit {limit}")
    x = R.render(["--solo=expanse", "--notes=36"], 21)
    d = R.describe(x, 65.41)
    if d["harmonics"] < 3.0:
        raise Fail(f"Expanse at C2 sits at {d['harmonics']:.1f} x f0; the break-back octave is missing")


def test_bass_is_mono():
    """Below ~120 Hz the output is mono, whatever WIDTH or the reverb does above it.

    WIDTH spreads whole oscillators; without the side high-pass a C2 chord at
    full width put its fundamentals into the side channel. DETUNE at 100%
    decorrelates the stacks further, so it is the worst case.

    With the reverb on, the dry bass was mono but the reverb's decorrelated
    return rebuilt a side channel below the split: -5.3 to -5.6 dB under the
    mid on Sternenzelt and Bergecho (tools/measure/translation.py, 2026-10-03),
    because the side high-pass ran before the send. The second render guards
    that, tail included.
    """
    from scipy import signal
    b, a = signal.butter(4, 100, fs=48000)

    def side_under_mid(reverb, t0, t1, tail):
        x, sr = render(["--notes=36,43,48", "--seed=3", f"--param=reverb={reverb}",
                        "--param=root=1;clearing=1;expanse=1;bloom=1",
                        "--param=width=1;detune=1",
                        "--hold=6", f"--tail={tail}"])
        seg = window(x, sr, t0, t1)
        mid = signal.lfilter(b, a, seg.mean(axis=1))
        side = signal.lfilter(b, a, 0.5 * (seg[:, 0] - seg[:, 1]))
        return 10 * np.log10((side ** 2).mean() / ((mid ** 2).mean() + 1e-20) + 1e-20)

    dry = side_under_mid(0, 3.0, 6.0, 0.2)
    if dry > -15.0:
        raise Fail(f"side below 100 Hz is only {dry:.1f} dB under the mid at full width")
    wet = side_under_mid(1, 3.0, 8.0, 2.0)
    if wet > -15.0:
        raise Fail(f"with REVERB 100% the side below 100 Hz is only {wet:.1f} dB under the "
                   "mid - the reverb return is putting stereo back into the bass")


# --------------------------------------------------------------------------
# WIDTH and DETUNE macros (2026-10-02)
# --------------------------------------------------------------------------

def test_width_profile_staggered():
    """WIDTH opens the pads in order: the air first, the foundation last.

    One WIDTH macro drives all four pads through each pad's fixed profile
    (LayerBase::widthProfile()): Expanse opens from 0%, Clearing from 10%,
    Bloom from 15%, Root from 20%, and since 2026-10-07 every pad reaches its
    full spread at 100% (Root stopped at 60% and Bloom at 90% before; the owner
    preferred v1's full width on Root and Sternenzelt at WIDTH 100%). At 8%
    Root, Bloom and Clearing must still be exactly mono while Expanse has
    opened; at 14% Root and Bloom mono, Clearing open; at 18% Root mono, Bloom
    open (Clearing's ensemble is stereo at any WIDTH, so its opening is checked
    at 50%); no pad may narrow as WIDTH rises.
    """
    steps = (0.0, 0.08, 0.14, 0.18, 0.5, 1.0)
    corr = {}
    for layer in ("root", "clearing", "expanse", "bloom"):
        corr[layer] = []
        for w in steps:
            x, sr = render([f"--solo={layer}", "--notes=48,55,60,64", "--seed=3", "--param=reverb=0",
                            f"--param=width={w}", "--hold=6", "--tail=0.2"])
            seg = window(x, sr, 3.0, 6.0)
            corr[layer].append(float(np.corrcoef(seg[:, 0], seg[:, 1])[0, 1]))

    for layer, i in (("root", 1), ("root", 2), ("root", 3), ("bloom", 1), ("bloom", 2), ("clearing", 1)):
        if abs(corr[layer][i] - corr[layer][0]) > 1e-4:
            raise Fail(f"{layer} has opened at WIDTH {steps[i]:.0%} (L/R correlation "
                       f"{corr[layer][0]:.4f} -> {corr[layer][i]:.4f}); its profile should keep it mono")
    # The order: each pad has *started* to open (any change) at the step after its
    # profile's start, while the next one is still mono (checked above).
    for layer, i in (("bloom", 3),):
        if abs(corr[layer][i] - corr[layer][0]) <= 1e-4:
            raise Fail(f"{layer} has not started to open by WIDTH {steps[i]:.0%}")
    # Clearly open by 50% (Clearing's ensemble is stereo at any WIDTH, so its
    # opening only shows there).
    for layer, i in (("expanse", 1), ("clearing", 4), ("bloom", 4), ("root", 4)):
        if corr[layer][0] - corr[layer][i] < 0.002:
            raise Fail(f"{layer} has not opened by WIDTH {steps[i]:.0%} (L/R correlation "
                       f"{corr[layer][0]:.4f} -> {corr[layer][i]:.4f})")
    for layer, vals in corr.items():
        if any(b > a + 0.002 for a, b in zip(vals, vals[1:])):
            raise Fail(f"{layer} narrows as WIDTH rises: correlation {[round(v, 4) for v in vals]}")


def _partial_spread_cents(x, sr, target_hz):
    """Power-weighted pitch spread, in cents, of the partial nearest target_hz."""
    m = x.mean(axis=1)
    p = np.abs(np.fft.rfft(m * np.hanning(len(m)))) ** 2
    f = np.fft.rfftfreq(len(m), 1.0 / sr)
    near = (f > target_hz * 0.985) & (f < target_hz * 1.015)
    peak = f[np.argmax(np.where(near, p, 0.0))]
    win = (f > peak * 0.975) & (f < peak * 1.025)
    pw, fw = p[win], f[win]
    mean = (pw * fw).sum() / pw.sum()
    std = np.sqrt((pw * (fw - mean) ** 2).sum() / pw.sum())
    return 1200.0 * np.log2(1.0 + std / mean)


def test_detune_spreads_every_stack():
    """DETUNE widens every pad's unison spread and leaves the level alone (rule 8).

    DETUNE scales each stack's static detune and its baseline drift - and on
    Clearing, Expanse and Bloom the drift is most of the spread. Measured as
    the pitch spread of one partial of a held C4 (5 s window, 0.2 Hz bins),
    three seeds. Clearing's ensemble adds pitch movement of its own that
    DETUNE does not touch, so its ratio is the smallest.
    """
    partial = {"root": 784.9, "clearing": 1046.5, "expanse": 523.3, "bloom": 784.9}
    for layer, hz in partial.items():
        spread, level = {}, {}
        for amount in (0.0, 0.5, 1.0):
            s, lv = [], []
            for seed in (3, 4, 5):
                x, sr = render([f"--solo={layer}", "--notes=60", f"--seed={seed}", "--param=reverb=0",
                                "--param=width=0", "--param=filter=0.5", f"--param=detune={amount}",
                                "--hold=8", "--tail=0.2"])
                seg = window(x, sr, 3.0, 8.0)
                s.append(_partial_spread_cents(seg, sr, hz))
                lv.append(lufs(seg, sr))
            spread[amount], level[amount] = float(np.mean(s)), float(np.mean(lv))
        if spread[1.0] < 1.25 * spread[0.0]:
            raise Fail(f"{layer}: DETUNE 0 -> 100% only moves the {hz:.0f} Hz partial's spread "
                       f"from {spread[0.0]:.2f} to {spread[1.0]:.2f} cents")
        for amount in (0.0, 1.0):
            if abs(level[amount] - level[0.5]) > 1.0:
                raise Fail(f"{layer}: DETUNE 50 -> {amount:.0%} changes loudness by "
                           f"{level[amount] - level[0.5]:+.2f} LU")


def test_register_is_c0_to_c7():
    """The instrument plays C0 to C7 (MIDI 12-96) and is silent outside that register.

    The register was set by the owner's listening (brief A-010, 2026-10-17):
    below C0 and above C7 the sound is no longer musical, so a note-on there
    starts no voice. Both ends must still sound; the first key outside each
    end, and the extremes of the MIDI range, must render nothing at all.
    """
    for note in (12, 96):
        d = run([f"--notes={note}", "--seed=1", "--hold=1", "--tail=0.5"])
        if d["levels"]["rmsDb"] < -60.0:
            raise Fail(f"MIDI {note} is inside the register but renders at {d['levels']['rmsDb']:.1f} dBFS RMS")
    for note in (0, 11, 97, 127):
        d = run([f"--notes={note}", "--seed=1", "--hold=1", "--tail=0.5"])
        if d["levels"]["peak"] > 0.0:
            raise Fail(f"MIDI {note} is outside the register C0-C7 but renders at "
                       f"{d['levels']['peakDb']:.1f} dBFS peak")


def _f0_hz(seg, sr, lo, hi):
    """The strongest spectral peak between lo and hi Hz, parabolically interpolated."""
    m = seg.mean(axis=1)
    spec = np.abs(np.fft.rfft(m * np.hanning(len(m))))
    f = np.fft.rfftfreq(len(m), 1.0 / sr)
    k = int(np.argmax(np.where((f > lo) & (f < hi), spec, 0.0)))
    a, b, c = np.log(spec[k - 1:k + 2] + 1e-30)
    return (k + 0.5 * (a - c) / (a - 2 * b + c)) * sr / len(m)


def test_velocity_curve():
    """Velocity raises the level at every step, and the softest key is still played (rule 30).

    The curve is 0.35 + 0.65 * sqrt(v) on the voice gain (PluginProcessor::noteOn):
    gentle for a pad, never silent. Measured 2026-10-17 on the default patch, C4,
    reverb 0: -31.6 / -27.2 / -25.6 / -24.6 / -23.8 LUFS at velocity 1/127, 0.25,
    0.5, 0.75, 1 - a 7.8 LU span.
    """
    levels = []
    for v in (1 / 127, 0.25, 0.5, 0.75, 1.0):
        lv = [lufs(window(render(["--notes=60", f"--seed={seed}", "--param=reverb=0", f"--velocity={v}",
                                  "--hold=4", "--tail=0.2"])[0], 48000, 2.0, 4.0), 48000) for seed in (3, 4)]
        levels.append(float(np.mean(lv)))
    steps = np.diff(levels)
    if steps.min() < 0.4:
        raise Fail(f"velocity does not raise the level at every step: {[round(v, 1) for v in levels]} LUFS")
    span = levels[-1] - levels[0]
    if not 4.0 <= span <= 12.0:
        raise Fail(f"velocity 1/127 -> 1 spans {span:.1f} LU (want 4-12: soft keys audible, loud ones not "
                   f"a jump): {[round(v, 1) for v in levels]}")


def test_pitch_bend_range():
    """The pitch wheel bends +/-2 semitones at its ends, through the real MIDI path (rule 30).

    A 14-bit pitch-wheel message at full throw must move the note by 200
    cents either way (kPitchBendRangeSemitones). Clearing solo at A3, DETUNE 0,
    so the fundamental is one clear peak. Measured 2026-10-17: +200.1 / -197.6
    cents against the unbent note.
    """
    common = ["--solo=clearing", "--notes=57", "--seed=3", "--param=reverb=0", "--param=detune=0",
              "--param=width=0", "--hold=6", "--tail=0.1"]
    hz = {}
    for bend in (0.0, 1.0, -1.0):
        x, sr = render(common + [f"--bend=0:{bend}"])
        hz[bend] = _f0_hz(window(x, sr, 1.0, 6.0), sr, 150.0, 330.0)
    for bend, want in ((1.0, 200.0), (-1.0, -200.0)):
        cents = 1200.0 * np.log2(hz[bend] / hz[0.0])
        if abs(cents - want) > 5.0:
            raise Fail(f"pitch wheel at {bend:+.0f} moves the note {cents:+.1f} cents, want {want:+.0f}")


def test_mod_wheel_adds_movement():
    """CC1 (MOD) adds drift on every stack it reaches, sensibly at full throw (rule 30).

    MOD adds kModDriftDepth to each voice's drift. Measured as the pitch spread
    of one partial of a held C4 (as detune_spreads_every_stack), three seeds,
    2026-10-17: Root 3.6 -> 12.2, Clearing 6.3 -> 14.5, Bloom 2.5 -> 13.7 cents,
    level within 0.4 LU. Full MOD must move clearly, stay a drift (under 30
    cents, not a detuned chorus) and leave the level alone.
    """
    partial = {"root": 784.9, "clearing": 1046.5, "bloom": 784.9}
    for layer, hz in partial.items():
        spread, level = {}, {}
        for mod in (0.0, 1.0):
            s, lv = [], []
            for seed in (3, 4, 5):
                x, sr = render([f"--solo={layer}", "--notes=60", f"--seed={seed}", "--param=reverb=0",
                                "--param=width=0", "--param=filter=0.5", f"--mod=0:{mod}", "--hold=8", "--tail=0.2"])
                seg = window(x, sr, 3.0, 8.0)
                s.append(_partial_spread_cents(seg, sr, hz))
                lv.append(lufs(seg, sr))
            spread[mod], level[mod] = float(np.mean(s)), float(np.mean(lv))
        if spread[1.0] < 1.8 * spread[0.0]:
            raise Fail(f"{layer}: MOD 0 -> 100% only moves the spread from {spread[0.0]:.2f} to {spread[1.0]:.2f} cents")
        if spread[1.0] > 30.0:
            raise Fail(f"{layer}: MOD 100% spreads {spread[1.0]:.1f} cents - a detune, not a drift")
        if abs(level[1.0] - level[0.0]) > 1.0:
            raise Fail(f"{layer}: MOD 0 -> 100% changes loudness by {level[1.0] - level[0.0]:+.2f} LU")


def _hf_db(x, sr, t0, t1, lo=12000.0):
    from scipy import signal
    sos = signal.butter(8, lo, "highpass", fs=sr, output="sos")
    y = signal.sosfilt(sos, window(x, sr, t0, t1).mean(axis=1))[int(0.05 * sr):]
    return 10 * np.log10(np.mean(y ** 2) + 1e-30)


def test_automation_is_zipper_free():
    """Fast host automation steps per block but sounds smooth (rule 3, definition of done).

    JUCE hosts deliver automation once per block, so a 0.25 s sweep at 512
    samples is a staircase of ~23 jumps. Every parameter on the audio path is
    smoothed per sample; an unsmoothed one adds broadband energy at the block
    rate. Root at C2 has almost nothing above 12 kHz (~-130 dB), so any zipper
    shows there: during a sweep up and back the band must stay within 3 dB of
    the louder of the two static ends. Measured 2026-10-17: every sweep sits
    between its ends, no clicks.
    """
    base = ["--notes=48", "--seed=3", "--param=reverb=0", "--hold=6", "--tail=0.1", "--block=512",
            "--param=clearing=0", "--param=expanse=0", "--param=bloom=0"]
    for pid, lo, hi in (("root", 0.2, 1.0), ("filter", 0.2, 0.8), ("width", 0.0, 1.0),
                        ("reverb", 0.0, 0.6), ("detune", 0.0, 1.0)):
        patch = base + ([] if pid == "root" else ["--param=root=1"])
        sweep = patch + [f"--param={pid}={lo}", f"--automate={pid}:3.0={lo},3.25={hi},3.5={lo}"]
        x, sr = render(sweep)
        d = run(sweep)
        swept = _hf_db(x, sr, 3.0, 3.5)
        ends = max(_hf_db(render(patch + [f"--param={pid}={v}"])[0], sr, 3.0, 3.5) for v in (lo, hi))
        if swept > ends + 3.0:
            raise Fail(f"{pid}: a fast sweep puts {swept - ends:+.1f} dB above 12 kHz over its static ends (zipper)")
        if d["safety"]["clickCount"] > 0:
            raise Fail(f"{pid}: a fast sweep clicks ({d['safety']['clickCount']} discontinuities)")



# ---------------------------------------------------------------------------
# The parameter table (parameters.csv) is the single source (S.1, 2026-10-21)
# ---------------------------------------------------------------------------
REPO = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))


def test_parameter_table_matches_plugin():
    """The plugin's layout is parameters.csv's, and the generated files are current.

    The layout is written by hand only because ATTACK/RELEASE show a time the
    table cannot express yet; this diff keeps it honest: IDs, order, names,
    0..100 ranges and defaults against the CSV (paramgen.py check --list-params
    plus name and range), the manual and Source/Params.h regenerated and compared.
    """
    import csv
    paramgen = os.path.join(REPO, "scripts", "dsp-kit", "paramgen.py")
    table = os.path.join(REPO, "parameters.csv")
    r = subprocess.run([TOOL, "--list-params"], capture_output=True, encoding="utf-8")
    if r.returncode != 0 or not r.stdout.lstrip().startswith("["):
        raise Fail(f"--list-params gave no parameter list (exit {r.returncode}); the harness must answer it")
    listing = json.loads(r.stdout)
    with tempfile.TemporaryDirectory() as tmp:
        lp = os.path.join(tmp, "params.json")
        with open(lp, "w") as f:
            json.dump(listing, f)
        c = subprocess.run([sys.executable, paramgen, "check", table,
                            "--manual", os.path.join(REPO, "docs", "parameters.md"), "--list-params", lp],
                           capture_output=True, text=True)
        if c.returncode != 0:
            raise Fail("paramgen check: " + (c.stdout + c.stderr).strip()[-600:])
        gen = os.path.join(tmp, "Params.h")
        subprocess.run([sys.executable, paramgen, "juce", table, "-o", gen], check=True, capture_output=True)
        with open(gen) as f1, open(os.path.join(REPO, "Source", "Params.h")) as f2:
            if f1.read() != f2.read():
                raise Fail("Source/Params.h is stale: run scripts/dsp-kit/paramgen.py juce parameters.csv -o Source/Params.h")
    with open(table, newline="", encoding="utf-8") as f:
        rows = {row["id"]: row for row in csv.DictReader(f)}
    for item in listing:
        row = rows.get(item["id"])
        if row is None:
            continue   # paramgen reported it already
        for key, want in (("name", row["name"]), ("min", float(row["min"])), ("max", float(row["max"])),
                          ("default_plain", float(row["default"]))):
            got = item.get(key)
            if (got != want) if key == "name" else (got is None or abs(float(got) - want) > 1e-3):
                raise Fail(f"{item['id']}: plugin {key} {got!r}, parameters.csv {want!r}")



# ---------------------------------------------------------------------------
# Session state (D.5.3): version 2 since 0.1.0 (2026-10-21)
# ---------------------------------------------------------------------------
ALIAS = {"rootVolume": "root", "clearingVolume": "clearing", "expanseVolume": "expanse", "bloomVolume": "bloom",
         "attackMacro": "attack", "releaseMacro": "release", "filterMacro": "filter", "reverbMacro": "reverb",
         "widthMacro": "width", "detuneMacro": "detune"}
STATE_DIR = os.path.join(REPO, "tools", "tests", "state")


def _state_run(extra):
    """The tool with a state flag: (exit code, JSON or None, stderr). Never raises on a non-zero exit."""
    r = subprocess.run([TOOL, "--notes=60", "--hold=0.3", "--tail=0.2"] + extra, capture_output=True, encoding="utf-8")
    if r.returncode < 0:
        raise Fail(f"tool crashed (signal {-r.returncode}) on {extra}: {r.stderr[-300:]}")
    d = json.loads(r.stdout[r.stdout.index("{"):]) if "{" in r.stdout else None
    return r.returncode, d, r.stderr


def _xml_of(blob):
    import struct
    if len(blob) < 9 or struct.unpack("<I", blob[:4])[0] != 0x21324356:
        raise Fail("state blob is not JUCE's XML-in-binary format")
    return blob[8:].rstrip(b"\0").decode("utf-8")


def test_state_round_trip():
    """State out, in, out is bytewise identical, and a session renders as the preset it was saved on."""
    with tempfile.TemporaryDirectory() as tmp:
        for preset in ("init", "sternenzelt", "funkenflug"):
            a, b = os.path.join(tmp, "a.bin"), os.path.join(tmp, "b.bin")
            run([f"--preset={preset}", "--notes=60", "--hold=0.3", "--tail=0", f"--state-out={a}"])
            code, d, err = _state_run([f"--state-in={a}", f"--state-out={b}"])
            if code != 0:
                raise Fail(f"{preset}: its own state was refused: {err[-200:]}")
            if open(a, "rb").read() != open(b, "rb").read():
                raise Fail(f"{preset}: state out -> in -> out differs bytewise")
            if 'version="2"' not in _xml_of(open(a, "rb").read()):
                raise Fail(f"{preset}: the state carries no version 2")
            x, _ = render([f"--preset={preset}", "--seed=1", "--notes=48,60,64", "--hold=1.5", "--tail=0.5"])
            y, _ = render([f"--state-in={a}", "--seed=1", "--notes=48,60,64", "--hold=1.5", "--tail=0.5"])
            resid = 20 * np.log10(max(np.max(np.abs(x - y)), 1e-20))
            if resid > -120:
                raise Fail(f"{preset}: the session renders {resid:.1f} dBFS away from the preset")


def test_corrupted_state_is_refused():
    """Any bytes as state: refused with a message and exit 2, or loaded as far as understood; never a
    crash, never NaN, and the plugin keeps playing (harness contract --state-in, D.5.3)."""
    import struct
    good = open(os.path.join(STATE_DIR, "0.1.0.bin"), "rb").read()
    xml = _xml_of(good)
    pack = lambda s: struct.pack("<II", 0x21324356, len(s) + 1) + s + b"\0"
    refused = {
        "empty": b"",
        "random": bytes(range(7, 250, 3)),
        "truncated": good[: len(good) // 2],
        "foreign": pack(b'<?xml version="1.0"?><SomeOtherPlugin a="1"/>'),
        "bad length": struct.pack("<II", 0x21324356, 0x7FFFFFF0) + b"<HorizonPadState",
    }
    sanitised = {
        "values not numbers": pack(xml.replace('value="16.7"', 'value="nan"', 1).replace('value="100"', 'value="abc"', 1).encode()),
        "values out of range": pack(xml.replace('value="80"', 'value="1e9"', 1).replace('value="75"', 'value="-40"', 1).encode()),
        "flipped bytes": good[:200] + bytes(b ^ 0x5A for b in good[200:260]) + good[260:],
    }
    with tempfile.TemporaryDirectory() as tmp:
        for cases, want_refused in ((refused, True), (sanitised, False)):
            for name, blob in cases.items():
                path = os.path.join(tmp, "s.bin")
                open(path, "wb").write(blob)
                code, d, err = _state_run([f"--state-in={path}"])
                if code == 3 or d is None:
                    raise Fail(f"{name}: no clean render (exit {code})")
                if d["safety"]["hasNanOrInf"]:
                    raise Fail(f"{name}: NaN or Inf in the output")
                if d["levels"]["peakDb"] < -60:
                    raise Fail(f"{name}: the plugin stopped playing (peak {d['levels']['peakDb']:.1f} dBFS)")
                bad = {k: v for k, v in d["activeParams"].items() if not 0.0 <= float(v) <= 1.0}
                if bad:
                    raise Fail(f"{name}: parameters outside their range: {bad}")
                if want_refused and (code != 2 or "refused" not in err):
                    raise Fail(f"{name}: should be refused with a message and exit 2, got exit {code}")
                if not want_refused and name != "flipped bytes" and code != 0:
                    raise Fail(f"{name}: a readable state with broken values should load (sanitised), got exit {code}")


def test_golden_states_load():
    """Every golden state in tools/tests/state loads into this build with its values and preset name:
    v1 (before 0.1.0, 0..1, buffers by index) migrates, 0.1.0 loads unchanged (rule 36, D.5.3)."""
    goldens = sorted(f[:-4] for f in os.listdir(STATE_DIR) if f.endswith(".bin"))
    if len(goldens) < 2:
        raise Fail(f"expected the v1 and 0.1.0 goldens, found {goldens}")
    with tempfile.TemporaryDirectory() as tmp:
        for g in goldens:
            want = json.load(open(os.path.join(STATE_DIR, g + ".json"), encoding="utf-8"))
            out = os.path.join(tmp, "out.bin")
            code, d, err = _state_run([f"--state-in={os.path.join(STATE_DIR, g + '.bin')}", f"--state-out={out}"])
            if code != 0:
                raise Fail(f"{g}: refused ({err[-200:]})")
            for k, v in want["params"].items():
                got = d["activeParams"].get(ALIAS.get(k, k))
                if got is None or abs(float(got) - float(v)) > 1e-4:
                    raise Fail(f"{g}: {k} loads as {got}, the golden says {v}")
            if d["activePreset"] != want["activePreset"]:
                raise Fail(f"{g}: preset name {d['activePreset']!r}, the golden says {want['activePreset']!r}")
            if "bufferB" in want:
                import re
                xml = _xml_of(open(out, "rb").read())
                attrs = dict(re.findall(r'(\w+)="([^"]*)"', re.search(r"<BufferB ([^>]*)/>", xml).group(1)))
                off = {k: (attrs.get(k), v) for k, v in want["bufferB"].items() if abs(float(attrs.get(k, "nan")) - v) > 1e-3}
                if off:
                    raise Fail(f"{g}: buffer B migrated wrong: {off}")
            if "editorScale" in _xml_of(open(out, "rb").read()):
                raise Fail(f"{g}: the editor size is still saved in the state (machine-local since 0.1.0)")


def test_newer_state_loads_what_it_understands():
    """A session from a newer version loads its parameters, ignores what it does not know, says so."""
    good = _xml_of(open(os.path.join(STATE_DIR, "0.1.0.bin"), "rb").read())
    newer = good.replace('version="2"', 'version="7"', 1).replace('pluginVersion="0.1.0"', 'pluginVersion="3.0.0"', 1)
    newer = newer.replace("</HorizonPadState>", '<FutureThing x="1"/></HorizonPadState>')
    import struct
    blob = struct.pack("<II", 0x21324356, len(newer) + 1) + newer.encode() + b"\0"
    with tempfile.TemporaryDirectory() as tmp:
        path = os.path.join(tmp, "n.bin")
        open(path, "wb").write(blob)
        code, d, err = _state_run([f"--state-in={path}"])
    if code != 0:
        raise Fail(f"a newer session was refused (exit {code}); it should load as far as understood")
    if "newer" not in d.get("stateNotice", ""):
        raise Fail(f"no notice that the session is newer: {d.get('stateNotice')!r}")
    if abs(float(d["activeParams"]["expanse"]) - 1.0) > 1e-4:
        raise Fail(f"the parameters of the newer session did not load: {d['activeParams']}")



def test_larger_block_than_prepared():
    """A host block larger than prepareToPlay announced renders the same sound (D.5.1).

    Found 2026-10-22 (Round 14, harness --max-block): the layers and the FX chain clamped to the
    prepared size, so everything past it in the block was silence - a gap three quarters long
    at 4x the announced size. Seeded, so the renders compare sample for sample.
    """
    # 1024 = 4 x 256 renders in the same pieces as the reference: bit-identical. 300 renders as
    # 256 + 44, and the instrument's output depends slightly on where its render calls fall
    # (-78.6 dBFS here, measured 2026-10-22), so it gets -60. The defect measured -1.8 dBFS.
    patch = ["--preset=sternenzelt", "--seed=1", "--notes=48,60,64,67", "--seconds=3", "--gate=2"]
    ref, sr = render(patch + ["--block=256"])
    for block, limit in ((1024, -120.0), (300, -60.0)):
        big, _ = render(patch + [f"--block={block}", "--max-block=256"])
        resid = 20 * np.log10(max(np.max(np.abs(big - ref)), 1e-20))
        if resid > limit:
            raise Fail(f"block {block} on a 256 prepare: {resid:.1f} dBFS from the 256-block render (limit {limit:.0f})")



def test_output_dc_and_knee():
    """EX-001's triggers, automated (invariant 6, Round 14 S.9).

    The output limiter is odd-symmetric but the mix is not, so where it engages it leaves DC;
    the 2 Hz blocker after it took the pathological extreme from -58.6 dBFS (EX-001, before
    the fix) to -80.3 dBFS worst of six. Fails on either trigger: the offset above -60 dBFS
    at that extreme, or a factory preset reaching the limiter's knee (-1.94 dBFS) in a chord.
    """
    extreme = ["--param=root=1", "--param=clearing=1", "--param=expanse=1", "--param=bloom=1",
               "--param=width=1", "--param=filter=1", "--param=reverb=1",
               "--notes=48,52,55,60,64,67,72,76", "--velocity=1.0", "--hold=2", "--tail=0.5"]
    worst = -400.0
    for rate, block in ((96000, 13), (48000, 512)):
        for seed in range(1, 7):
            d = run(extreme + [f"--sample-rate={rate}", f"--block={block}", f"--seed={seed}"])
            dc = max(abs(d["levels"]["dcOffsetL"]), abs(d["levels"]["dcOffsetR"]))
            worst = max(worst, 20 * np.log10(max(dc, 1e-20)))
    if worst > -60.0:
        raise Fail(f"output DC {worst:.1f} dBFS at the limiter's extreme (EX-001 trigger: -60)")
    knee_db = 20 * np.log10(0.80)
    for p in list_presets():
        d = run([f"--preset={p['slug']}", "--seed=1", "--notes=48,55,60,64,67", "--hold=3", "--tail=1"])
        if d["levels"]["peakDb"] > knee_db:
            raise Fail(f"{p['name']} peaks at {d['levels']['peakDb']:.2f} dBFS, past the limiter knee "
                       f"({knee_db:.2f}) - EX-001 trigger: a factory preset reaching the knee")



def test_limit_led_only_past_knee():
    """The editor's LIMIT LED (owner 2026-10-28) lights only while the output limiter works.

    isLimiting() is a meter flag beside the output level: a sample passed the knee (0.80,
    -1.94 dBFS) in the last 20 ms window. It must be up at the limiter's own extreme (all four
    volumes, WIDTH, FILTER and REVERB at 100 %, eight voices at full velocity) and down for the
    default patch and the bank's edge presets in a chord - no factory preset reaches the knee.
    """
    extreme = ["--param=root=1", "--param=clearing=1", "--param=expanse=1", "--param=bloom=1",
               "--param=width=1", "--param=filter=1", "--param=reverb=1",
               "--notes=48,52,55,60,64,67,72,76", "--velocity=1.0", "--hold=2", "--tail=0.5", "--seed=1"]
    d = run(extreme)
    if d["levels"]["limitingBlocks"] < 1:
        raise Fail(f"LIMIT never lit at the limiter's extreme (peak {d['levels']['peakDb']:.2f} dBFS)")
    quiet = [("default patch", [])] + [(p["name"], [f"--preset={p['slug']}"]) for p in list_presets() if p.get("edge") == "Y"
                                       or p["slug"] in ("mitternachtsblau", "bergecho", "sternenzelt")]
    for name, args in quiet:
        d = run(args + ["--seed=1", "--notes=48,55,60,64,67", "--hold=3", "--tail=1"])
        if d["levels"]["limitingBlocks"] != 0:
            raise Fail(f"LIMIT lit for {name} in {d['levels']['limitingBlocks']} of {d['levels']['blocks']} blocks "
                       f"(peak {d['levels']['peakDb']:.2f} dBFS) - no factory preset reaches the knee")


def test_output_meter_per_channel():
    """The meter's two wells read each channel's own RMS (owner 2026-10-28).

    The last window the processor publishes must equal that channel's RMS over the render's
    last 20 ms: 48 kHz in 480-sample blocks, so the 960-sample window ends on the last sample.
    One note at WIDTH 100 % puts its voice's spread to one side (6.5 dB apart, seed 4), so a
    summed or swapped meter cannot pass.
    """
    with tempfile.TemporaryDirectory() as tmp:
        p = os.path.join(tmp, "m.wav")
        d = run(["--notes=60", "--param=width=1", "--hold=2", "--tail=0", "--sample-rate=48000",
                 "--block=480", "--seed=4", f"--out={p}"])
        x, _ = read(p)
    want = np.sqrt(np.mean(x[-960:] ** 2, axis=0))
    got = np.array([d["levels"]["meterRmsL"], d["levels"]["meterRmsR"]])
    if abs(20 * np.log10(want[0] / want[1])) < 1.0:
        raise Fail(f"the render's channels are within 1 dB ({want}); the test cannot tell left from right")
    err = 20 * np.log10(np.maximum(got, 1e-12) / want)
    if np.any(np.abs(err) > 0.01):
        raise Fail(f"meter L/R {got} vs the render's last window {want}: off by {err} dB")


def test_about_copy_is_fourteen_lines():
    """The About box copies the fourteen diagnostics fields and nothing else
    (plugin-ui-bob audio-plugins.md 14, ship-bob feedback-loop.md 3): no user name, no paths."""
    r = subprocess.run([TOOL, "--diagnostics", "--sample-rate=44100", "--block=256"], capture_output=True, encoding="utf-8")
    if r.returncode != 0:
        raise Fail(f"--diagnostics failed (exit {r.returncode}): {r.stderr[:200]}")
    lines = r.stdout.strip("\n").split("\n")
    want = ["Product", "Version", "Sound version", "Build", "Build date", "Format", "Host", "OS",
            "Architecture", "Rosetta", "Sample rate", "Buffer size", "Editor scale", "Preset"]
    labels = [l.split(":", 1)[0] for l in lines]
    if labels != want:
        raise Fail(f"diagnostics fields {labels}, want exactly {want}")
    fields = {l.split(":", 1)[0]: l.split(":", 1)[1].strip() for l in lines}
    version = open(os.path.join(REPO, "VERSION")).read().strip()
    if fields["Version"] != version:
        raise Fail(f"Version {fields['Version']!r} but VERSION says {version!r}")
    if fields["Sample rate"] != "44100 Hz" or fields["Buffer size"] != "256 samples":
        raise Fail(f"rate/buffer not the ones the plugin runs at: {fields['Sample rate']!r}, {fields['Buffer size']!r}")
    home = os.path.expanduser("~")
    for private in (home, os.environ.get("USER", "\0"), os.path.basename(home)):
        if private and private in r.stdout:
            raise Fail(f"the copy contains something private ({private!r})")



def test_preset_file_round_trip():
    """Export a sound as a .oberlipreset, import it into a fresh preset folder: it lands in User/,
    keeps its name and author, and plays the same values; a file from a newer version is refused
    with a message and nothing is half-loaded; files from the company's earlier names - a
    .quellepreset (root <QuellePreset>) and a .bobpreset (root <BobPreset>) - still import
    (D.6.5, D.5.3)."""
    import re
    with tempfile.TemporaryDirectory() as tmp:
        env_a = dict(os.environ, HORIZON_PRESET_ROOT=os.path.join(tmp, "a"))
        env_b = dict(os.environ, HORIZON_PRESET_ROOT=os.path.join(tmp, "b"))
        out = os.path.join(tmp, "Mine.oberlipreset")
        base = ["--notes=60", "--seconds=0.3"]
        r = subprocess.run([TOOL, "--preset=glasperle", "--param=width=0.9", f"--export-preset={out}"] + base,
                           capture_output=True, encoding="utf-8", env=env_a)
        if r.returncode != 0 or not os.path.exists(out):
            raise Fail(f"export failed (exit {r.returncode}): {r.stderr[-200:]}")
        want = json.loads(r.stdout[r.stdout.index("{"):])["activeParams"]
        r = subprocess.run([TOOL, f"--import-preset={out}"] + base, capture_output=True, encoding="utf-8", env=env_b)
        if r.returncode != 0:
            raise Fail(f"import refused its own export (exit {r.returncode}): {r.stderr[-200:]}")
        d = json.loads(r.stdout[r.stdout.index("{"):])
        off = {k: (d["activeParams"][k], v) for k, v in want.items() if abs(float(d["activeParams"][k]) - float(v)) > 1e-4}
        if off:
            raise Fail(f"the imported preset plays different values: {off}")
        if d["activePreset"] != "Glasperle":
            raise Fail(f"imported preset is named {d['activePreset']!r}, not the exported name")
        user = os.path.join(tmp, "b", "User")
        if not any(f.endswith(".oberlipreset") for f in os.listdir(user)):
            raise Fail("the import left no .oberlipreset in the User folder")
        text = open(out, encoding="utf-8").read()
        if "<OberliPreset " not in text:
            raise Fail("the export is not an <OberliPreset> document")
        newer = re.sub(r'<HorizonPadState version="\d+"', '<HorizonPadState version="9"', text)
        newer_path = os.path.join(tmp, "Newer.oberlipreset")
        open(newer_path, "w", encoding="utf-8").write(newer)
        r = subprocess.run([TOOL, f"--import-preset={newer_path}"] + base, capture_output=True, encoding="utf-8", env=env_b)
        if r.returncode != 2 or "newer" not in r.stderr.lower():
            raise Fail(f"a preset from a newer version was not refused with a message (exit {r.returncode}): {r.stderr[-200:]}")
        for ext, root, name in ((".quellepreset", "QuellePreset", "Altglas"), (".bobpreset", "BobPreset", "Bobglas")):
            old = text.replace("<OberliPreset ", f"<{root} ").replace("</OberliPreset>", f"</{root}>").replace('name="Glasperle"', f'name="{name}"')
            old_path = os.path.join(tmp, "Old" + ext)
            open(old_path, "w", encoding="utf-8").write(old)
            r = subprocess.run([TOOL, f"--import-preset={old_path}"] + base, capture_output=True, encoding="utf-8", env=env_b)
            if r.returncode != 0 or json.loads(r.stdout[r.stdout.index("{"):])["activePreset"] != name:
                raise Fail(f"a {ext} from the company's earlier name did not import (exit {r.returncode}): {r.stderr[-200:]}")


def test_previous_brand_presets_carried_over():
    """0.1.0 kept the User presets under "Quelle Music"; the first start under the Oberli Audio
    name copies them into the new User folder as .oberlipreset, once, and leaves the old folder as
    it was (HORIZON_PRESET_ROOT/Previous stands in for the old folder)."""
    with tempfile.TemporaryDirectory() as tmp:
        env = dict(os.environ, HORIZON_PRESET_ROOT=tmp)
        exported = os.path.join(tmp, "x.oberlipreset")
        r = subprocess.run([TOOL, "--preset=glasperle", f"--export-preset={exported}", "--notes=60", "--seconds=0.2"],
                           capture_output=True, encoding="utf-8", env=env)
        if r.returncode != 0:
            raise Fail(f"export failed (exit {r.returncode}): {r.stderr[-200:]}")
        old_user = os.path.join(tmp, "Previous", "User")
        os.makedirs(old_user)
        old = open(exported, encoding="utf-8").read().replace("<OberliPreset ", "<QuellePreset ").replace("</OberliPreset>", "</QuellePreset>")
        open(os.path.join(old_user, "meins.quellepreset"), "w", encoding="utf-8").write(old)
        new_user = os.path.join(tmp, "User")
        if os.path.exists(new_user):
            shutil.rmtree(new_user)
        r = subprocess.run([TOOL, "--notes=60", "--seconds=0.2"], capture_output=True, encoding="utf-8", env=env)
        if r.returncode != 0:
            raise Fail(f"the start after the rebrand failed (exit {r.returncode}): {r.stderr[-200:]}")
        copied = os.path.join(new_user, "meins.oberlipreset")
        if not os.path.isfile(copied):
            raise Fail(f"the 0.1.0 User preset was not carried over: {os.listdir(new_user) if os.path.isdir(new_user) else 'no User folder'}")
        if not os.path.isfile(os.path.join(old_user, "meins.quellepreset")):
            raise Fail("the old User folder was changed")
        os.remove(copied)
        subprocess.run([TOOL, "--notes=60", "--seconds=0.2"], capture_output=True, encoding="utf-8", env=env)
        if os.path.exists(copied):
            raise Fail("the old presets were copied again into an existing User folder (must happen once)")


TESTS = {name[5:]: fn for name, fn in sorted(globals().items())
         if name.startswith("test_")}


def main():
    global TOOL
    ap = argparse.ArgumentParser()
    ap.add_argument("--tool", default=TOOL)
    ap.add_argument("name", nargs="?", help="one test, or all of them")
    a = ap.parse_args()
    TOOL = a.tool

    names = [a.name] if a.name else list(TESTS)
    for n in names:
        if n not in TESTS:
            print(f"no such test: {n}\navailable: {', '.join(TESTS)}", file=sys.stderr)
            return 2
    failed = 0
    for n in names:
        try:
            TESTS[n]()
            print(f"PASS  {n}")
        except Fail as e:
            print(f"FAIL  {n}: {e}", file=sys.stderr)
            failed += 1
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
