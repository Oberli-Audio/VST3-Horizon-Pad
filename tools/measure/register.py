#!/usr/bin/env python3
"""
Register behaviour: does each layer stay musical from the bass to the top?

A pad that sounds right at C4 can stop being music two octaves down. Three
psychoacoustic reasons, each measured here per layer and per note:

  roughness      Plomp-Levelt / Vassilakis roughness of the steady-state
                 spectrum, normalised to the loudest partial. Below ~500 Hz a
                 critical band is ~100 Hz wide - wider than the harmonic
                 spacing of a low note - so adjacent harmonics fall inside one
                 band and grate. Two low notes a third apart are rougher still.
  wobble_pct     Slow amplitude fluctuation, 0.1-8 Hz, as % of the mean level.
                 Detuned unison beats at a rate proportional to frequency, so
                 in the bass it turns from shimmer into a slow, deep wobble.
  harmonics      Spectral centroid divided by the fundamental: how many
                 harmonics' worth of brightness the note carries. A fixed-Hz
                 filter makes low notes buzz with many harmonics and high notes
                 thin; musical key scaling keeps this in a sane range.
  mud_share_db   Share of energy in 150-400 Hz.
  salience       Pitch salience: normalised autocorrelation at the note's
                 period, averaged over 4-period frames. 1 = a clean, stable
                 pitch; it falls when unresolved harmonics and slow phasing
                 blur the period, which is how a low note stops sounding like
                 a note.
  side_low_db    Energy of the side signal (L-R)/2 below 150 Hz relative to the
                 mid below 150 Hz. Bass should be mono: a PA sums it, and a
                 stereo sub reads as phasey.

Plus the same for two low voicings a keys player uses: an open fifth
(C2 G2 C3) and a close triad (C2 E2 G2), where roughness is the voicing's
own and the instrument can only make it worse or better.

    python3 tools/measure/register.py [--tool path] [--seeds 3] [--save out.json]
"""
import argparse
import json
import os
import subprocess
import sys
import tempfile

import numpy as np
from scipy import signal
from scipy.io import wavfile

SR = 48000
LAYERS = ["root", "clearing", "expanse", "bloom"]
NOTES = [12, 24, 36, 48, 60, 72, 84, 96]      # C0 .. C7: the whole register (brief A-010)
CHORDS = {"open5_C2": "36,43,48", "triad_C2": "36,40,43"}
TOOL = "build-release/HorizonPadSoundTool"


def render(args, seed, hold=8.0):
    fd, path = tempfile.mkstemp(suffix=".wav")
    os.close(fd)
    try:
        subprocess.run([TOOL, f"--hold={hold}", "--tail=0.2", f"--seed={seed}", f"--sample-rate={SR}",
                        "--param=reverb=0", f"--out={path}"] + args, check=True, capture_output=True)
        _, x = wavfile.read(path)
    finally:
        os.remove(path)
    return x.astype(np.float64)


def peaks(mono):
    """Partials of the steady state: long window, fine resolution, -50 dB floor."""
    n = 1 << 16                                        # 0.73 Hz bins at 48 kHz
    seg = mono[:n] if len(mono) >= n else np.pad(mono, (0, n - len(mono)))
    mag = np.abs(np.fft.rfft(seg * signal.windows.blackmanharris(n)))
    f = np.fft.rfftfreq(n, 1.0 / SR)
    # distance >= 8 Hz: partials closer than that are slow beats (unison
    # detune, vibrato sidebands), which the ear hears as movement, not as
    # roughness (15-300 Hz modulation). Counting them made a chorus score
    # as "rough".
    idx, _ = signal.find_peaks(mag, height=mag.max() * 10 ** (-50 / 20), distance=int(8.0 / (SR / n)) + 1)
    idx = idx[(f[idx] > 15) & (f[idx] < 12000)]
    return f[idx], mag[idx] / mag.max()


