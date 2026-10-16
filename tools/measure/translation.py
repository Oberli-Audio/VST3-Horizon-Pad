#!/usr/bin/env python3
"""Translation battery (playbook F.7, rule 34): does Horizon Pad survive the places it
will be heard?

Renders every factory preset (or --presets) with the open worship voicing used by
descriptors.py, over several seeds, and measures:

  mono      L+R sum against stereo: broadband loss, worst octave band (a comb shows
            as one band far below the rest), and the band below 120 Hz, brick-wall (rule 26:
            bass must already be mono, so it must not lose anything)
  phone     a small-speaker model - 4th-order high-pass at 200 Hz, 2nd-order low-pass
            at 10 kHz: how much loudness survives, against the bank's median
  normalise the peak-to-loudness ratio, and the true peak the render would reach if
            it were turned up to -14 LUFS (streaming) - information, not a pass mark:
            no playback chain raises a track past its peaks without a limiter
  codec     the render mastered to -1 dBTP (AES TD1008: the level a delivered master
            keeps before lossy encoding), through AAC 128 kbit/s (afconvert, decoded to
            float) and MP3 128 kbit/s (lame, decodes to 16 bit, so overshoot past 0 dBFS
            shows as clipped samples): decoded true peak, and the residual in the
            reverb tail (where decorrelated noise turns into codec artefacts)
  pa        a big mono PA: the mono sum's level below 120 Hz relative to the whole

Pass marks are defaults, not physics (playbook F.0.2) - stated here, in one place:
  mono broadband loss >= -3.5 LU; worst band >= -6 dB; bass band >= -0.5 dB
  phone loss within 3 LU of the bank median
  AAC decoded true peak <= 0 dBTP; MP3 decode without clipped samples
  (until 2026-10-03 the battery also failed "true peak at -14 LUFS > -1 dBTP"; that
  modelled a +4 dB gain with no limiter, which no delivery chain applies, so it is
  reported now, not judged - the codec row tests the real delivery case)

The mix-context check needs the brief's `mix_context`, which this brief does not have;
descriptors.py's "pocket" section is the nearest measurement.

Needs numpy, scipy, the playbook's dspkit (~/bob-skills/sound-design-bob/scripts/dsp-kit, or $DSPKIT) for BS.1770
loudness and true peak, and afconvert / lame for the codec row (skipped if missing).

    python3 tools/measure/translation.py                  # all presets, 3 seeds
    python3 tools/measure/translation.py --presets Lagerfeuer,Sternenzelt --seeds 1
    python3 tools/measure/translation.py --json docs/baselines/translation-v2.json
"""
import argparse
import json
import os
import shutil
import statistics
import subprocess
import sys
import tempfile
import warnings
from pathlib import Path

import numpy as np
from scipy import signal

warnings.filterwarnings("ignore", message="Chunk")   # afconvert/lame write chunks scipy skips
KIT = Path(os.environ.get("DSPKIT", Path.home() / "bob-skills" / "sound-design-bob" / "scripts" / "dsp-kit"))
sys.path.insert(0, str(KIT))
try:
    import dspkit as K
except ImportError:
    sys.exit("translation.py needs dspkit.py from the DSP playbook kit (%s)" % KIT)

CHORD = "48,55,60,64"            # descriptors.py's open voicing: C3 G3 C4 E4
FS = 48000
OCTAVES = [31.5, 63, 125, 250, 500, 1000, 2000, 4000, 8000, 16000]


def render(tool, preset, seed, out):
    cmd = [tool, "--preset=" + preset, "--notes=" + CHORD, "--seed=%d" % seed,
           "--sample-rate=%d" % FS, "--hold=4", "--tail=3", "--out=" + str(out)]
    r = subprocess.run(cmd, capture_output=True, text=True)
    if r.returncode != 0:
        raise RuntimeError("render failed: %s\n%s" % (" ".join(cmd), r.stderr))
    x, fs = K.read_wav(out)
    assert fs == FS
    return x


def band_energy(x, lo, hi):
    sos = signal.butter(4, [lo, hi], "bandpass", fs=FS, output="sos")
    y = signal.sosfilt(sos, x, axis=0)
    return float(np.sum(y ** 2))


BASS_HZ = 120.0


