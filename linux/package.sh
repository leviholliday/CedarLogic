#!/usr/bin/env bash
# Wrap the native Linux app in an AppImage: one file that runs on most desktop
# distros without installing anything.
#
#   linux/package.sh [build-dir]      (default linux/build-release)
#
# Output: <build-dir>/CedarLogic-Native-<version>-<arch>.AppImage, <arch> being
# the machine it was built on (x86_64 or aarch64). An AppImage holds native
# code, so each CPU needs its own; build on that CPU (CI uses an arm64 runner).
#
# The app needs only the desktop stack every distro has -- GTK 3, Cairo,
# fontconfig, FreeType -- and none of it is bundled: the host's copies pick up
# the user's theme, input methods and fonts. So the AppImage is little more
# than the app and its gate library. Build on the oldest distro you want to
# support (CI uses Ubuntu 22.04), since glibc and GTK only work forwards.
#
# Needs linuxdeploy for the same CPU on PATH, or
# LINUXDEPLOY=/path/to/linuxdeploy-<arch>.AppImage.
set -euo pipefail
cd "$(dirname "$0")/.."

BUILD=${1:-linux/build-release}
ARCH=$(uname -m)
case "$ARCH" in
	x86_64|aarch64) ;;
	*) echo "unsupported CPU for the AppImage: $ARCH" >&2; exit 1 ;;
esac
LINUXDEPLOY=${LINUXDEPLOY:-linuxdeploy}

if [ ! -f "$BUILD/CMakeCache.txt" ]; then
	cmake -S linux -B "$BUILD" -G Ninja -DCMAKE_BUILD_TYPE=Release -DCL_BUILD_TOOLS=OFF
fi
cmake --build "$BUILD" --target CedarLogic

VERSION=$(sed -n 's/^project(CedarLogicNative VERSION \([0-9.]*\).*/\1/p' linux/CMakeLists.txt)
[ -n "$VERSION" ] || { echo "could not read the version from linux/CMakeLists.txt" >&2; exit 1; }

APPDIR="$BUILD/AppDir"
rm -rf "$APPDIR"
DESTDIR="$PWD/$APPDIR" cmake --install "$BUILD" --prefix /usr

# linuxdeploy copies in every shared library the app needs, minus the ones on
# the AppImage excludelist (glibc, fontconfig, FreeType...) and the ones named
# here: the GTK stack and what it stands on, and the C++ runtime (the host's
# is newer, and a host input method written in C++ would need it).
EXCLUDES=()
for lib in \
	'libgtk-3.so*' 'libgdk-3.so*' 'libgdk_pixbuf-2.0.so*' \
	'libglib-2.0.so*' 'libgobject-2.0.so*' 'libgio-2.0.so*' 'libgmodule-2.0.so*' \
	'libpango-1.0.so*' 'libpangocairo-1.0.so*' 'libpangoft2-1.0.so*' \
	'libcairo.so*' 'libcairo-gobject.so*' 'libatk-1.0.so*' 'libatk-bridge-2.0.so*' \
	'libatspi.so*' 'libepoxy.so*' 'libwayland-*.so*' 'libxkbcommon.so*' \
	'libharfbuzz.so*' 'libfribidi.so*' 'libX*.so*' 'libxcb*.so*' \
	'libdbus-1.so*' 'libsystemd.so*' 'libmount.so*' 'libblkid.so*' \
	'libselinux.so*' 'libffi.so*' 'libpcre*.so*' 'libz.so*' 'libpng16.so*' \
	'libexpat.so*' 'libbz2.so*' 'libbrotli*.so*' 'libgraphite2.so*' \
	'libpixman-1.so*' 'libthai.so*' 'libdatrie.so*' \
	'libgcrypt.so*' 'libgpg-error.so*' 'libzstd.so*' 'liblz4.so*' 'liblzma.so*' \
	'libcap.so*' 'libbsd.so*' 'libmd.so*' \
	'libstdc++.so*' 'libgcc_s.so*' 'libfontconfig.so*' 'libfreetype.so*' \
	'libjpeg.so*' 'libtiff.so*' 'libwebp*.so*' 'libdeflate.so*' 'libLerc.so*' 'libjbig.so*' \
	'libsharpyuv.so*' 'libgomp.so*'
do
	EXCLUDES+=(--exclude-library "$lib")
done

OUTNAME="CedarLogic-Native-$VERSION-$ARCH.AppImage"
rm -f "$BUILD"/CedarLogic*-"$ARCH".AppImage
(
	cd "$BUILD"
	export ARCH
	export LINUXDEPLOY_OUTPUT_VERSION="$VERSION"
	export OUTPUT="$OUTNAME" LDAI_OUTPUT="$OUTNAME"
	# Runs linuxdeploy in place of mounting it, so this works in containers
	# and on CI runners that have no FUSE.
	export APPIMAGE_EXTRACT_AND_RUN=1
	"$LINUXDEPLOY" --appdir AppDir \
		--executable AppDir/usr/bin/cedarlogic \
		--desktop-file AppDir/usr/share/applications/cedarlogic.desktop \
		--icon-file AppDir/usr/share/icons/hicolor/256x256/apps/cedarlogic.png \
		"${EXCLUDES[@]}" \
		--output appimage
)

# Nothing should have been bundled: say so if something was.
BUNDLED=$(find "$APPDIR/usr/lib" -name '*.so*' 2>/dev/null | sed 's|.*/||' | sort | tr '\n' ' ')
[ -z "$BUNDLED" ] || echo "note: bundled libraries: $BUNDLED" >&2

OUT="$BUILD/$OUTNAME"
[ -f "$OUT" ] || OUT=$(ls "$BUILD"/CedarLogic*-"$ARCH".AppImage 2>/dev/null | head -1)
[ -n "$OUT" ] && [ -f "$OUT" ] || { echo "linuxdeploy did not produce an AppImage" >&2; exit 1; }
chmod +x "$OUT"
echo "$OUT"
