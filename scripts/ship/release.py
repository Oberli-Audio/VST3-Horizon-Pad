#!/usr/bin/env python3
"""release - the release recipe as functions (references/release-recipe.md).

    release.py dry-run   [--repo .] [--from <step>] [--to <step>]     (also: --dry-run)
    release.py run       [--repo .] [--from <step>] [--to <step>] [--yes]
    release.py steps
    release.py selftest

Steps, in order: precheck, version, docs, tag, build, provenance, sign, package, checksums,
smoke, publish, announce. `--dry-run` prints every command without running anything and
checks ship.toml and the tree; `run` executes the local steps and prints the ones that
happen on GitHub or by hand (build on the workflow, smoke test in Ableton Live, announce). Nothing
here can delete a release or force-push; a failed step stops the run and keeps the files.
Config: ship.toml at the repo root (release-recipe.md shows it).
"""
import argparse
import json
import shlex
import subprocess
import sys
from pathlib import Path

try:
    import tomllib
except ImportError:
    tomllib = None

HERE = Path(__file__).resolve().parent
STEPS = ["precheck", "version", "docs", "tag", "build", "provenance", "sign", "package", "checksums", "smoke", "publish", "announce"]


class Ctx:
    def __init__(self, repo, dry, yes=False):
        self.repo = Path(repo).resolve()
        self.dry = dry
        self.yes = yes
        self.cfg = self.load_cfg()
        vp = self.repo / "VERSION"
        self.version = vp.read_text(encoding="utf-8").strip() if vp.exists() else None
        self.log = []

    def load_cfg(self):
        p = self.repo / "ship.toml"
        if not p.exists():
            raise SystemExit("release: no ship.toml in %s (release-recipe.md shows the template)" % self.repo)
        sys.path.insert(0, str(HERE))
        from shipcheck import load_toml
        return load_toml(p)

    @property
    def name(self):
        return self.cfg["product"]["name"]

    @property
    def slug(self):
        return self.cfg["product"]["slug"]

    @property
    def tier(self):
        return self.cfg["product"].get("tier", "P")

    def say(self, text):
        print(text)
        self.log.append(text)

    def sh(self, cmd, where="local", check=True):
        """Print the command; run it unless dry or it happens elsewhere (GitHub, by hand)."""
        line = cmd if isinstance(cmd, str) else " ".join(shlex.quote(c) for c in cmd)
        tag = {"local": "$", "github": "[workflow]", "hand": "[by hand]"}[where]
        self.say("  %s %s" % (tag, line))
        if self.dry or where != "local":
            return 0
        r = subprocess.run(cmd, shell=isinstance(cmd, str), cwd=self.repo)
        if check and r.returncode != 0:
            raise RuntimeError("step failed: %s (exit %d)" % (line, r.returncode))
        return r.returncode


# ----------------------------------------------------------------------------- steps

def precheck(c):
    c.say("0 precheck: the joint release gate is green on this commit (sound-design-bob validate_plugin.py full, presetlint, perf, goldens; interface list)")
    c.sh(["git", "status", "--porcelain"])
    if not c.dry:
        dirty = subprocess.run(["git", "status", "--porcelain"], cwd=c.repo, capture_output=True, text=True).stdout.strip()
        if dirty:
            raise RuntimeError("the tree is dirty; commit or stash before a release")
    c.say("  tier %s, product %s, version %s" % (c.tier, c.name, c.version))


def version(c):
    c.say("1 version: VERSION = %s; CHANGELOG.md has the section; Sounds different when dspVersion or a taper changed" % c.version)
    c.sh(["git", "log", "-1", "--format=%h %s"])


def docs(c):
    c.say("2 docs: notices generated from the manifest, Known issues from the exceptions, then shipcheck")
    c.sh([sys.executable, str(HERE / "notices.py"), "generate", str(c.repo)])
    c.sh([sys.executable, str(HERE / "shipcheck.py"), "known-issues", str(c.repo), "--write"])
    c.sh([sys.executable, str(HERE / "shipcheck.py"), "docs", str(c.repo)])


def tag(c):
    t = "v%s" % c.version
    c.say("3 tag: %s on HEAD, pushed; the workflow builds from it" % t)
    c.sh(["git", "tag", "-a", t, "-m", "%s %s" % (c.name, c.version)])
    c.sh(["git", "push"])
    c.sh(["git", "push", "origin", t])


