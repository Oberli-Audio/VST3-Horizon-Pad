#!/usr/bin/env python3
"""
Blind A/B/X listening session: baseline build vs candidate build.

The playbook requires a level-matched, blind comparison with instant switching
before a sound change lands (12.1, 12.6) - and the ear is the one instrument the
offline harness does not have. This script turns that into a 20-minute session:

  1. renders every item below with both HorizonPadSoundTool builds, same seed,
  2. loudness-matches each pair (ITU-R BS.1770, both to -20 LUFS), so the louder
     one cannot win by being louder,
  3. writes ONE self-contained HTML page (lossless FLAC embedded, no server) with
     sample-synchronous instant switching between "1", "2" and a hidden "X",
     randomised per item so neither label means "new",
  4. records, per item, which one X was and which one you prefer, plus notes, and
     exports the verdicts as JSON with the key - so the result can go into the
     project's docs.

    python3 tools/listening/make_session.py \\
        --baseline /path/to/main/HorizonPadSoundTool \\
        --candidate build-release/HorizonPadSoundTool \\
        --out build-listening

Then open build-listening/index.html in a browser. Build the baseline from a git
worktree of the previous release (see docs/sound-design-v2.md).

Needs numpy and scipy, and macOS `afconvert` (for FLAC; falls back to 16-bit WAV).
"""
import argparse
import base64
import json
import os
import shutil
import subprocess
import tempfile

import numpy as np
from scipy import signal
from scipy.io import wavfile

SR = 48000
TARGET_LUFS = -20.0