def roughness(freqs, amps):
    """Vassilakis (2001) roughness of a set of partials."""
    total = 0.0
    for i in range(len(freqs)):
        for j in range(i + 1, len(freqs)):
            f1, f2 = sorted((freqs[i], freqs[j]))
            a1, a2 = amps[i], amps[j]
            s = 0.24 / (0.0207 * f1 + 18.96)
            z = np.exp(-3.5 * s * (f2 - f1)) - np.exp(-5.75 * s * (f2 - f1))
            total += (a1 * a2) ** 0.1 * 0.5 * (2 * min(a1, a2) / (a1 + a2)) ** 3.11 * z
    return float(total)


def wobble(mono):
    w = int(0.05 * SR)
    env = np.sqrt(np.convolve(mono ** 2, np.ones(w) / w, mode="same"))[w:-w]
    env = env / env.mean()
    b, a = signal.butter(2, [0.1, 8.0], btype="band", fs=SR)
    return float(100 * np.sqrt(2) * np.std(signal.filtfilt(b, a, env)))


def salience(mono, f0):
    period = SR / f0
    frame = int(4 * period)
    lags = np.arange(int(period * 0.97), int(period * 1.03) + 2)
    vals = []
    for start in range(0, len(mono) - frame - lags[-1], max(frame, int(0.05 * SR))):
        a = mono[start:start + frame]
        e = np.sqrt((a ** 2).sum()) + 1e-20
        best = max(float(np.dot(a, mono[start + L:start + L + frame])) /
                   (e * (np.sqrt((mono[start + L:start + L + frame] ** 2).sum()) + 1e-20)) for L in lags)
        vals.append(best)
    return float(np.mean(vals))


def describe(x, f0):
    ss = x[int(4.0 * SR):int(8.0 * SR)]
    mono = ss.mean(axis=1)
    fr, am = peaks(mono)
    f, p = signal.welch(mono, SR, nperseg=8192)
    centroid = float((f * p).sum() / p.sum())
    mud = float(10 * np.log10(p[(f >= 150) & (f < 400)].sum() / p[(f >= 20)].sum() + 1e-12))
    side = 0.5 * (ss[:, 0] - ss[:, 1])
    b, a = signal.butter(4, 150, fs=SR)
    lo_mid = signal.lfilter(b, a, mono)
    lo_side = signal.lfilter(b, a, side)
    side_low = float(10 * np.log10((lo_side ** 2).mean() / ((lo_mid ** 2).mean() + 1e-20) + 1e-12))
    return {"roughness": roughness(fr, am), "wobble_pct": wobble(mono), "salience": salience(mono, f0),
            "harmonics": centroid / f0, "mud_share_db": mud, "side_low_db": side_low}


def mean_of(dicts):
    return {k: round(float(np.mean([d[k] for d in dicts])), 3) for k in dicts[0]}


def main():
    global TOOL
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--tool", default=TOOL)
    ap.add_argument("--seeds", type=int, default=3)
    ap.add_argument("--save")
    ap.add_argument("--layers", default=",".join(LAYERS))
    args = ap.parse_args()
    TOOL = args.tool
    seeds = list(range(21, 21 + args.seeds))

    out = {}
    for layer in args.layers.split(","):
        row = {}
        for note in NOTES:
            f0 = 440.0 * 2 ** ((note - 69) / 12)
            row[f"n{note}"] = mean_of([describe(render([f"--solo={layer}", f"--notes={note}"], s), f0)
                                        for s in seeds])
        for name, notes in CHORDS.items():
            f0 = 440.0 * 2 ** ((36 - 69) / 12)
            row[name] = mean_of([describe(render([f"--solo={layer}", f"--notes={notes}"], s), f0)
                                  for s in seeds])
        out[layer] = row
        print(f"[register] {layer} done", file=sys.stderr, flush=True)

    text = json.dumps(out, indent=1)
    if args.save:
        with open(args.save, "w") as fh:
            fh.write(text + "\n")
    print(text)


if __name__ == "__main__":
    main()