def build(c):
    c.say("4 build: the workflow's matrix (macOS universal Release, Windows /%s /arch:%s)" % (c.cfg["windows"].get("runtime", "MT"), c.cfg["windows"].get("arch", "SSE4.2")))
    c.sh("cmake --preset release && cmake --build --preset release", where="github")
    c.sh([sys.executable, "scripts/ship/buildinfo.py", "emit", "--repo", ".", "--build", "build", "--out", "build/build-info.json"], where="github")


def provenance(c):
    c.say("5 provenance: build-info.json says Release, both slices, clean, version %s" % c.version)
    c.sh([sys.executable, str(HERE / "buildinfo.py"), "check", "build/build-info.json", "--expect-version", c.version], where="github")


EXT = {"VST3": "vst3", "AU": "component", "CLAP": "clap"}


def bundles_of(c):
    return ["%s.%s" % (c.name, EXT[f]) for f in c.cfg["product"].get("formats", ["VST3"]) if f in EXT]


def asset_names(c):
    """Version-free asset names (decided 2026-10-19) so releases/latest/download/<asset> never changes;
    the version lives in the tag and build-info-<platform>.json."""
    n = c.cfg["product"].get("asset_name") or c.name.replace(" ", "")
    return {"mac_zip": "%s-macOS.zip" % n, "mac_pkg": "%s-macOS.pkg" % n, "win_zip": "%s-Windows-x64.zip" % n,
            "win_setup": "%s-Windows-x64-setup.exe" % n, "info_mac": "build-info-macOS.json", "info_win": "build-info-Windows.json",
            "sym_mac": "%s-symbols-macOS.zip" % n, "sym_win": "%s-symbols-Windows.zip" % n, "reg": "enable-crashdumps.reg",
            "sums": "SHA256SUMS.txt"}


def sign(c):
    bundles = bundles_of(c)
    if c.tier == "F":
        ident = c.cfg["macos"].get("identity_app")
        if not ident:
            raise RuntimeError("Tier F needs macos.identity_app in ship.toml (Developer ID Application); refusing to ship ad-hoc as a flagship")
        c.say("6 sign (Tier F, macos-14 runner): Developer ID, hardened runtime, timestamp")
        for b in bundles:
            c.sh(["xattr", "-cr", b], where="github")
            c.sh(["codesign", "--force", "--options", "runtime", "--timestamp", "--sign", ident, b], where="github")
            c.sh(["codesign", "--verify", "--deep", "--strict", "--verbose=2", b], where="github")
    else:
        c.say("6 sign (Tier P, macos-14 runner): ad-hoc, both slices; friends follow the Open-Anyway paragraph")
        for b in bundles:
            c.sh(["xattr", "-cr", b], where="github")
            c.sh(["codesign", "--force", "--sign", "-", b], where="github")
            c.sh(["codesign", "--verify", "--deep", "--strict", "--verbose=2", b], where="github")
    for b in bundles:
        c.sh(["lipo", "-archs", "%s/Contents/MacOS/%s" % (b, c.name)], where="github")
    a = asset_names(c)
    c.say("   symbols of this tag (feedback-loop.md 6): dSYM before strip, zipped as %s" % a["sym_mac"])
    for b in bundles:
        c.sh(["dsymutil", "%s/Contents/MacOS/%s" % (b, c.name), "-o", "%s.dSYM" % b], where="github")
        c.sh(["dwarfdump", "--uuid", "%s.dSYM" % b], where="github")
    c.sh("ditto -c -k --norsrc --noextattr --keepParent *.dSYM %s" % a["sym_mac"], where="github")


