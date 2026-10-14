#!/bin/bash
# Builds the VST3 (if needed) and installs it to the CURRENT USER's plugin
# folder (~/Library/Audio/Plug-Ins/VST3), which every major DAW (including
# Ableton Live) scans right alongside the system-wide one. This needs no
# elevated privileges at all - no osascript, no admin password prompt - so
# it can run completely unattended (e.g. from a background agent iterating
# on its own). See the one-time migration note in git history for why this
# replaced the old /Library + osascript version.
#
# Installs a Release build (build-release/, configured here if missing):
# that is what a host's CPU meter should be judged on. A Debug build is
# unoptimised - measured 2026-10-10 at ~6x the audio-thread cost of Release
# (30% vs 5% of a 128-sample buffer, 8 voices) - so use
#   tools/install_plugin.sh --debug
# only to step through the plugin in a debugger.
set -euo pipefail

cd "$(dirname "$0")/.."

CONFIG=Release
BUILD_DIR=build-release

if [[ "${1:-}" == "--debug" ]]; then
    CONFIG=Debug
    BUILD_DIR=build
fi

if [[ ! -f "$BUILD_DIR/CMakeCache.txt" ]]; then
    cmake -B "$BUILD_DIR" -G Ninja -DCMAKE_BUILD_TYPE="$CONFIG"
fi

cmake --build "$BUILD_DIR" --target HorizonPad_VST3 -j 8

SRC="$BUILD_DIR/HorizonPad_artefacts/$CONFIG/VST3/Horizon Pad.vst3"
DEST="$HOME/Library/Audio/Plug-Ins/VST3/Horizon Pad.vst3"

mkdir -p "$(dirname "$DEST")"
rm -rf "$DEST"
cp -R "$SRC" "$DEST"

echo "Installed $CONFIG build to $DEST"
