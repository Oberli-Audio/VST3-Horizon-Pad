#!/usr/bin/env bash
# Horizon Pad - macOS .pkg installer builder
#
# Builds an installer package that puts the plugin at the system-wide
# location (/Library/Audio/Plug-Ins/VST3/), matching the project's decision
# to install system-wide even though the build is unsigned/unnotarized.
# pkgbuild itself needs no admin rights to *build* the package - only
# *installing* it (writing under /Library) needs admin, which macOS's
# Installer app prompts for at install time.
#
# This produces an UNSIGNED package (no Apple Developer ID on this project -
# see README "Known gaps" / the project's distribution decisions). End users
# will see a Gatekeeper warning and need to right-click > Open, or allow it
# in System Settings > Privacy & Security, the first time they run it.
#
# Usage:
#   packaging/macos/build-pkg.sh "<path-to>/Horizon Pad.vst3" "<output-dir>"
#
# Requires: pkgbuild (ships with Xcode Command Line Tools / macOS itself -
# nothing extra to install on a Mac or a macOS CI runner).

set -euo pipefail

VST3_PATH="${1:?Usage: $0 <path-to-Horizon-Pad.vst3> <output-dir>}"
OUT_DIR="${2:?Usage: $0 <path-to-Horizon-Pad.vst3> <output-dir>}"

IDENTIFIER="com.oberliaudio.horizonpad.installer"
VERSION="1.0.0"
INSTALL_LOCATION="/Library/Audio/Plug-Ins/VST3"
PKG_NAME="Horizon Pad.pkg"
SCRIPTS_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/scripts" && pwd)"

if [ ! -d "$VST3_PATH" ]; then
    echo "ERROR: '$VST3_PATH' not found (expected the built Horizon Pad.vst3 bundle)." >&2
    exit 1
fi

if [ ! -x "$SCRIPTS_DIR/postinstall" ]; then
    echo "ERROR: '$SCRIPTS_DIR/postinstall' not found or not executable." >&2
    exit 1
fi

mkdir -p "$OUT_DIR"

# pkgbuild's --root mirrors --install-location: put the .vst3 in a staging
# root exactly as it should appear under /Library/Audio/Plug-Ins/VST3/.
STAGING_DIR="$(mktemp -d)"
COMPONENT_DIR="$(mktemp -d)"
trap 'rm -rf "$STAGING_DIR" "$COMPONENT_DIR"' EXIT

cp -R "$VST3_PATH" "$STAGING_DIR/"

# Re-sign ad-hoc (this project has no Developer ID) right after copying
# into the staging root. codesign's sealed-resource envelope is only valid
# for the exact bundle contents present at sign time - any copy, artifact
# round-trip, or repackaging step since the original build can leave a
# stale signature, which surfaces to end users as "a sealed resource is
# missing or invalid" the moment their DAW scans the installed plugin.
# Re-signing the exact copy that's about to be packaged, right before
# pkgbuild runs, guarantees it is internally consistent no matter what
# happened upstream.
STAGED_BUNDLE="$STAGING_DIR/$(basename "$VST3_PATH")"
BINARY="$(find "$STAGED_BUNDLE/Contents/MacOS" -type f | head -n 1)"
if [ -z "$BINARY" ] || [ ! -s "$BINARY" ]; then
    echo "ERROR: no (or empty) executable found under $STAGED_BUNDLE/Contents/MacOS" >&2
    ls -laR "$STAGED_BUNDLE" >&2
    exit 1
fi
codesign --force --deep --sign - "$STAGED_BUNDLE"
codesign --verify --deep --strict "$STAGED_BUNDLE"

# pkgbuild auto-analyzes bundles under --root and, for any it recognises as
# relocatable (chiefly .app bundles), may flag them BundleIsRelocatable -
# which tells Installer.app to search the whole disk for a bundle sharing
# the same identifier and install there instead of at --install-location.
# A .vst3 typically isn't flagged (pkgbuild's --analyze commonly returns an
# empty component list for it), but force BundleIsRelocatable false on
# every entry --analyze does produce, so the install location is pinned no
# matter how pkgbuild classifies it on a given macOS/Xcode version. Uses
# plistlib rather than PlistBuddy's `Set` because `Set` errors out when the
# key/array entry it expects isn't there (e.g. an empty component list).
COMPONENT_PLIST="$COMPONENT_DIR/component.plist"
pkgbuild --analyze --root "$STAGING_DIR" "$COMPONENT_PLIST"
python3 - "$COMPONENT_PLIST" << 'PYEOF'
import plistlib
import sys

path = sys.argv[1]
with open(path, "rb") as f:
    data = plistlib.load(f)
for entry in data:
    entry["BundleIsRelocatable"] = False
with open(path, "wb") as f:
    plistlib.dump(data, f)
PYEOF

# --scripts wires in packaging/macos/scripts/postinstall, which runs on
# the end user's Mac right after Installer.app extracts this payload to
# its final location - it re-signs ad-hoc there (the true last mile of
# the distribution chain) and, critically, exits nonzero if anything is
# missing or invalid, which makes Installer.app report a real failure
# instead of silently showing "The installation was successful" for a
# broken install.
pkgbuild \
    --root "$STAGING_DIR" \
    --component-plist "$COMPONENT_PLIST" \
    --scripts "$SCRIPTS_DIR" \
    --identifier "$IDENTIFIER" \
    --version "$VERSION" \
    --install-location "$INSTALL_LOCATION" \
    "$OUT_DIR/$PKG_NAME"

echo "Built: $OUT_DIR/$PKG_NAME"
