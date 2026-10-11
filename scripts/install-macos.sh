#!/usr/bin/env bash
# MeterScope - macOS installer (no administrator password needed)
# Copyright (C) 2026 aguarras (TES Lab)
# SPDX-License-Identifier: AGPL-3.0-or-later
#
# Usage (Terminal):
#   curl -fsSL https://raw.githubusercontent.com/aguarras/MeterScope/main/scripts/install-macos.sh | bash
#   curl -fsSL https://raw.githubusercontent.com/aguarras/MeterScope/main/scripts/install-macos.sh | bash -s v1.1.0-beta.1
#
# Without an argument it installs the latest stable release. Downloading with
# curl (not a web browser) means macOS does not quarantine the plugin, so the
# free ad-hoc signature is enough: no Apple notarization required.
set -euo pipefail

REPO="aguarras/MeterScope"
TAG="${1:-}"
if [ -n "$TAG" ]; then
    URL="https://github.com/$REPO/releases/download/$TAG/MeterScope-macOS.zip"
else
    URL="https://github.com/$REPO/releases/latest/download/MeterScope-macOS.zip"
fi

VST3_DIR="$HOME/Library/Audio/Plug-Ins/VST3"
AU_DIR="$HOME/Library/Audio/Plug-Ins/Components"
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

echo "Downloading MeterScope ${TAG:-(latest)}..."
curl -fL --progress-bar -o "$WORK/MeterScope-macOS.zip" "$URL"
ditto -x -k "$WORK/MeterScope-macOS.zip" "$WORK"

mkdir -p "$VST3_DIR" "$AU_DIR"
rm -rf "$VST3_DIR/MeterScope.vst3" "$AU_DIR/MeterScope.component"
cp -R "$WORK/MeterScope-macOS/MeterScope.vst3" "$VST3_DIR/"
cp -R "$WORK/MeterScope-macOS/MeterScope.component" "$AU_DIR/"

# Belt and braces: clear any quarantine flag and refresh the Audio Unit cache.
xattr -dr com.apple.quarantine "$VST3_DIR/MeterScope.vst3" "$AU_DIR/MeterScope.component" 2>/dev/null || true
killall -9 AudioComponentRegistrar 2>/dev/null || true

DOCS="$HOME/Library/Application Support/TES Lab/MeterScope"
mkdir -p "$DOCS"
cp "$WORK/MeterScope-macOS/"{LICENSE,COPYING,COPYING.LESSER,NOTICE.md,README-macOS.md} "$DOCS/"

echo
echo "MeterScope installed:"
echo "  VST3: $VST3_DIR/MeterScope.vst3"
echo "  AU:   $AU_DIR/MeterScope.component"
echo "Open your DAW and rescan plug-ins (REAPER: Preferences > Plug-ins > VST > Re-scan)."
echo "Source code and licence: https://github.com/$REPO"
