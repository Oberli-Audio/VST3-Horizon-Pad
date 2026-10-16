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
import argparse, hashlib, json, os, subprocess, sys, tempfile
import numpy as np
from scipy.io import wavfile

CHORD = "48,55,60,64,67,72,76,79"
TOOL = "./build-release/HorizonPadSoundTool"


class Fail(Exception):
    pass


def run(extra):
    r = subprocess.run([TOOL] + extra, capture_output=True, text=True)
    if r.returncode != 0:
        raise Fail(f"tool exited {r.returncode}: {r.stderr[:400]}")
    return json.loads(r.stdout[r.stdout.index('{'):])


def list_presets():
    """--list-presets emits a JSON array, not the usual object."""
    r = subprocess.run([TOOL, "--list-presets"], capture_output=True, text=True)
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
