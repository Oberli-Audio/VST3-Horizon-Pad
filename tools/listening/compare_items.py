#!/usr/bin/env python3
"""What differs, measurably, between two builds on each listening item.

Companion to make_session.py: after a blind A/B pass, run this with the same two
builds to see what actually changed behind each verdict - band balance, width
(side/mid) per band, spectral centroid and movement, after the session's own
-20 LUFS loudness match. Used on 2026-10-06/07 to turn the owner's preferences
into targeted changes (docs/status.md, Listening debt).

    python3 tools/listening/compare_items.py --baseline ../hp-v1/build/HorizonPadSoundTool \
            --candidate build-release/HorizonPadSoundTool [--prefs verdicts.json]

--prefs takes the JSON the session page exports; its preferences are printed per row.
"""
import sys, json, subprocess, tempfile, warnings
from pathlib import Path
import numpy as np
warnings.filterwarnings("ignore")
import os; sys.path.insert(0, os.environ.get("DSPKIT", str(Path(__file__).resolve().parents[2] / "scripts" / "dsp-kit"))); import dspkit as K
sys.path.insert(0, "tools/listening"); import make_session as M
import argparse
ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
ap.add_argument("--baseline", required=True)
ap.add_argument("--candidate", default="build-release/HorizonPadSoundTool")
ap.add_argument("--prefs", help="verdict JSON exported by the session page")
A = ap.parse_args()
V1, NOW = A.baseline, A.candidate; SR = 48000
items = {it[0]: it for it in M.ITEMS}
items.setdefault("preset_klarheit", ("preset_klarheit", "Klarheit W50", "", ["--preset=Klarheit", "--notes=48,55,60,64", "--hold=7", "--tail=3"]))
BANDS = [(20, 150), (150, 500), (500, 2000), (2000, 5000), (5000, 12000)]
def render(tool, args):
    with tempfile.TemporaryDirectory() as d:
        out = Path(d)/"r.wav"
        r = subprocess.run([tool, "--seed=7", f"--out={out}"] + args, capture_output=True, text=True)
        if r.returncode: return None
        x, _ = K.read_wav(out)
    return x
def bandpow(sig, lo, hi):
    S = np.abs(np.fft.rfft(sig))**2; f = np.fft.rfftfreq(len(sig), 1/SR)
    return np.sum(S[(f >= lo) & (f < hi)]) + 1e-20
def metrics(x):
    x = x * K.undb(-20 - K.integrated_lufs(x, SR))
    seg = x[int(1.5*SR):int(5.5*SR)]
    m, s = 0.5*(seg[:, 0]+seg[:, 1]), 0.5*(seg[:, 0]-seg[:, 1])
    tot = sum(bandpow(m, lo, hi) + bandpow(s, lo, hi) for lo, hi in BANDS)
    bands = [10*np.log10((bandpow(m, lo, hi)+bandpow(s, lo, hi))/tot) for lo, hi in BANDS]
    width = [10*np.log10(bandpow(s, lo, hi)/bandpow(m, lo, hi)) for lo, hi in BANDS]
    w = int(0.05*SR); env = 20*np.log10(np.sqrt((m[:len(m)//w*w].reshape(-1, w)**2).mean(1))+1e-12)
    return bands, width, K.spectral_centroid_hz(seg, SR), np.std(env)
pref = {k: "?" for k in items}
if A.prefs:
    for it in json.load(open(A.prefs)).get("items", []):
        pref[it["id"]] = {"baseline": "base", "candidate": "cand"}.get(it.get("preferred"), "?")
print("diff = candidate minus baseline | band balance dB: <150 150-500 .5-2k 2-5k 5-12k | width S/M dB same bands | centroid | movement")
for iid, p in pref.items():
    it = items.get(iid)
    if it is None: continue
    args = list(it[3]); per = it[4] if len(it) > 4 else {}
    xa = render(V1, args + per.get("baseline", [])); xb = render(NOW, args + per.get("candidate", []))
    if xa is None or xb is None: print(f"{iid:22s} render failed"); continue
    a, b = metrics(xa), metrics(xb)
    db = " ".join("%+5.1f" % (y - x) for x, y in zip(a[0], b[0]))
    dw = " ".join("%+5.1f" % (y - x) for x, y in zip(a[1], b[1]))
    print(f"{iid:22s} prefers {p:4s} | {db} | {dw} | {1200*np.log2(b[2]/a[2]):+5.0f} c | {b[3]-a[3]:+4.1f} dB", flush=True)
