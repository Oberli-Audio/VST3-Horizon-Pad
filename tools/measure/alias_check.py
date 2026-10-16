#!/usr/bin/env python3
"""
Alias floor measurement by sample-rate comparison.

Why not the sound tool's ASR/NMR: those score energy that is not at a declared
partial, which on this instrument also catches the resonant bandpass ringing in
Expanse and every detuned-stack beat. At MIDI 108 that scored -10 dB ASR on a
layer whose real alias content is negligible - the metric was measuring the
layer's designed sound, not aliasing.

This instead renders the same note twice, at 48 kHz and at 192 kHz, with the
SAME oscillator seed so the two renders are the same signal. At 192 kHz an
alias that folds at 48 kHz either does not fold at all or folds somewhere
else, while every wanted partial sits at the same absolute frequency in both.
So per bin, below 20 kHz:

    excess_dB(f) = 20*log10( |X_48(f)| / |X_192(f)| )

is aliasing (plus a small amount of genuine sample-rate-dependent filter
behaviour, which is what makes a few tenths of a dB normal). The reported
number is the worst bin that carries meaningful energy.

Usage: alias_check.py [--tool PATH] [--notes 60,84,90,96]
(the register ends at C7 = MIDI 96 since 2026-10-17; above it nothing sounds)
"""
import argparse, json, subprocess, sys, os, tempfile
import numpy as np
from scipy.io import wavfile

LAYERS = ["root", "clearing", "expanse", "bloom"]


def render(tool, layer, note, sr, seed, outpath, bright=1.0):
    # Reverb and WIDTH are switched off: both are time-domain effects whose
    # output differs between two sample rates for reasons that are not
    # aliasing, and either one swamps the measurement.
    cmd = [tool, f"--solo={layer}", f"--notes={note}", f"--sample-rate={sr}",
           f"--seed={seed}", f"--param=filter={bright}", "--param=reverb=0",
           "--param=width=0",
           "--hold=3", "--tail=0.5", f"--out={outpath}", "--block=512"]
    r = subprocess.run(cmd, capture_output=True, text=True)
    if r.returncode != 0:
        raise RuntimeError(f"render failed: {r.stderr[:400]}")
    return json.loads(r.stdout[r.stdout.index('{'):])


def read_wav_mono(path):
    """32-bit float WAV; python's `wave` cannot read format 3, scipy can."""
    sr, x = wavfile.read(path)
    x = np.asarray(x, dtype=np.float64)
    if x.ndim > 1:
        x = x.mean(axis=1)
    return x, sr


def spectrum(x, sr, start_s, dur_s, nfft):
    """nfft is passed in scaled with the sample rate by the caller, so both
    renders are analysed at the SAME bin width in Hz. Comparing a 48 kHz
    spectrum against a coarser 192 kHz one smooths the latter's peaks and
    shows up as excess that is not there."""
    a = int(start_s * sr)
    b = min(len(x), a + int(dur_s * sr))
    seg = x[a:b]
    if len(seg) < nfft:
        return None, None
    # Average several Hann-windowed frames for a stable magnitude estimate.
    win = np.hanning(nfft)
    frames, step = [], nfft // 2
    for o in range(0, len(seg) - nfft + 1, step):
        frames.append(np.abs(np.fft.rfft(seg[o:o + nfft] * win)))
    if not frames:
        return None, None
    mag = np.mean(frames, axis=0)
    freqs = np.fft.rfftfreq(nfft, 1.0 / sr)
    return freqs, mag


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--tool", default="./build-release/HorizonPadSoundTool")
    ap.add_argument("--notes", default="60,84,90,96")
    ap.add_argument("--seed", type=int, default=7)
    ap.add_argument("--json", action="store_true")
    args = ap.parse_args()

    notes = [int(n) for n in args.notes.split(",")]
    tmp = tempfile.mkdtemp(prefix="aliaschk")
    results = []

    for layer in LAYERS:
        for note in notes:
            lo = os.path.join(tmp, f"{layer}_{note}_48.wav")
            hi = os.path.join(tmp, f"{layer}_{note}_192.wav")
            m48 = render(args.tool, layer, note, 48000, args.seed, lo)
            render(args.tool, layer, note, 192000, args.seed, hi)

            x48, sr48 = read_wav_mono(lo)
            x192, sr192 = read_wav_mono(hi)
            nfft48 = 1 << 15
            f48, s48 = spectrum(x48, sr48, 1.0, 2.0, nfft48)
            f192, s192 = spectrum(x192, sr192, 1.0, 2.0,
                                  nfft48 * int(round(sr192 / sr48)))
            if s48 is None or s192 is None:
                continue

            # Compare on the 48 kHz grid, below 20 kHz, against the 192 kHz
            # render interpolated onto it.
            band = f48 <= 20000.0
            fb, a48 = f48[band], s48[band]
            a192 = np.interp(fb, f192, s192)

            peak = a48.max() if a48.size else 0.0
            if peak <= 0:
                continue
            floor = peak * (10 ** (-120 / 20))
            live = (a48 > floor) & (a192 > 0)

            excess = 20 * np.log10(a48[live] / a192[live])
            rel = 20 * np.log10(a48[live] / peak)   # bin level vs this layer's peak
            flive = fb[live]

            # The number that matters is not "which bin has the biggest ratio"
            # - a near-silent bin divided by a more-near-silent one wins that
            # and means nothing. It is "how loud is the loudest thing here that
            # is actually alias". EXCESS_GATE decides what counts as alias
            # rather than ordinary sample-rate-dependent filter behaviour.
            EXCESS_GATE = 10.0
            is_alias = excess > EXCESS_GATE
            if is_alias.any():
                k = int(np.argmax(rel[is_alias]))
                worst_rel = float(rel[is_alias][k])
                worst_f = float(flive[is_alias][k])
                worst_excess = float(excess[is_alias][k])
            else:
                worst_rel, worst_f, worst_excess = float('-inf'), 0.0, 0.0

            results.append(dict(layer=layer, note=note, peakDb=m48['levels']['peakDb'],
                                worstAliasRelDb=worst_rel, atHz=worst_f,
                                excessDb=worst_excess))

    if args.json:
        print(json.dumps(results, indent=1))
    else:
        print(f"{'layer':10s} {'note':>5s} {'peak dBFS':>10s} "
              f"{'loudest alias':>14s} {'at Hz':>9s} {'excess':>8s}")
        for r in results:
            v = r['worstAliasRelDb']
            shown = "     none" if v == float('-inf') else f"{v:10.1f} dB"
            print(f"{r['layer']:10s} {r['note']:5d} {r['peakDb']:10.1f} "
                  f"{shown:>14s} {r['atHz']:9.0f} {r['excessDb']:7.1f}dB")
        worst = [r for r in results if r['worstAliasRelDb'] != float('-inf')]
        if worst:
            w = max(worst, key=lambda r: r['worstAliasRelDb'])
            print(f"\nWorst alias anywhere: {w['worstAliasRelDb']:.1f} dB below its layer's "
                  f"peak ({w['layer']}, MIDI {w['note']}, {w['atHz']:.0f} Hz)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