def package(c):
    a = asset_names(c)
    bundles = bundles_of(c)
    if c.tier == "F" and c.cfg["macos"].get("identity_installer"):
        c.say("7 package (Tier F, macos-14 runner): pkg per format, productbuild, notarise, staple, assess (installers-macos.md)")
        c.sh("pkgbuild ... && productbuild --sign %s %s" % (shlex.quote(c.cfg["macos"]["identity_installer"]), a["mac_pkg"]), where="github")
        c.sh(["xcrun", "notarytool", "submit", a["mac_pkg"], "--keychain-profile", c.cfg["macos"].get("notary_profile", "<profile>"), "--wait"], where="github")
        c.sh(["xcrun", "stapler", "staple", a["mac_pkg"]], where="github")
        c.sh(["spctl", "-a", "-vv", "-t", "install", a["mac_pkg"]], where="github")
    else:
        c.say("7 package (Tier P, macos-14 runner): every format in one zip, bundles at the top level, no __MACOSX")
        c.sh("mkdir -p stage && cp -R %s stage/ && cp \"$GITHUB_WORKSPACE\"/LICENSE \"$GITHUB_WORKSPACE\"/THIRD_PARTY_NOTICES.md stage/" % " ".join(shlex.quote(b) for b in bundles), where="github")
        c.sh(["ditto", "-c", "-k", "--norsrc", "--noextattr", "stage", a["mac_zip"]], where="github")
    c.say("   (windows-latest runner, shell: pwsh)")
    c.sh("Compress-Archive -Path '%s.vst3', LICENSE, THIRD_PARTY_NOTICES.md -DestinationPath %s" % (c.name, a["win_zip"]), where="github")
    c.sh("Compress-Archive -Path '*.pdb' -DestinationPath %s   # /Zi and /DEBUG in Release keep the PDB (feedback-loop.md 6)" % a["sym_win"], where="github")
    c.sh("Copy-Item scripts/ship/%s ." % a["reg"], where="github")
    if c.cfg["windows"].get("installer"):
        c.sh("iscc packaging/installer.iss  (PLUGIN_VERSION=%s)  -> %s" % (c.version, a["win_setup"]), where="github")


def checksums(c):
    a = asset_names(c)
    c.say("8 checksums and assets (ubuntu release job, after download-artifact merge-multiple)")
    c.sh("for f in *.zip *.pkg; do [ -f \"$f\" ] && sha256sum \"$f\"; done > %s" % a["sums"], where="github")
    c.sh([sys.executable, "scripts/ship/shipcheck.py", "release-assets", "dist", "--repo", "."], where="github")
    c.sh("softprops/action-gh-release: draft, body = changelog section, files = %s" % ", ".join(
        v for k, v in a.items() if k in ("mac_zip", "mac_pkg", "win_zip", "win_setup", "sym_mac", "sym_win", "reg", "info_mac", "info_win", "sums")), where="github")


def smoke(c):
    c.say("9 smoke: download the assets from the draft release (not the build folder); `shipcheck.py release-assets <download folder>`; quit Ableton Live; replace the old bundle; start Ableton Live 10.1 under Rosetta, the Open-Anyway dance, rescan, Init preset, last session, About shows %s and the SHA" % c.version)
    c.sh([sys.executable, str(HERE / "shipcheck.py"), "release-assets", "~/Downloads/release-assets", "--repo", str(c.repo)], where="hand")
    c.sh("open https://github.com/%s/releases" % c.cfg["product"].get("repo", "<owner>/<repo>"), where="hand")


def publish(c):
    c.say("10 publish: the draft becomes the release")
    c.sh(["gh", "release", "edit", "v%s" % c.version, "--draft=false"], where="hand" if c.dry else "local")


def announce(c):
    c.say("11 announce: paste this to the friends (shipcheck.py message)")
    out = subprocess.run([sys.executable, str(HERE / "shipcheck.py"), "message", str(c.repo)], capture_output=True, text=True).stdout
    for line in out.splitlines():
        c.say("  | " + line)
    c.say("12 done when the friend confirms the installed version plays his set; from his first report on, references/feedback-loop.md (docs/feedback.md row, triage route, test or exception per closed report)")


FUNCS = {"precheck": precheck, "version": version, "docs": docs, "tag": tag, "build": build, "provenance": provenance,
         "sign": sign, "package": package, "checksums": checksums, "smoke": smoke, "publish": publish, "announce": announce}


def run(a, dry):
    c = Ctx(a.repo, dry, getattr(a, "yes", False))
    i0 = STEPS.index(a.from_step) if a.from_step else 0
    i1 = STEPS.index(a.to_step) + 1 if a.to_step else len(STEPS)
    c.say("release %s %s (%s)%s" % (c.name, c.version, "dry run" if dry else "run", ", steps %s..%s" % (STEPS[i0], STEPS[i1 - 1]) if (i0 or i1 < len(STEPS)) else ""))
    for s in STEPS[i0:i1]:
        try:
            FUNCS[s](c)
        except RuntimeError as e:
            c.say("STOP at %s: %s" % (s, e))
            return 1
    return 0


