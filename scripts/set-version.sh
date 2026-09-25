#!/bin/bash
# Set CedarLogic's version everywhere it's written down.
#
#   scripts/set-version.sh 4.0.1
#
# Each update you publish needs a higher number than the last, or testers'
# apps won't offer it. Betas and normal releases share one sequence (4.0.1,
# 4.0.2, ...); whether a build is a beta is decided when you publish it.
set -euo pipefail
V="${1:-}"; V="${V#v}"
[[ "$V" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]] || { echo "usage: $0 X.Y.Z" >&2; exit 1; }
ROOT="$(cd "$(dirname "$0")/.." && pwd)"

sed -i.bak -E "s/^project\(CedarLogic VERSION [0-9.]+\)/project(CedarLogic VERSION $V)/" "$ROOT/CMakeLists.txt"
rm -f "$ROOT/CMakeLists.txt.bak"
if [ -f "$ROOT/flake.nix" ]; then
    sed -i.bak -E "s/(version = \")[0-9.]+(\";)/\1$V\2/" "$ROOT/flake.nix" && rm -f "$ROOT/flake.nix.bak"
fi
# The native Mac app, where this branch has it.
PL="$ROOT/mac/App/Info.plist"
if [ -f "$PL" ]; then
    /usr/libexec/PlistBuddy -c "Set :CFBundleShortVersionString $V" -c "Set :CFBundleVersion $V" "$PL"
fi
grep -n "^project(CedarLogic VERSION" "$ROOT/CMakeLists.txt"
echo "Version set to $V. Commit it, push, and let CI build before publishing."