def fft_band_energy(x, hi_hz):
    spec = np.fft.rfft(x, axis=0)
    f = np.fft.rfftfreq(len(x), 1.0 / FS)
    sel = (f >= 20.0) & (f < hi_hz)
    return float(np.sum(np.abs(spec[sel]) ** 2))


def mono_rows(x):
    m = 0.5 * (x[:, 0] + x[:, 1])
    mono = np.stack([m, m], axis=1)
    broadband = K.integrated_lufs(mono, FS) - K.integrated_lufs(x, FS)
    bands = {}
    for f in OCTAVES:
        lo, hi = f / np.sqrt(2), min(f * np.sqrt(2), 0.45 * FS)
        es, em = band_energy(x, lo, hi), band_energy(mono, lo, hi)
        if es > 1e-12:
            bands[f] = 10 * np.log10(em / es)
    # Rule 26's band, brick-wall in the frequency domain: a filter skirt would let the
    # voicing's lowest fundamental (C3, 131 Hz - inside the 140 Hz side high-pass's
    # transition) leak in and read as "stereo bass" (it did, in the first version).
    lo_s, lo_m = fft_band_energy(x, BASS_HZ), fft_band_energy(mono, BASS_HZ)
    bass = 10 * np.log10(lo_m / lo_s) if lo_s > 1e-12 else 0.0
    audible = {f: v for f, v in bands.items() if f >= 63}
    worst_f = min(audible, key=audible.get)
    pa = 10 * np.log10(lo_m / max(fft_band_energy(mono, 0.5 * FS), 1e-30))
    return {"mono_lu": broadband, "mono_worst_band_db": audible[worst_f],
            "mono_worst_band_hz": worst_f, "mono_bass_db": bass, "pa_bass_share_db": pa}


def phone_row(x):
    hp = signal.butter(4, 200, "highpass", fs=FS, output="sos")
    lp = signal.butter(2, 10000, "lowpass", fs=FS, output="sos")
    y = signal.sosfilt(lp, signal.sosfilt(hp, x, axis=0), axis=0)
    return {"phone_lu": K.integrated_lufs(y, FS) - K.integrated_lufs(x, FS)}


def normalise_row(x):
    lufs = K.integrated_lufs(x, FS)
    tp = K.true_peak_dbtp(x, FS, oversample=8)
    return {"lufs": lufs, "true_peak_dbtp": tp, "plr_db": tp - lufs,
            "tp_at_minus14_dbtp": tp + (-14.0 - lufs)}


