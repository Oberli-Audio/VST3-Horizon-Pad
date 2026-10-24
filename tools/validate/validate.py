#!/usr/bin/env python3
"""Complete validation run for Horizon Pad, unattended (playbook G6, D.3-D.5).

One command builds the plugin, runs the DSP test suite, the Steinberg VST3
validator and pluginval, and (if a RealtimeSanitizer build exists) the suite
under RTSan - then writes every log and a JSON summary to one folder and exits
non-zero if anything failed. Validator versions are pinned in versions.json.

Profiles - pick by tier and occasion (playbook D.3.6):

  quick   every change that touches the host interface: DSP tests, Steinberg
          validator, pluginval strictness 5 (the host-compatibility floor)
  full    before a merge or release - Tier P's G6: DSP tests, Steinberg
          validator, pluginval strictness 10 at all six sample rates and seven
          block sizes, with a fixed, logged seed; RTSan suite if built
  soak    nightly or before a Tier F release: everything in full, plus pluginval
          strictness 10 repeated 5x in random order with a fresh seed (fuzzing
          state and parameters; the seed is logged so any failure reproduces)

    python3 tools/validate/validate.py                    # full
    python3 tools/validate/validate.py --profile quick
    python3 tools/validate/validate.py --plugin "/path/to/Horizon Pad.vst3" --no-build
    python3 tools/validate/validate.py --only pluginval --seed 0x1234   # reproduce
    python3 tools/validate/validate.py --fetch-pluginval  # download the pinned pluginval

pluginval is looked for in $PLUGINVAL, on PATH, in /Applications/pluginval.app,
then in build-validators/; --fetch-pluginval downloads the pinned release there.
The Steinberg validator is built from the pinned VST3 SDK commit into
build-validators/ on first use (without full Xcode, -DXCODE_VERSION is passed).

Exit codes: 0 all passed; 1 something failed (with --strict, also a skipped
step); 77 every requested step was skipped (missing plugin bundle or tool) -
CTest reports that as skipped. A mix of passes and skips is 0 without --strict.
"""
import argparse
import json
import os
import platform
import random
import re
import shutil
import subprocess
import sys
import time
import urllib.request
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
PINS = json.loads((Path(__file__).with_name("versions.json")).read_text())
TOOLS_DIR = ROOT / "build-validators"
SKIP = 77

RATES_ALL = "44100,48000,88200,96000,176400,192000"
BLOCKS_ALL = "32,64,128,256,512,1024,2048"
PROFILES = {
    "quick": {"steps": ["ctest", "validator", "pluginval"], "level": 5,
              "rates": None, "blocks": None, "repeat": 1, "randomise": False},
    "full": {"steps": ["ctest", "validator", "pluginval", "rtsan"], "level": 10,
             "rates": RATES_ALL, "blocks": BLOCKS_ALL, "repeat": 1, "randomise": False},
    "soak": {"steps": ["ctest", "validator", "pluginval", "pluginval_soak", "rtsan"], "level": 10,
             "rates": RATES_ALL, "blocks": BLOCKS_ALL, "repeat": 5, "randomise": True},
}
IS_MAC, IS_WIN = sys.platform == "darwin", sys.platform.startswith("win")


class Unavailable(Exception):
    """A step cannot run here (missing tool or bundle) - reported as skipped."""


def run(cmd, log, cwd=None, env=None, timeout=None):
    """Run cmd, tee everything to log, return (exit code, output)."""
    t0 = time.time()
    with open(log, "w", encoding="utf-8", errors="replace") as fh:
        fh.write("$ " + " ".join('"%s"' % c if " " in str(c) else str(c) for c in cmd) + "\n\n")
        fh.flush()
        try:
            p = subprocess.run([str(c) for c in cmd], cwd=cwd, env=env, timeout=timeout,
                               stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True,
                               errors="replace")
            out, code = p.stdout, p.returncode
        except subprocess.TimeoutExpired as e:
            out, code = (e.stdout or "") if isinstance(e.stdout, str) else "", 124
            out += "\n[validate.py] TIMEOUT after %ss\n" % timeout
        fh.write(out)
        fh.write("\n[validate.py] exit %d after %.0f s\n" % (code, time.time() - t0))
    return code, out


def bundle_from(path):
    """Accept the .vst3 bundle or any path inside it (e.g. CMake's TARGET_FILE_DIR)."""
    p = Path(path).resolve()
    for q in [p] + list(p.parents):
        if q.suffix == ".vst3":
            return q
    return p


# ------------------------------------------------------------------ tools