# (id, title, what to listen for, tool arguments[, per-side extra arguments])
#
# The optional fifth element adds arguments to one side only, for a control
# the two builds spell differently or one of them lacks: before 2026-10-02
# every pad had its own WIDTH parameter (`--param=root-width=1`), since then
# one WIDTH macro (`--param=width=1`); a build without DETUNE plays the
# designed detune, which is DETUNE 50%.
ITEMS = [
    ("blend_mid", "Default patch, C3-E4 chord",
     "The whole instrument in its home register. Warmth, width, life.",
     ["--notes=48,55,60,64", "--hold=7", "--tail=3"]),
    ("root_chord", "Root solo, C3-E4, WIDTH 100%",
     "Stereo image of the foundation pad: width, depth, does it still feel centred? "
     "(Since 2026-10-02 full WIDTH is 60% of Root's spread.)",
     ["--solo=root", "--notes=48,55,60,64", "--hold=7", "--tail=3"],
     {"baseline": ["--param=root-width=1"], "candidate": ["--param=width=1"]}),
    ("clearing_chord", "Clearing solo, C3-E4, WIDTH 100%",
     "Ensemble vs slow flanger. Is it still Clearing? Richer, or just different?",
     ["--solo=clearing", "--notes=48,55,60,64", "--hold=7", "--tail=3"],
     {"baseline": ["--param=clearing-width=1"], "candidate": ["--param=width=1"]}),
    ("expanse_chord", "Expanse solo, C4-E4-G4, WIDTH 100%",
     "Brightness and the shimmer's width. Airy, or too bright?",
     ["--solo=expanse", "--notes=60,64,67", "--hold=7", "--tail=3"],
     {"baseline": ["--param=expanse-width=1"], "candidate": ["--param=width=1"]}),
    ("bloom_chord", "Bloom solo, C3-E4 chord",
     "The tremolo in a chord: a musical pulse, or too obvious?",
     ["--solo=bloom", "--notes=48,55,60,64", "--hold=7", "--tail=3"]),
    ("low_fifth", "Low register: C2 open fifth (C2 G2 C3), default patch",
     "Is the bass register music - clear, stable, not muddy?",
     ["--notes=36,43,48", "--hold=7", "--tail=3"]),
    ("low_triad", "Low register: C2 close triad (C2 E2 G2), default patch",
     "A voicing that is rough on any instrument. Less rough? Still usable?",
     ["--notes=36,40,43", "--hold=7", "--tail=3"]),
    ("root_low_notes", "Root solo: C1 then C2",
     "Single bass notes: pitch clear, no slow phasing, no rumble.",
     ["--solo=root", "--notes=24,36", "--note-onsets=0,4.5", "--note-holds=4,4", "--hold=4", "--tail=2"]),
    ("expanse_bassline", "Expanse solo: C2 - G2 - C3",
     "Register pinning: does the air follow the bass line naturally? Any audible octave jump?",
     ["--solo=expanse", "--notes=36,43,48", "--note-onsets=0,3,6", "--note-holds=2.8,2.8,3", "--hold=3", "--tail=2"]),
    ("clearing_top", "Clearing solo, C5-E5-G5",
     "Top of the range: open, or shrill?",
     ["--solo=clearing", "--notes=72,76,79", "--hold=7", "--tail=3"]),
    ("preset_bergecho", "Preset Bergecho (wide, big reverb)",
     "The room with the bass kept dry: still big? Clearer underneath?",
     ["--preset=Bergecho", "--notes=48,55,60,64", "--hold=7", "--tail=4"]),
    # Closing the whole listening list (2026-10-04): the reverb's low end after
    # the side high-pass moved behind the reverb return (2026-10-03), the top
    # octave Expanse aliased in (EX-003), and the high register of the blend.
    ("preset_steinerne_ruhe", "Preset Steinerne Ruhe (reverb low end)",
     "Since 2026-10-03 the reverb no longer puts stereo into the bass. Is the low end of the room "
     "still warm, and does the image still feel wide?",
     ["--preset=Steinerne Ruhe", "--notes=48,55,60,64", "--hold=7", "--tail=4"]),
    ("expanse_top", "Expanse solo, F6-A6-C7 (EX-003, the top of the register)",
     "The top of the register, where Expanse aliased in v1 (MIDI 96-108 then; the "
     "instrument stops at C7 = 96 since 2026-10-17). Any grit, whistle or "
     "inharmonic tone? Bright is fine; dirty is not.",
     ["--solo=expanse", "--notes=89,93,96", "--hold=6", "--tail=3"]),
    ("blend_high", "Default patch, C6 chord",
     "The whole instrument high up: still one instrument, or shrill and thin?",
     ["--notes=84,88,91", "--hold=6", "--tail=3"]),
    ("preset_lagerfeuer", "Preset Lagerfeuer (the default sound)",
     "The first thing a user hears.",
     ["--preset=Lagerfeuer", "--notes=48,55,60,64", "--hold=7", "--tail=3"]),
    # WIDTH and DETUNE macros (2026-10-02). Against a build from before that
    # date the presets compare the same width on all four pads with the
    # staggered profile; the DETUNE items compare an extreme with the
    # designed detune.
    # Klarheit became an in-between preset on 2026-10-07, so it no longer
    # compares like with like against v1; Daemmerlicht is an unchanged pad at
    # a similar WIDTH.
    ("preset_daemmerlicht", "Preset D\u00e4mmerlicht, WIDTH 55%",
     "The profile's biggest change: Root now near the centre, the air still open. Clearer, or narrower?",
     ["--preset=D\u00e4mmerlicht", "--notes=48,55,60,64", "--hold=7", "--tail=3"]),
    ("preset_sternenzelt", "Preset Sternenzelt, WIDTH 100%",
     "Full width, with Root and Bloom held back by their profiles. Still vast?",
     ["--preset=Sternenzelt", "--notes=48,55,60,64", "--hold=7", "--tail=4"]),
    ("detune_tight", "Default patch, DETUNE 0% against the designed detune",
     "Tighter and cleaner: still alive, or static and organ-like?",
     ["--notes=48,55,60,64", "--hold=7", "--tail=3"],
     {"candidate": ["--param=detune=0"]}),
    ("detune_wide", "Default patch, DETUNE 100% against the designed detune",
     "Wider and lusher: still in tune, or seasick? Listen to Clearing, the pad DETUNE moves least.",
     ["--notes=48,55,60,64", "--hold=7", "--tail=3"],
     {"candidate": ["--param=detune=1"]}),
    # The ends of the register (review 2026-10-17, R-1 to R-3) and Bloom's
    # movement (D-1). Run these with the SAME build on both sides (--baseline
    # and --candidate both build-release/HorizonPadSoundTool, --only <ids>):
    # each pairs the end key with the octave inside it, so the question is
    # "is the end still music, next to its neighbour?" - X still checks you
    # can tell them apart, and the preference column records the verdict.
    ("register_bottom_root", "Root solo: C0 (candidate) against C1",
     "The lowest key of the register. A note with a pitch, or a wobble? (Roughness 1.25 at C0.)",
     ["--solo=root", "--hold=6", "--tail=3"],
     {"baseline": ["--notes=24"], "candidate": ["--notes=12"]}),
    ("register_bottom_bloom", "Bloom solo: C0 (candidate) against C1",
     "Bloom is the roughest layer at the bottom (3.6 at C0, 1.8 at C1). Music, or grind?",
     ["--solo=bloom", "--hold=6", "--tail=3"],
     {"baseline": ["--notes=24"], "candidate": ["--notes=12"]}),
    ("register_bottom_blend", "Default patch: open fifth C0-G0-C1 (candidate) against C1-G1-C2",
     "The whole instrument at the bottom of the register, as a left hand would play it.",
     ["--hold=6", "--tail=3"],
     {"baseline": ["--notes=24,31,36"], "candidate": ["--notes=12,19,24"]}),
    ("register_top_root", "Root solo: C7 (candidate) against C6",
     "Root loses its pitch above C5 (salience -0.04 at C7): does it sound an octave low, or just thin?",
     ["--solo=root", "--hold=6", "--tail=3"],
     {"baseline": ["--notes=84"], "candidate": ["--notes=96"]}),
    ("register_top_clearing", "Clearing solo: C7 (candidate) against C6",
     "Clearing is roughest at the top (3.7 at C7): shimmer, or a buzz?",
     ["--solo=clearing", "--hold=6", "--tail=3"],
     {"baseline": ["--notes=84"], "candidate": ["--notes=96"]}),
    ("bloom_movement", "Bloom solo, C3-E4: DETUNE 0% (candidate) against 50%",
     "Bloom's measured movement halved since DETUNE's default went to 0% (43 -> 25%). "
     "Did the pad lose its life, or only its beating?",
     ["--solo=bloom", "--notes=48,55,60,64", "--hold=7", "--tail=3"],
     {"baseline": ["--param=detune=0.5"], "candidate": ["--param=detune=0"]}),
]