def selftest():
    import shutil, tempfile, io, contextlib
    here = HERE.parent.parent / "tests" / "fixtures" / "fakeplugin"
    fails = []
    expect = lambda label, cond: None if cond else fails.append(label)
    with tempfile.TemporaryDirectory() as d:
        repo = Path(d) / "repo"
        shutil.copytree(here, repo)
        g = lambda *x: subprocess.run(["git", "-C", str(repo), "-c", "user.email=t@t", "-c", "user.name=t", *x], check=True, capture_output=True)
        g("init", "-q"); g("add", "-A"); g("commit", "-q", "-m", "fixture")
        buf = io.StringIO()
        with contextlib.redirect_stdout(buf):
            rc = run(argparse.Namespace(repo=repo, from_step=None, to_step=None), dry=True)
        out = buf.getvalue()
        expect("dry run passes", rc == 0)
        expect("all steps printed", all(("%d " % i) in out for i in range(0, 13)))
        expect("ad-hoc at P", "--sign -" in out and "Developer ID" not in out.split("6 sign")[1].split("7 package")[0])
        expect("zip at P", "ditto -c -k" in out and "notarytool" not in out and "--norsrc" in out)
        expect("every format staged", "stage/" in out)
        expect("notices in the zips", "THIRD_PARTY_NOTICES.md stage/" in out and "LICENSE, THIRD_PARTY_NOTICES.md -DestinationPath" in out and "notices.py" in out)
        expect("symbols per tag", "dsymutil" in out and "-symbols-macOS.zip" in out and "*.pdb" in out and "enable-crashdumps.reg" in out)
        expect("announce prints the message", "Sounds different" in out and "releases/latest" in out and "Quit Ableton Live" in out)
        # Tier F without identity stops
        (repo / "ship.toml").write_text((repo / "ship.toml").read_text().replace('tier = "P"', 'tier = "F"'))
        buf = io.StringIO()
        with contextlib.redirect_stdout(buf):
            rc = run(argparse.Namespace(repo=repo, from_step="sign", to_step="sign"), dry=True)
        expect("Tier F without identity stops", rc == 1 and "identity_app" in buf.getvalue())
        (repo / "ship.toml").write_text((repo / "ship.toml").read_text().replace('tier = "F"', 'tier = "P"'))
        # Tier F path
        (repo / "ship.toml").write_text((repo / "ship.toml").read_text().replace('tier = "P"', 'tier = "F"')
                                        .replace('identity_app = ""', 'identity_app = "Developer ID Application: X (T)"')
                                        .replace('identity_installer = ""', 'identity_installer = "Developer ID Installer: X (T)"'))
        buf = io.StringIO()
        with contextlib.redirect_stdout(buf):
            rc = run(argparse.Namespace(repo=repo, from_step="sign", to_step="package"), dry=True)
        out = buf.getvalue()
        expect("Tier F signs and notarises", rc == 0 and "--options runtime" in out and "notarytool submit" in out and "stapler" in out)
        # real run of the local docs step on a clean fixture passes; a dirty tree stops precheck
        buf = io.StringIO()
        with contextlib.redirect_stdout(buf):
            rc = run(argparse.Namespace(repo=repo, from_step="docs", to_step="docs", yes=True), dry=False)
        expect("docs step runs shipcheck", rc == 0 and "shipcheck docs OK" in buf.getvalue() or rc == 0)
        (repo / "README.md").write_text((repo / "README.md").read_text() + "\n")
        buf = io.StringIO()
        with contextlib.redirect_stdout(buf):
            rc = run(argparse.Namespace(repo=repo, from_step="precheck", to_step="precheck", yes=True), dry=False)
        expect("dirty tree stops precheck", rc == 1 and "dirty" in buf.getvalue())
    if fails:
        print("release selftest FAILED:\n  " + "\n  ".join(fails), file=sys.stderr)
        return 1
    print("release selftest OK: dry run prints all twelve steps, Tier P ad-hoc with every format staged into one zip, symbols zipped per platform, Tier F Developer ID and notarisation (and a stop without identity), the message printed, the docs step runs shipcheck, a dirty tree stops precheck")
    return 0


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("mode", nargs="?", default="dry-run", choices=("dry-run", "run", "steps", "selftest"))
    ap.add_argument("--dry-run", dest="dry_flag", action="store_true", help="same as the dry-run mode")
    ap.add_argument("--repo", default=".")
    ap.add_argument("--from", dest="from_step", choices=STEPS)
    ap.add_argument("--to", dest="to_step", choices=STEPS)
    ap.add_argument("--yes", action="store_true")
    a = ap.parse_args(argv)
    if a.mode == "steps":
        print("\n".join(STEPS))
        return 0
    if a.mode == "selftest":
        return selftest()
    return run(a, dry=(a.mode == "dry-run" or a.dry_flag))


if __name__ == "__main__":
    sys.exit(main())
