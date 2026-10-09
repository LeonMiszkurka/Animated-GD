#!/usr/bin/env bash
# Builds the mod and installs it into Steam Geometry Dash on macOS.
# One-time setup (see README.md): Xcode Command Line Tools, cmake, ninja,
# the Geode CLI and the Geode SDK.
set -euo pipefail
cd "$(dirname "$0")"

GD_MODS="$HOME/Library/Application Support/Steam/steamapps/common/Geometry Dash/Geometry Dash.app/Contents/geode/mods"

if [[ -z "${GEODE_SDK:-}" ]]; then
    echo "GEODE_SDK is not set. Run: geode sdk install && geode sdk install-binaries, then restart your shell." >&2
    exit 1
fi

# arm64 only keeps local builds fast; CI builds the universal binary.
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo -DCMAKE_OSX_ARCHITECTURES=arm64
cmake --build build

GEODE_FILE=$(ls build/*.geode 2>/dev/null | head -n1 || true)
if [[ -z "$GEODE_FILE" ]]; then
    echo "Build finished but no .geode package was produced." >&2
    exit 1
fi

cp "$GEODE_FILE" "$GD_MODS/"
echo "Installed $(basename "$GEODE_FILE") -> $GD_MODS"