_K1 = ([1.53512485958697, -2.69169618940638, 1.19839281085285], [1.0, -1.69065929318241, 0.73248077421585])
_K2 = ([1.0, -2.0, 1.0], [1.0, -1.99004745483398, 0.99007225036621])


def lufs(x):
    y = signal.lfilter(*_K2, signal.lfilter(*_K1, x, axis=0), axis=0)
    return -0.691 + 10 * np.log10(np.mean(y ** 2, axis=0).sum() + 1e-20)


def render(tool, args, seed):
    fd, path = tempfile.mkstemp(suffix=".wav")
    os.close(fd)
    try:
        subprocess.run([tool, f"--seed={seed}", f"--sample-rate={SR}", f"--out={path}"] + args,
                       check=True, capture_output=True)
        _, x = wavfile.read(path)
    finally:
        os.remove(path)
    return x.astype(np.float64)


def encode(x, tmp, name):
    """Level-matched float stereo -> base64 FLAC (or 16-bit WAV)."""
    wav = os.path.join(tmp, name + ".wav")
    pcm = np.clip(x, -1.0, 1.0 - 1.0 / 32768)
    wavfile.write(wav, SR, (pcm * 32767).astype(np.int16))
    if shutil.which("afconvert"):
        flac = os.path.join(tmp, name + ".flac")
        subprocess.run(["afconvert", "-f", "flac", "-d", "flac", wav, flac], check=True, capture_output=True)
        with open(flac, "rb") as fh:
            return "audio/flac", base64.b64encode(fh.read()).decode()
    with open(wav, "rb") as fh:
        return "audio/wav", base64.b64encode(fh.read()).decode()


