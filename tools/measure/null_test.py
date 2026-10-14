#!/usr/bin/env python3
"""
G5: the C++ port against the Faust prototype, per layer.

A sample-accurate null is not available here, for two reasons that are both
deliberate rather than accidental:

  1. The C++ randomises every voice's oscillator start phases (see LayerBase's
     rng). --seed makes that reproducible but not zero, and the prototype
     starts every oscillator at phase 0. Two signals that differ only in start
     phase do not null, and making them null would mean removing the
     randomisation the product wants.
  2. The port deliberately replaced the prototype's `fi.lowpass(2, ...)`
     Butterworth biquads with TPT/ZDF state-variable filters (playbook
     invariant #9, since every one of these filters is modulated). Different
     topology, different response - an improvement, and one that guarantees a
     residual.

So this runs playbook 6.1's other form for G5: characterise the difference by
band, and declare what is intentional. The comparison is per LAYER and dry -
oscillators, filter, envelope - because that is the part that was ported
structurally. The master stage was redesigned outright (stereo WIDTH, an
equal-power reverb crossfade, a limiter) and is out of scope here; see
docs/g1-algorithm.md.

Both sides are RMS-matched before comparison, so what is reported is spectral
shape, not level.
"""
import json, subprocess, os, tempfile, sys
import numpy as np
from scipy.io import wavfile

CPP = "./build-release/HorizonPadSoundTool"
FAUST = "./build/faust/g5_layers_render"
# channel index in g5_layers.dsp -> C++ --solo name
LAYERS = [(0, "root", "Warm Foundation"), (1, "clearing", "Analog Ensemble"),
          (2, "expanse", "Airy Choir"), (3, "bloom", "Motion Pad")]
FREQ_C4, NOTE_C4 = 261.6255653, 60
BANDS = [(20, 80), (80, 160), (160, 320), (320, 640), (640, 1280),
         (1280, 2560), (2560, 5120), (5120, 10240), (10240, 20000)]


def read(path):
    sr, x = wavfile.read(path)
    return np.asarray(x, dtype=np.float64), sr


def avg_spectrum(x, sr, t0, t1, nfft=1 << 14):
    seg = x[int(t0 * sr):int(t1 * sr)]
    win = np.hanning(nfft)
    frames = [np.abs(np.fft.rfft(seg[o:o + nfft] * win))
              for o in range(0, len(seg) - nfft + 1, nfft // 2)]
    if not frames:
        return None, None
    return np.fft.rfftfreq(nfft, 1.0 / sr), np.mean(frames, axis=0)


def main():
    tmp = tempfile.mkdtemp(prefix="g5")
    fa = os.path.join(tmp, "proto.wav")
    subprocess.run([FAUST, "--dur=6", "--gate-on=0", "--gate-off=4",
                    f"--freq={FREQ_C4}", "--gain=1.0", "--sample-rate=48000",
                    f"--out={fa}"], check=True, capture_output=True)
    P, sr = read(fa)

    print("# G5 - port vs prototype, per layer\n")
    print("RMS-matched, steady state (1.5-3.5 s of a held C4), 48 kHz.\n")
    print("| Layer | Broadband delta | Worst band | Worst band delta |")
    print("|---|---|---|---|")

    detail = []
    for ch, solo, name in LAYERS:
        cf = os.path.join(tmp, f"{solo}.wav")
        subprocess.run([CPP, f"--solo={solo}", f"--notes={NOTE_C4}", "--seed=11",
                        # ATTACK/RELEASE 1.0 is the designed envelope since
                        # brief A-009 (2026-10-14; it was 0.5 before).
                        "--velocity=1.0", "--param=reverb=0", "--param=attack=1.0",
                        "--param=release=1.0", "--param=filter=0.5",
                        "--param=width=0",
                        "--hold=4", "--tail=2", "--sample-rate=48000",
                        f"--out={cf}"], check=True, capture_output=True)
        C, _ = read(cf)
        c = C.mean(axis=1) if C.ndim > 1 else C
        p = P[:, ch]

        f, sp = avg_spectrum(p, sr, 1.5, 3.5)
        _, sc = avg_spectrum(c, sr, 1.5, 3.5)
        # RMS-match so this reports shape, not level.
        sp = sp / np.sqrt((sp ** 2).sum())
        sc = sc / np.sqrt((sc ** 2).sum())

        rows, worst, worst_band = [], 0.0, None
        for lo, hi in BANDS:
            m = (f >= lo) & (f < hi)
            ep, ec = np.sqrt((sp[m] ** 2).sum()), np.sqrt((sc[m] ** 2).sum())
            if ep <= 0 and ec <= 0:
                continue
            d = 20 * np.log10(max(ec, 1e-20) / max(ep, 1e-20))
            rows.append((lo, hi, d, ep, ec))
            # Only count bands that carry real energy in at least one side.
            if max(ep, ec) > 0.02 and abs(d) > abs(worst):
                worst, worst_band = d, (lo, hi)

        broad = 20 * np.log10(np.sqrt((sc ** 2).sum()) / np.sqrt((sp ** 2).sum()))
        wb = f"{worst_band[0]}-{worst_band[1]} Hz" if worst_band else "-"
        print(f"| {name} | {broad:+.2f} dB | {wb} | {worst:+.1f} dB |")
        detail.append((name, rows))

    print("\n## Per-band detail (C++ minus prototype, dB; '.' = below 2% of energy)\n")
    hdr = " | ".join(f"{lo}-{hi}" for lo, hi in BANDS)
    print(f"| Layer | {hdr} |")
    print("|" + "---|" * (len(BANDS) + 1))
    for name, rows in detail:
        cells = []
        for lo, hi, d, ep, ec in rows:
            cells.append(f"{d:+.1f}" if max(ep, ec) > 0.02 else ".")
        print(f"| {name} | " + " | ".join(cells) + " |")
    return 0


if __name__ == "__main__":
    sys.exit(main())