def find_pluginval(fetch):
    candidates = [os.environ.get("PLUGINVAL"), shutil.which("pluginval"),
                  "/Applications/pluginval.app/Contents/MacOS/pluginval"]
    local = TOOLS_DIR / ("pluginval-" + PINS["pluginval"])
    candidates += [local / "pluginval.app/Contents/MacOS/pluginval", local / "pluginval.exe", local / "pluginval"]
    for c in candidates:
        if c and Path(c).is_file():
            return Path(c)
    if not fetch:
        raise Unavailable("pluginval not found - install it, set PLUGINVAL, or pass --fetch-pluginval")
    asset = "pluginval_macOS.zip" if IS_MAC else "pluginval_Windows.zip" if IS_WIN else "pluginval_Linux.zip"
    url = "https://github.com/Tracktion/pluginval/releases/download/%s/%s" % (PINS["pluginval"], asset)
    local.mkdir(parents=True, exist_ok=True)
    zpath = local / asset
    print("[validate] downloading %s" % url, flush=True)
    urllib.request.urlretrieve(url, zpath)
    if IS_MAC:   # keep the app bundle's symlinks and permissions intact
        subprocess.run(["ditto", "-x", "-k", str(zpath), str(local)], check=True)
    else:
        with zipfile.ZipFile(zpath) as z:
            z.extractall(local)
    for c in candidates[3:]:
        if Path(c).is_file():
            os.chmod(c, 0o755)
            return Path(c)
    raise Unavailable("pluginval download unpacked, but no executable found in %s" % local)


def find_validator(build_if_missing=True):
    sdk = TOOLS_DIR / "vst3sdk"
    out = TOOLS_DIR / "vst3sdk-build"
    name = "validator.exe" if IS_WIN else "validator"
    hits = sorted(out.rglob(name)) if out.exists() else []
    hits = [h for h in hits if h.is_file() and (IS_WIN or os.access(h, os.X_OK))]
    stamp = out / "pinned-commit.txt"
    if hits and stamp.exists() and stamp.read_text().strip() == PINS["vst3sdk"]["commit"]:
        return hits[0]
    if not build_if_missing:
        raise Unavailable("Steinberg validator not built")
    commit = PINS["vst3sdk"]["commit"]
    print("[validate] building the Steinberg validator from VST3 SDK %s (%s)"
          % (PINS["vst3sdk"]["version"], commit[:7]), flush=True)
    if sdk.exists():
        shutil.rmtree(sdk)
    sdk.mkdir(parents=True)
    git = lambda *a: subprocess.run(["git", *a], cwd=sdk, check=True, capture_output=True)
    git("init", "-q")
    git("remote", "add", "origin", "https://github.com/steinbergmedia/vst3sdk.git")
    git("fetch", "-q", "--depth", "1", "origin", commit)
    git("checkout", "-q", "FETCH_HEAD")
    git("submodule", "update", "-q", "--init", "--depth", "1", "--", "base", "cmake", "pluginterfaces", "public.sdk")
    cfg = ["cmake", "-B", str(out), "-S", str(sdk), "-DCMAKE_BUILD_TYPE=Release",
           "-DSMTG_ENABLE_VST3_PLUGIN_EXAMPLES=OFF", "-DSMTG_ENABLE_VSTGUI_SUPPORT=OFF",
           "-DSMTG_ENABLE_VST3_HOSTING_EXAMPLES=OFF"]
    env = dict(os.environ)
    if IS_MAC:
        cfg += ["-G", "Ninja"] if shutil.which("ninja") else []
        if subprocess.run(["xcodebuild", "-version"], capture_output=True).returncode != 0:
            # Command Line Tools only: the SDK's configure asks xcodebuild for a version and
            # needs XCODE_VERSION as an environment variable AND a CMake variable (playbook D.4.2)
            env["XCODE_VERSION"] = "16.0"
            cfg += ["-DXCODE_VERSION=16.0", "-DCMAKE_C_COMPILER=clang", "-DCMAKE_CXX_COMPILER=clang++"]
    if out.exists():
        shutil.rmtree(out)
    log = TOOLS_DIR / "vst3sdk-build.log"
    with open(log, "w") as fh:
        subprocess.run(cfg, check=True, stdout=fh, stderr=subprocess.STDOUT, env=env)
        subprocess.run(["cmake", "--build", str(out), "--target", "validator", "--config", "Release"],
                       check=True, stdout=fh, stderr=subprocess.STDOUT, env=env)
    stamp.write_text(commit + "\n")
    return find_validator(build_if_missing=False)


# ------------------------------------------------------------------ steps

def step_build(a, logs):
    code, _ = run(["cmake", "--build", a.build_dir, "--target", "HorizonPad_VST3", "HorizonPadSoundTool",
                   "--config", "Release"], logs / "build.log")
    return code == 0, "cmake --build (VST3 + sound tool)"


