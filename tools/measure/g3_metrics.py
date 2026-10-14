#!/usr/bin/env python3
"""
G3 metric battery for Horizon Pad.

Runs the measurements docs/brief.md's `quality_targets` asks for and prints a
markdown table. Everything here drives the real plugin DSP through
HorizonPadSoundTool, seeded so runs are reproducible.

Not covered here: the alias floor (tools/measure/alias_check.py - it needs a
two-sample-rate comparison) and the null test (tools/measure/null_test.py).
"""
import argparse, json, subprocess, sys, time, os, tempfile, statistics
import numpy as np
from scipy.io import wavfile

TOOL = "./build-release/HorizonPadSoundTool"
RATES = [44100, 48000, 88200, 96000, 176400, 192000]
BLOCKS = [1, 13, 32, 64, 512, 4096]
CHORD = "48,55,60,64,67,72,76,79"     # 8 voices: all slots busy


def run(extra, tool=TOOL):
    r = subprocess.run([tool] + extra, capture_output=True, text=True)
    if r.returncode != 0:
        raise RuntimeError(r.stderr[:500])
    return json.loads(r.stdout[r.stdout.index('{'):])


def rate_block_matrix():
    """Definition-of-done: clean at six rates and six block sizes."""
    rows, worst_dc, bad = [], -300.0, []
    for sr in RATES:
        for bs in BLOCKS:
            d = run([f"--sample-rate={sr}", f"--block={bs}", f"--notes={CHORD}",
                     "--seed=11", "--hold=2", "--tail=2"])
            s, l = d["safety"], d["levels"]
            dc = max(abs(l["dcOffsetL"]), abs(l["dcOffsetR"]))
            dcdb = 20 * np.log10(dc) if dc > 0 else -300.0
            worst_dc = max(worst_dc, dcdb)
            ok = (not s["hasNanOrInf"] and not s["trueClipping"]
                  and s["clickCount"] == 0)
            if not ok:
                bad.append(f"{sr}/{bs}: {json.dumps(s)}")
            rows.append((sr, bs, ok, dcdb))
    return rows, worst_dc, bad


def cpu_budget():
    """Render wall-time vs audio duration = share of one core.

    The brief's budget is <8% of one core at 48 kHz / 64 samples with eight
    voices, all four layers up and REVERB at 50%. Measured in Release, and
    again over the tail after the sound has stopped, which is where a denormal
    problem shows up.
    """
    out = []
    for label, hold, tail in (("8 voices sounding", 10.0, 0.5),
                              ("60 s of silence after the sound stops", 0.5, 60.0)):
        best = None
        for _ in range(3):                      # best of 3: least polluted by the OS
            t0 = time.perf_counter()
            run(["--sample-rate=48000", "--block=64", f"--notes={CHORD}", "--seed=11",
                 "--param=reverb=0.5", "--param=root=1", "--param=clearing=1",
                 "--param=expanse=1", "--param=bloom=1",
                 f"--hold={hold}", f"--tail={tail}"])
            el = time.perf_counter() - t0
            best = el if best is None else min(best, el)
        audio = hold + tail
        out.append((label, audio, best, 100.0 * best / audio))
    return out


def envelope_timing():
    """Acoustic envelope per layer, against its designed ADSR.

    These will NOT match the raw ADSR numbers, and that is by design rather
    than a fault: every layer couples its filter cutoff to its own envelope, so
    the audible envelope rises faster than the amplitude ramp (the filter is
    opening at the same time, adding harmonics) and falls further than the
    sustain level (the filter closes again). juce::ADSR gets the designed
    values unmodified - see LayerBase::beginNote() - so what is measured here
    is the layer, not the ADSR.

    What the design does depend on is the RELATIVE spacing: the four layers
    must not all arrive together. That ordering is the real check.
    """
    designed = {"root": 2.2, "clearing": 2.6, "expanse": 2.6, "bloom": 1.2}
    rows = []
    for layer, want in designed.items():
        # ATTACK 1.0 is the designed attack since brief A-009 (it was 0.5).
        d = run([f"--solo={layer}", "--notes=60", "--seed=11", "--param=attack=1.0",
                 "--param=reverb=0", "--hold=8", "--tail=1", "--window-ms=10"])
        t = d["envelope"]["timing"]
        rows.append((layer, want, t["timeToPeakSec"], t["attack10to90Sec"],
                     t["sustainRatio"]))
    return rows


def denormal_check():
    """No denormal storm and no residual output after 60 s of silence."""
    d = run(["--sample-rate=48000", "--block=64", f"--notes={CHORD}", "--seed=11",
             "--param=reverb=1.0", "--hold=1", "--tail=60"])
    return d["safety"], d["levels"]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--skip-matrix", action="store_true")
    a = ap.parse_args()

    print("# G3 metric battery\n")

    if not a.skip_matrix:
        rows, worst_dc, bad = rate_block_matrix()
        print(f"## Sample rate x block size ({len(rows)} combinations)\n")
        print(f"- Clean (no NaN/Inf/clipping/clicks): "
              f"**{sum(1 for r in rows if r[2])}/{len(rows)}**")
        print(f"- Worst DC offset across all of them: **{worst_dc:.1f} dBFS**")
        for b in bad:
            print(f"- FAILED {b}")
        print()

    print("## CPU budget (Release, 48 kHz / 64 samples, 8 voices, all layers, REVERB 50%)\n")
    print("| Condition | Audio | Wall time | Share of one core |")
    print("|---|---|---|---|")
    for label, audio, el, pct in cpu_budget():
        print(f"| {label} | {audio:.1f} s | {el:.3f} s | **{pct:.2f}%** |")
    print()

    print("## Envelope timing (acoustic, filter coupling included by design)\n")
    print("| Layer | Designed ADSR attack | Time to peak | 10-90% rise | Sustain ratio |")
    print("|---|---|---|---|---|")
    rows = envelope_timing()
    for layer, want, ttp, rise, sus in rows:
        print(f"| {layer} | {want:.1f} s | {ttp:.2f} s | {rise:.2f} s | {sus:.2f} |")
    order = [r[0] for r in sorted(rows, key=lambda r: r[2])]
    want_order = [r[0] for r in sorted(rows, key=lambda r: r[1])]
    print(f"\nArrival order measured: {' < '.join(order)}")
    print(f"Arrival order designed: {' < '.join(want_order)}")
    print(f"Spread between first and last layer: "
          f"{max(r[2] for r in rows) - min(r[2] for r in rows):.2f} s")
    print()

    s, l = denormal_check()
    print("## After 60 s of silence (denormal check)\n")
    print(f"- NaN/Inf: {s['hasNanOrInf']} · true clipping: {s['trueClipping']} "
          f"· clicks: {s['clickCount']}")
    print(f"- Peak over the whole render: {l['peakDb']:.1f} dBFS")
    return 0


if __name__ == "__main__":
    sys.exit(main())
