#!/usr/bin/env bash
# Wrap the native Linux app in a .deb: installed like any app (double-click
# it, or `sudo apt install ./file.deb`), with its icon in the menu, .cdl files
# opening in it, and nothing to make executable.
#
#   linux/package-deb.sh [build-dir] [build-number]
#
# Output: <build-dir>/cedarlogic_<version>_<arch>.deb (amd64 or arm64). The
# version gets the build number on the end, so every build is newer than the
# last and installs over it. Built on Ubuntu 22.04 it runs on that and newer,
# Debian 12 and Raspberry Pi OS Bookworm.
set -euo pipefail
cd "$(dirname "$0")/.."

BUILD=${1:-linux/build-release}
NUMBER=${2:-0}
case "$(uname -m)" in
	x86_64) DEBARCH=amd64 ;;
	aarch64) DEBARCH=arm64 ;;
	*) echo "unsupported CPU for the .deb: $(uname -m)" >&2; exit 1 ;;
esac

if [ ! -f "$BUILD/CMakeCache.txt" ]; then
	cmake -S linux -B "$BUILD" -G Ninja -DCMAKE_BUILD_TYPE=Release -DCL_BUILD_TOOLS=OFF
fi
cmake --build "$BUILD" --target CedarLogic

VERSION=$(sed -n 's/^project(CedarLogicNative VERSION \([0-9.]*\).*/\1/p' linux/CMakeLists.txt)
[ -n "$VERSION" ] || { echo "could not read the version from linux/CMakeLists.txt" >&2; exit 1; }
DEBVERSION="$VERSION+$NUMBER"

ROOT="$BUILD/deb"
rm -rf "$ROOT"
DESTDIR="$PWD/$ROOT" cmake --install "$BUILD" --prefix /usr
strip "$ROOT/usr/bin/cedarlogic" 2>/dev/null || true
mkdir -p "$ROOT/DEBIAN"
SIZE=$(du -sk "$ROOT/usr" | cut -f1)
cat > "$ROOT/DEBIAN/control" <<EOT
Package: cedarlogic
Version: $DEBVERSION
Architecture: $DEBARCH
Maintainer: CedarLogic <leviholliday7@gmail.com>
Installed-Size: $SIZE
Depends: libc6 (>= 2.35), libgtk-3-0, libcairo2, libfontconfig1, libfreetype6, libstdc++6
Recommends: curl
Section: education
Priority: optional
Homepage: https://cedarlogic.netlify.app
Description: Digital logic simulator
 Build logic circuits from gates, switches and lights, and watch them run
 live: truth tables, Karnaugh maps, an oscilloscope, and exports for lab
 reports. The native Linux CedarLogic: GTK and Cairo, no OpenGL, so it runs
 the same on a Raspberry Pi as on a PC.
EOT
OUT="$BUILD/cedarlogic_${DEBVERSION}_${DEBARCH}.deb"
rm -f "$BUILD"/cedarlogic_*_"$DEBARCH".deb
dpkg-deb --build --root-owner-group "$ROOT" "$OUT" >&2
echo "$OUT"