def step_ctest(a, logs):
    code, out = run(["ctest", "--test-dir", a.build_dir, "-LE", "validation", "--output-on-failure"],
                    logs / "ctest.log", timeout=3600)
    m = re.search(r"\d+% tests passed(?:, (\d+) tests failed)? out of (\d+)", out)
    if not m:
        return code == 0, "see log"
    failed = int(m.group(1) or 0)
    return code == 0, "%d of %s passed" % (int(m.group(2)) - failed, m.group(2))


def step_validator(a, logs):
    if not a.plugin.exists():
        raise Unavailable("no plugin bundle at %s" % a.plugin)
    v = find_validator()
    code, out = run([v, a.plugin], logs / "steinberg-validator.log", timeout=1800)
    m = re.search(r"Result: (\d+) tests passed, (\d+) tests failed", out)
    detail = ("%s passed, %s failed (SDK %s)" % (m.group(1), m.group(2), PINS["vst3sdk"]["version"])) if m else "see log"
    # Only an Info line to the validator, but it is JUCE reading past an array (UBSan,
    # 2026-10-25; cmake/PatchJuce.cmake): a JUCE without the patch must not pass.
    out_of_range_cc = out.count("MIDI Mapping supplied for a wrong ControllerNumbers value")
    if out_of_range_cc:
        detail += "; MIDI mapping answered %d out-of-range controller queries (JUCE unpatched?)" % out_of_range_cc
    return code == 0 and (m is None or m.group(2) == "0") and not out_of_range_cc, detail


def pluginval_cmd(a, prof, seed, repeat, randomise, logs, name):
    pv = find_pluginval(a.fetch_pluginval)
    if not a.plugin.exists():
        raise Unavailable("no plugin bundle at %s" % a.plugin)
    cmd = [pv, "--strictness-level", str(prof["level"]), "--random-seed", seed,
           "--timeout-ms", str(a.timeout_ms), "--output-dir", logs, "--output-filename", name + "-pluginval-report.txt"]
    if prof["rates"]:
        cmd += ["--sample-rates", prof["rates"]]
    if prof["blocks"]:
        cmd += ["--block-sizes", prof["blocks"]]
    if repeat > 1:
        cmd += ["--repeat", str(repeat)]
    if randomise:
        cmd += ["--randomise"]
    if a.skip_gui_tests:
        cmd += ["--skip-gui-tests"]
    cmd += ["--validate", a.plugin]
    return cmd


def summarise_pluginval(out):
    fails = sorted(set(l.strip() for l in out.splitlines() if re.search(r"!!!|FAILED|\bfailed\b", l)))
    return fails[:8]


def step_pluginval(a, logs, prof):
    cmd = pluginval_cmd(a, prof, a.seed, 1, False, logs, "strictness%d" % prof["level"])
    code, out = run(cmd, logs / "pluginval.log", timeout=a.step_timeout)
    fails = summarise_pluginval(out) if code else []
    return code == 0, "strictness %d, seed %s%s%s" % (prof["level"], a.seed,
        ", rates %s" % prof["rates"] if prof["rates"] else ", default rates",
        ("; " + " | ".join(fails)) if fails else "")


def step_pluginval_soak(a, logs, prof):
    seed = "0x%08x" % random.SystemRandom().getrandbits(32)
    cmd = pluginval_cmd(a, prof, seed, prof["repeat"], True, logs, "soak")
    code, out = run(cmd, logs / "pluginval-soak.log", timeout=a.step_timeout * prof["repeat"])
    fails = summarise_pluginval(out) if code else []
    return code == 0, "strictness %d x%d randomised, seed %s (reproduce: --only pluginval --seed %s)%s" % (
        prof["level"], prof["repeat"], seed, seed, ("; " + " | ".join(fails)) if fails else "")


