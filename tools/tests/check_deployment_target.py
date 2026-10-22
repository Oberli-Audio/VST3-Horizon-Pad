#!/usr/bin/env python3
"""The shipped binary runs on the macOS the README promises (10.13; arm64 cannot go below 11.0).

    python3 tools/tests/check_deployment_target.py "<bundle>/Contents/MacOS/Horizon Pad"

Found 2026-10-22 (Round 14, R.7): CMakeLists.txt set CMAKE_OSX_DEPLOYMENT_TARGET after
project(), where a cache entry CMake had already created empty wins, so every build took
the build machine's macOS - 14.0 on the CI runner, 26.0 locally - and would not load on an
older Mac. Reads each slice's minimum from the Mach-O load commands with otool.
Exit 0 pass, 1 too new, 77 skipped (not macOS, or the binary is not built).
"""
import os
import re
import subprocess
import sys

LIMITS = {"x86_64": (10, 13), "arm64": (11, 0)}


def minimum(binary, arch):
    out = subprocess.run(["otool", "-arch", arch, "-l", binary], capture_output=True, text=True).stdout
    m = re.search(r"cmd LC_BUILD_VERSION.*?minos (\d+)\.(\d+)", out, re.S) or \
        re.search(r"cmd LC_VERSION_MIN_MACOSX.*?version (\d+)\.(\d+)", out, re.S)
    return (int(m.group(1)), int(m.group(2))) if m else None


def main():
    if sys.platform != "darwin" or len(sys.argv) < 2 or not os.path.isfile(sys.argv[1]):
        print("skipped: needs macOS and a built binary")
        return 77
    binary = sys.argv[1]
    archs = subprocess.run(["lipo", "-archs", binary], capture_output=True, text=True).stdout.split()
    bad = []
    for arch in archs:
        got, limit = minimum(binary, arch), LIMITS.get(arch)
        print("%s: minimum macOS %s (limit %s)" % (arch, "%d.%d" % got if got else "?", "%d.%d" % limit if limit else "-"))
        if limit and (got is None or got > limit):
            bad.append(arch)
    if bad:
        print("FAIL: %s needs a newer macOS than the README promises" % ", ".join(bad))
        return 1
    print("PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main())
