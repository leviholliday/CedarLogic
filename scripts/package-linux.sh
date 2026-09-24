#!/usr/bin/env bash
# Wrap a Linux build of CedarLogic in an AppImage: one file that runs on most
# desktop distros without installing anything.
#
#   scripts/package-linux.sh [build-dir]
#
# Output: <build-dir>/CedarLogic-<version>-<arch>.AppImage, where <arch> is the
# machine it was built on: x86_64 or aarch64. An AppImage holds native code, so
# each CPU needs its own; build on that CPU (CI uses an arm64 runner).
#
# Configure the build with -DUSE_SYSTEM_WXWIDGETS=OFF first. That links a static
# wxWidgets with no JPEG/TIFF, so the only shared libraries left are the desktop
# stack every distro ships.
#
# GTK, GLib, Pango, Cairo and friends are deliberately NOT bundled: the host's
# copies pick up the user's theme (and its dark variant), input methods and
# fonts, and a bundled GTK tends to break against the host's pixbuf loaders.
# Build on the oldest distro you want to support (CI uses Ubuntu 22.04), since
# glibc and GTK only work forwards.
#
# Needs linuxdeploy for the same CPU on PATH, or
# LINUXDEPLOY=/path/to/linuxdeploy-<arch>.AppImage.
set -euo pipefail
cd "$(dirname "$0")/.."

BUILD=${1:-build}
ARCH=$(uname -m)
case "$ARCH" in
    x86_64|aarch64) ;;
    *) echo "unsupported CPU for the AppImage: $ARCH" >&2; exit 1 ;;
esac
LINUXDEPLOY=${LINUXDEPLOY:-linuxdeploy}

cmake --build "$BUILD" --target CedarLogic -j"$(nproc)"

VERSION=$(sed -n 's/^project(CedarLogic VERSION \([0-9.]*\)).*/\1/p' CMakeLists.txt)
[ -n "$VERSION" ] || { echo "could not read the version from CMakeLists.txt" >&2; exit 1; }

APPDIR="$BUILD/AppDir"
rm -rf "$APPDIR"
DESTDIR="$PWD/$APPDIR" cmake --install "$BUILD" --prefix /usr

# linuxdeploy copies in every shared library the binary needs, minus the ones
# on the AppImage excludelist (glibc, libGL, fontconfig, ...) and the ones
# named here. Patterns match file names. The last rows are what the excluded
# system libraries (libsystemd, libxcb, ...) depend on: bundled, they could only
# ever meet the host's copies of those libraries, which want their own.
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
    'libcap.so*' 'libbsd.so*' 'libmd.so*'
do
    EXCLUDES+=(--exclude-library "$lib")
done

rm -f "$BUILD"/CedarLogic-*-"$ARCH".AppImage
(
    cd "$BUILD"
    export ARCH
    export LINUXDEPLOY_OUTPUT_VERSION="$VERSION"
    # Runs linuxdeploy in place of mounting it, so this works in containers
    # and on CI runners that have no FUSE.
    export APPIMAGE_EXTRACT_AND_RUN=1
    "$LINUXDEPLOY" --appdir AppDir \
        --executable AppDir/usr/bin/CedarLogic \
        --desktop-file AppDir/usr/share/applications/CedarLogic.desktop \
        --icon-file AppDir/usr/share/icons/hicolor/256x256/apps/CedarLogic.png \
        "${EXCLUDES[@]}" \
        --output appimage
)

OUT=$(ls "$BUILD"/CedarLogic-*-"$ARCH".AppImage 2>/dev/null | head -1)
[ -n "$OUT" ] || { echo "linuxdeploy did not produce an AppImage" >&2; exit 1; }
chmod +x "$OUT"
echo "$OUT"