def step_rtsan(a, logs):
    tool = ROOT / "build-rtsan" / "HorizonPadSoundTool"
    if not tool.exists():
        raise Unavailable("no build-rtsan/HorizonPadSoundTool (recipe in CLAUDE.md)")
    # A stale RTSan build tests old code (2026-10-07: it still had the previous
    # preset bank). Rebuild it - or, with --no-build, refuse to call it a pass.
    sources = [f for d in ("Source", "tools/sound_tool") for f in (ROOT / d).rglob("*")
               if f.suffix in (".cpp", ".h", ".hpp")] + [ROOT / "CMakeLists.txt"]
    newest = max(f.stat().st_mtime for f in sources)
    if newest > tool.stat().st_mtime:                 # timestamps: ninja -n stops at the CMake re-run
        if a.no_build:
            return False, "build-rtsan is out of date with the sources - rebuild it (or drop --no-build)"
        code, _ = run(["cmake", "--build", ROOT / "build-rtsan", "--target", "HorizonPadSoundTool"],
                      logs / "rtsan-build.log", timeout=3600)
        if code != 0:
            return False, "rebuilding build-rtsan failed - see rtsan-build.log"
    env = dict(os.environ)
    sym = "/opt/homebrew/opt/llvm/bin/llvm-symbolizer"
    if Path(sym).exists():
        env.setdefault("RTSAN_OPTIONS", "external_symbolizer_path=" + sym)
    selftest_env = dict(env, HORIZON_RTSAN_SELFTEST="1")
    c1, _ = run([tool, "--notes=60", "--hold=1", "--tail=0"], logs / "rtsan-selftest.log", env=selftest_env, timeout=120)
    if c1 == 0:
        return False, "RTSan self-test did not fire - the check is not live"
    code, out = run([sys.executable, ROOT / "tools/tests/dsp_tests.py", "--tool", tool], logs / "rtsan-suite.log",
                    env=env, timeout=3600)
    passed = len(re.findall(r"^PASS ", out, re.M))
    failed = len(re.findall(r"^FAIL ", out, re.M))
    return code == 0, "%d passed, %d failed under RealtimeSanitizer (self-test fired)" % (passed, failed)


# ------------------------------------------------------------------ main

def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--profile", choices=PROFILES, default="full")
    ap.add_argument("--only", help="comma-separated steps: build,ctest,validator,pluginval,pluginval_soak,rtsan")
    ap.add_argument("--build-dir", default=str(ROOT / "build-release"))
    ap.add_argument("--plugin", help="the .vst3 bundle (or a path inside it); default: the build-dir artefact")
    ap.add_argument("--no-build", action="store_true")
    ap.add_argument("--seed", default="0x48505631", help="pluginval random seed (default fixed, so runs compare)")
    ap.add_argument("--fetch-pluginval", action="store_true")
    ap.add_argument("--skip-gui-tests", action="store_true", help="for headless machines")
    ap.add_argument("--timeout-ms", type=int, default=60000, help="pluginval: max silence per test")
    ap.add_argument("--step-timeout", type=int, default=3600, help="seconds per pluginval run")
    ap.add_argument("--out", help="log folder (default build-validation/<timestamp>)")
    ap.add_argument("--strict", action="store_true",
                    help="a skipped step is a failure (CI: a missing tool must not pass silently)")
    a = ap.parse_args()

    prof = PROFILES[a.profile]
    a.plugin = bundle_from(a.plugin) if a.plugin else \
        Path(a.build_dir) / "HorizonPad_artefacts" / "Release" / "VST3" / "Horizon Pad.vst3"
    steps = a.only.split(",") if a.only else (([] if a.no_build else ["build"]) + prof["steps"])
    logs = Path(a.out) if a.out else ROOT / "build-validation" / time.strftime("%Y%m%d-%H%M%S")
    logs.mkdir(parents=True, exist_ok=True)

    runners = {"build": lambda: step_build(a, logs), "ctest": lambda: step_ctest(a, logs),
               "validator": lambda: step_validator(a, logs), "pluginval": lambda: step_pluginval(a, logs, prof),
               "pluginval_soak": lambda: step_pluginval_soak(a, logs, prof), "rtsan": lambda: step_rtsan(a, logs)}
    results, any_fail, any_skip = [], False, False
    print("[validate] profile %s, plugin %s, logs %s" % (a.profile, a.plugin, logs), flush=True)
    for s in steps:
        t0 = time.time()
        try:
            ok, detail = runners[s]()
            status = "PASS" if ok else "FAIL"
        except Unavailable as e:
            status, detail = "SKIP", str(e)
        except subprocess.CalledProcessError as e:
            status, detail = "FAIL", "setup failed (%s): see %s" % (Path(str(e.cmd[0])).name, TOOLS_DIR / "vst3sdk-build.log")
        dt = time.time() - t0
        results.append({"step": s, "status": status, "seconds": round(dt), "detail": detail})
        any_fail |= status == "FAIL"
        any_skip |= status == "SKIP"
        print("[validate] %-15s %s  %4.0f s  %s" % (s, status, dt, detail), flush=True)

    summary = {"profile": a.profile, "plugin": str(a.plugin), "platform": platform.platform(),
               "pins": PINS, "seed": a.seed, "steps": results,
               "result": "FAIL" if any_fail or (a.strict and any_skip)
                         else ("SKIP" if any_skip and all(r["status"] == "SKIP" for r in results)
                               else "PASS")}
    (logs / "summary.json").write_text(json.dumps(summary, indent=1))
    print("[validate] %s - summary in %s" % (summary["result"], logs / "summary.json"))
    if summary["result"] == "FAIL":
        sys.exit(1)
    if summary["result"] == "SKIP":
        sys.exit(SKIP)


if __name__ == "__main__":
    main()