PAGE = r"""<!doctype html>
<html lang="en"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Horizon Pad A/B</title>
<style>
:root{--bg:#f6f4ef;--fg:#1d1c1a;--muted:#6b665d;--card:#fff;--line:#e2ddd3;--accent:#b4531f;--on:#1d1c1a;--onfg:#fff}
@media (prefers-color-scheme: dark){:root{--bg:#161513;--fg:#ece8e1;--muted:#9c968c;--card:#201f1c;--line:#34312c;--accent:#e08a4f;--on:#ece8e1;--onfg:#161513}}
*{box-sizing:border-box}body{margin:0;background:var(--bg);color:var(--fg);font:15px/1.5 -apple-system,BlinkMacSystemFont,"Segoe UI",sans-serif}
main{max-width:780px;margin:0 auto;padding:24px 16px 80px}h1{font-size:22px;margin:0 0 4px}p.lead{color:var(--muted);margin:0 0 20px}
.item{background:var(--card);border:1px solid var(--line);border-radius:10px;padding:16px;margin:0 0 14px}
.item h2{font-size:16px;margin:0 0 2px}.item p{margin:0 0 12px;color:var(--muted)}
.row{display:flex;flex-wrap:wrap;gap:8px;align-items:center;margin:8px 0}
button{font:inherit;border:1px solid var(--line);background:transparent;color:var(--fg);border-radius:8px;padding:6px 14px;cursor:pointer;min-width:48px}
button.on{background:var(--on);color:var(--onfg);border-color:var(--on)}
label{display:inline-flex;gap:4px;align-items:center;margin-right:12px;cursor:pointer}
textarea{width:100%;font:inherit;background:transparent;color:var(--fg);border:1px solid var(--line);border-radius:8px;padding:6px;min-height:40px}
.k{color:var(--muted);font-size:13px}.done{color:var(--accent);font-weight:600}
#bar{position:sticky;top:0;background:var(--bg);padding:10px 0;border-bottom:1px solid var(--line);margin-bottom:16px;z-index:1}
pre{white-space:pre-wrap;background:var(--card);border:1px solid var(--line);border-radius:8px;padding:10px;font-size:13px}
</style></head><body><main>
<h1>Horizon Pad &middot; blind A/B</h1>
<p class="lead">Each pair is loudness-matched. "1" and "2" are assigned at random per item, so neither means "new". Keys while an item is active: <b>1</b>, <b>2</b>, <b>X</b> switch instantly (in sync), <b>space</b> stops. Identify X, then say which you prefer. Listen at a normal level on monitors or good headphones; check a few in mono too.</p>
<div id="bar"><span id="progress" class="k"></span> <button id="reveal">Reveal and export</button></div>
<div id="items"></div>
<pre id="out" hidden></pre>
</main>
<script>
const DATA = __DATA__;
let ctx = null, active = null;
const state = {};
function rnd(){ return Math.random() < 0.5; }
for (const it of DATA.items) state[it.id] = { oneIsCandidate: rnd(), xIsOne: rnd(), x: null, pref: null, notes: "" };

async function decode(b64, mime){
  const bin = atob(b64); const buf = new Uint8Array(bin.length);
  for (let i = 0; i < bin.length; i++) buf[i] = bin.charCodeAt(i);
  return await ctx.decodeAudioData(buf.buffer);
}
async function ensureCtx(){ if (!ctx) ctx = new (window.AudioContext || window.webkitAudioContext)(); if (ctx.state === "suspended") await ctx.resume(); }

async function play(it, which){
  await ensureCtx();
  const st = state[it.id];
  if (!it.buffers){ it.buffers = { base: await decode(it.base, it.mime), cand: await decode(it.cand, it.mime) }; }
  if (!active || active.id !== it.id){
    stop();
    const g1 = ctx.createGain(), g2 = ctx.createGain();
    g1.connect(ctx.destination); g2.connect(ctx.destination);
    const one = st.oneIsCandidate ? it.buffers.cand : it.buffers.base;
    const two = st.oneIsCandidate ? it.buffers.base : it.buffers.cand;
    const s1 = ctx.createBufferSource(), s2 = ctx.createBufferSource();
    s1.buffer = one; s2.buffer = two; s1.loop = s2.loop = true;
    s1.connect(g1); s2.connect(g2);
    g1.gain.value = 0; g2.gain.value = 0;
    const t = ctx.currentTime + 0.05; s1.start(t); s2.start(t);
    active = { id: it.id, g1, g2, s1, s2 };
  }
  let useOne = which === "1" || (which === "X" && st.xIsOne);
  const now = ctx.currentTime;
  active.g1.gain.setTargetAtTime(useOne ? 1 : 0, now, 0.004);
  active.g2.gain.setTargetAtTime(useOne ? 0 : 1, now, 0.004);
  for (const b of document.querySelectorAll(`#it-${it.id} button[data-w]`)) b.classList.toggle("on", b.dataset.w === which);
}
function stop(){
  if (!active) return;
  try { active.s1.stop(); active.s2.stop(); } catch(e) {}
  for (const b of document.querySelectorAll("button[data-w]")) b.classList.remove("on");
  active = null;
}
function progress(){
  const n = DATA.items.filter(it => state[it.id].x && state[it.id].pref).length;
  document.getElementById("progress").textContent = `${n} of ${DATA.items.length} answered`;
}
let focused = null;
const wrap = document.getElementById("items");
for (const it of DATA.items){
  const d = document.createElement("section"); d.className = "item"; d.id = "it-" + it.id;
  d.innerHTML = `<h2>${it.title}</h2><p>${it.hint}</p>
    <div class="row"><button data-w="1">1</button><button data-w="2">2</button><button data-w="X">X</button><button data-stop>Stop</button></div>
    <div class="row"><span class="k">X is</span><label><input type="radio" name="x-${it.id}" value="1">1</label><label><input type="radio" name="x-${it.id}" value="2">2</label>
    <span class="k">Prefer</span><label><input type="radio" name="p-${it.id}" value="1">1</label><label><input type="radio" name="p-${it.id}" value="2">2</label><label><input type="radio" name="p-${it.id}" value="none">no preference</label></div>
    <textarea placeholder="What you hear (optional)"></textarea>`;
  d.addEventListener("pointerdown", () => focused = it);
  for (const b of d.querySelectorAll("button[data-w]")) b.onclick = () => { focused = it; play(it, b.dataset.w); };
  d.querySelector("button[data-stop]").onclick = stop;
  for (const r of d.querySelectorAll(`input[name="x-${it.id}"]`)) r.onchange = () => { state[it.id].x = r.value; progress(); };
  for (const r of d.querySelectorAll(`input[name="p-${it.id}"]`)) r.onchange = () => { state[it.id].pref = r.value; progress(); };
  d.querySelector("textarea").oninput = e => state[it.id].notes = e.target.value;
  wrap.appendChild(d);
}
document.addEventListener("keydown", e => {
  if (e.target.tagName === "TEXTAREA" || !focused) return;
  const k = e.key.toUpperCase();
  if (k === "1" || k === "2" || k === "X"){ e.preventDefault(); play(focused, k); }
  if (e.key === " "){ e.preventDefault(); stop(); }
});
document.getElementById("reveal").onclick = () => {
  const open = DATA.items.filter(it => !(state[it.id].x && state[it.id].pref)).length;
  if (open && !confirm(`${open} item(s) unanswered. Reveal anyway?`)) return;
  stop();
  const rows = DATA.items.map(it => {
    const st = state[it.id];
    const label = n => (n === "1") === st.oneIsCandidate ? "candidate" : "baseline";
    const xTruth = st.xIsOne ? "1" : "2";
    return { id: it.id, title: it.title, x_correct: st.x ? st.x === xTruth : null,
             preferred: st.pref === "none" ? "none" : (st.pref ? label(st.pref) : null),
             one_was: label("1"), notes: st.notes };
  });
  const result = { session: DATA.meta, answered_at: new Date().toISOString(), items: rows };
  const text = JSON.stringify(result, null, 1);
  const out = document.getElementById("out"); out.hidden = false;
  const summary = rows.map(r => `${r.x_correct === null ? "  -" : r.x_correct ? "  X ok " : "  X miss"}  prefers ${r.preferred ?? "-"}  ${r.title}`).join("\n");
  out.textContent = summary + "\n\n" + text;
  const a = document.createElement("a");
  a.href = URL.createObjectURL(new Blob([text], { type: "application/json" }));
  a.download = "listening-results.json"; a.click();
  for (const it of DATA.items) document.querySelector(`#it-${it.id} h2`).insertAdjacentHTML("beforeend",
    ` <span class="done">1 = ${state[it.id].oneIsCandidate ? "candidate" : "baseline"}</span>`);
};
progress();
</script></body></html>
"""


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--baseline", required=True, help="HorizonPadSoundTool of the reference build")
    ap.add_argument("--candidate", default="build-release/HorizonPadSoundTool")
    ap.add_argument("--out", default="build-listening")
    ap.add_argument("--seed", type=int, default=7)
    ap.add_argument("--only", help="comma-separated item ids")
    args = ap.parse_args()

    chosen = [it for it in ITEMS if not args.only or it[0] in args.only.split(",")]
    os.makedirs(args.out, exist_ok=True)
    items = []
    with tempfile.TemporaryDirectory() as tmp:
        for item in chosen:
            item_id, title, hint, tool_args = item[:4]
            side = item[4] if len(item) > 4 else {}
            base = render(args.baseline, tool_args + side.get("baseline", []), args.seed)
            cand = render(args.candidate, tool_args + side.get("candidate", []), args.seed)
            gb = 10 ** ((TARGET_LUFS - lufs(base)) / 20)
            gc = 10 ** ((TARGET_LUFS - lufs(cand)) / 20)
            base, cand = base * gb, cand * gc
            peak = max(np.abs(base).max(), np.abs(cand).max())
            if peak > 0.89:                       # keep 1 dB of headroom, both by the same amount
                base, cand = base * 0.89 / peak, cand * 0.89 / peak
            mime, b64b = encode(base, tmp, item_id + "_b")
            _, b64c = encode(cand, tmp, item_id + "_c")
            items.append({"id": item_id, "title": title, "hint": hint, "mime": mime, "base": b64b, "cand": b64c,
                          "level_trim_db": {"baseline": round(20 * np.log10(gb), 2),
                                            "candidate": round(20 * np.log10(gc), 2)}})
            print(f"[listening] {item_id}: matched (baseline {20*np.log10(gb):+.1f} dB, "
                  f"candidate {20*np.log10(gc):+.1f} dB)", flush=True)

    meta = {"baseline": os.path.abspath(args.baseline), "candidate": os.path.abspath(args.candidate),
            "seed": args.seed, "target_lufs": TARGET_LUFS,
            "level_trims_db": {it["id"]: it["level_trim_db"] for it in items}}
    page = PAGE.replace("__DATA__", json.dumps({"meta": meta, "items": items}))
    path = os.path.join(args.out, "index.html")
    with open(path, "w") as fh:
        fh.write(page)
    print(f"[listening] wrote {path} ({os.path.getsize(path) / 1e6:.1f} MB) - open it in a browser")


if __name__ == "__main__":
    main()