def codec_rows(x, tmp):
    rows = {}
    g = K.undb(-1.0 - K.true_peak_dbtp(x, FS, oversample=8))     # mastered to -1 dBTP
    y = x * g
    src = tmp / "src.wav"
    K.write_wav(src, y, FS)                                      # float: nothing clipped on the way in
    tail = slice(int(5.0 * FS), int(6.5 * FS))        # reverb tail after the release
    codecs = []
    if shutil.which("afconvert"):
        codecs.append(("aac128", ["afconvert", "-f", "m4af", "-d", "aac", "-b", "128000", str(src), str(tmp / "c.m4a")],
                       ["afconvert", "-f", "WAVE", "-d", "LEF32", str(tmp / "c.m4a"), str(tmp / "aac.wav")], tmp / "aac.wav"))
    if shutil.which("lame"):
        codecs.append(("mp3_128", ["lame", "--quiet", "-b", "128", str(src), str(tmp / "c.mp3")],
                       ["lame", "--quiet", "--decode", str(tmp / "c.mp3"), str(tmp / "mp3.wav")], tmp / "mp3.wav"))
    for name, enc, dec, out in codecs:
        subprocess.run(enc, check=True, capture_output=True)
        subprocess.run(dec, check=True, capture_output=True)
        z, fs = K.read_wav(out)
        lag = K.latency_samples(y[:, 0], z[:, 0], max_lag=4096)
        z = z[lag:] if lag > 0 else z
        n = min(len(z), len(y))
        resid = z[:n] - y[:n]
        t_sig = float(np.sqrt(np.mean(y[tail] ** 2)))
        t_res = float(np.sqrt(np.mean(resid[tail] ** 2)))
        rows[name + "_tp_dbtp"] = K.true_peak_dbtp(z, FS, oversample=8)
        rows[name + "_overshoot_db"] = rows[name + "_tp_dbtp"] - K.true_peak_dbtp(y, FS, oversample=8)
        rows[name + "_clipped"] = int(np.sum(np.abs(z) >= 32767.0 / 32768.0))
        rows[name + "_tail_residual_db"] = float(K.db(t_res / t_sig)) if t_sig > 0 else float("nan")
    return rows


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--tool", default="./build-release/HorizonPadSoundTool")
    ap.add_argument("--presets", default="all")
    ap.add_argument("--seeds", type=int, default=3)
    ap.add_argument("--json")
    a = ap.parse_args()

    if a.presets == "all":
        r = subprocess.run([a.tool, "--list-presets"], capture_output=True, text=True, check=True)
        listed = json.loads(r.stdout)
        presets = [p["name"] if isinstance(p, dict) else p for p in (listed.get("presets", listed) if isinstance(listed, dict) else listed)]
    else:
        presets = a.presets.split(",")

    results = {}
    with tempfile.TemporaryDirectory() as d:
        tmp = Path(d)
        for preset in presets:
            per_seed = []
            for seed in range(1, a.seeds + 1):
                x = render(a.tool, preset, seed, tmp / "r.wav")
                row = {}
                row.update(mono_rows(x))
                row.update(phone_row(x))
                row.update(normalise_row(x))
                if seed == 1:
                    row.update(codec_rows(x, tmp))
                per_seed.append(row)
            keys = per_seed[0].keys()
            med = {}
            for k in keys:
                vals = [r[k] for r in per_seed if k in r]
                med[k] = statistics.median(vals) if not isinstance(vals[0], str) else vals[0]
            # worst case across seeds for the pass/fail quantities
            med["mono_worst_band_db"] = min(r["mono_worst_band_db"] for r in per_seed)
            med["tp_at_minus14_dbtp"] = max(r["tp_at_minus14_dbtp"] for r in per_seed)
            results[preset] = med
            print("%-14s mono %+5.1f LU (worst band %+5.1f dB @%5.0f Hz, bass %+4.1f)  phone %+5.1f LU  "
                  "PLR %4.1f dB (TP@-14 %+5.1f)  from -1 dBTP: aac TP %+5.1f, mp3 clips %d  tail resid aac %+5.1f dB"
                  % (preset, med["mono_lu"], med["mono_worst_band_db"], med["mono_worst_band_hz"],
                     med["mono_bass_db"], med["phone_lu"], med["plr_db"], med["tp_at_minus14_dbtp"],
                     med.get("aac128_tp_dbtp", float("nan")), int(med.get("mp3_128_clipped", 0)),
                     med.get("aac128_tail_residual_db", float("nan"))), flush=True)

    phone_median = statistics.median(r["phone_lu"] for r in results.values())
    verdict = {}
    for p, r in results.items():
        fails = []
        if r["mono_lu"] < -3.5: fails.append("mono broadband %.1f LU" % r["mono_lu"])
        if r["mono_worst_band_db"] < -6.0: fails.append("mono comb %.1f dB at %d Hz" % (r["mono_worst_band_db"], r["mono_worst_band_hz"]))
        if r["mono_bass_db"] < -0.5: fails.append("bass not mono (%.1f dB)" % r["mono_bass_db"])
        if r["phone_lu"] < phone_median - 3.0: fails.append("phone %.1f LU vs bank median %.1f" % (r["phone_lu"], phone_median))
        if r.get("aac128_tp_dbtp", -99) > 0.0: fails.append("AAC decoded TP %.1f dBTP" % r["aac128_tp_dbtp"])
        if r.get("mp3_128_clipped", 0) > 0: fails.append("MP3 decode clips %d samples" % r["mp3_128_clipped"])
        verdict[p] = fails
    print("\nbank median phone loss %.1f LU" % phone_median)
    failing = {p: f for p, f in verdict.items() if f}
    print("PASS - every preset" if not failing else "FAIL:\n" + "\n".join("  %-14s %s" % (p, "; ".join(f)) for p, f in failing.items()))
    if a.json:
        Path(a.json).write_text(json.dumps({"chord": CHORD, "seeds": a.seeds, "results": results,
                                            "phone_median_lu": phone_median, "failures": failing}, indent=1))
    sys.exit(1 if failing else 0)


if __name__ == "__main__":
    main()
