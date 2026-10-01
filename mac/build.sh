#!/bin/bash
# Build the native Mac app (no Xcode needed -- the Command Line Tools will do).
#
#   mac/build.sh            build mac/build/CedarLogic.app
#   CLEAN=1 mac/build.sh    rebuild everything (after changing a header)
#   OPEN=1 mac/build.sh     then launch it
#
# The engine (C++ shared with the wx app, built with CL_NO_WX) becomes
# libCedarCore.a; the Swift app links it through CedarCore.h.
set -euo pipefail
cd "$(dirname "$0")/.."

OUT=mac/build
OBJ=$OUT/obj
APP="$OUT/CedarLogic.app"
ARCH=$(uname -m)
MIN=14.0
[ "${CLEAN:-0}" = 1 ] && rm -rf "$OUT"
mkdir -p "$OBJ"

CORE=(
	# the model, drawing, routing and loading/saving, shared with the wx app
	src/gui/guiGate.cpp src/gui/guiWire.cpp src/gui/wireSegment.cpp
	src/gui/klsCollisionChecker.cpp src/gui/GateLibrary.cpp src/gui/LibraryParse.cpp
	src/gui/XMLParser.cpp src/gui/GUICircuitModel.cpp src/gui/RenderMode.cpp
	src/gui/PaletteDrag.cpp src/gui/Settings.cpp src/gui/gl_defs.cpp src/gui/CircuitParse.cpp
	src/gui/klsClipboard.cpp src/gui/CircuitEdits.cpp
	src/gui/route/TrunkRouter.cpp src/gui/route/GridRouter.cpp src/gui/route/Layout.cpp
	# the editing commands (undo/redo), minus the two that manage wx tabs
	$(ls src/gui/command/*.cpp | grep -v -e cmdAddTab -e cmdDeleteTab)
	# the logic engine and the file format library
	logic/src/*.cpp
	format/circuit_file_io.cpp format/legacy_cdl.cpp format/migrate.cpp format/numeric.cpp format/sexpr.cpp
	# the native side
	mac/CedarCore/*.cpp
)
CXXFLAGS=(-std=c++17 -O2 -arch "$ARCH" -mmacosx-version-min=$MIN -DCL_NO_WX
	-Wno-deprecated-declarations -Wno-inconsistent-missing-override -D_PRODUCTION_ -Iinclude -Iinclude/gui -Iinclude/gui/command -Ilogic/include -Iformat -Imac/CedarCore -Imac/CedarCore/include)

echo "Engine..."
OBJS=()
for f in "${CORE[@]}"; do
	o="$OBJ/$(echo "$f" | tr / _).o"
	d="${o%.o}.d"
	OBJS+=("$o")
	# Recompile when the source or any header it includes changed (the .d
	# file clang writes lists them). Missing that once let two files disagree
	# about a struct's layout.
	stale=0
	if [ ! -f "$o" ] || [ ! -f "$d" ] || [ "$f" -nt "$o" ]; then
		stale=1
	else
		for h in $(sed -e 's/^[^:]*://' -e 's/\\$//' "$d"); do
			if [ "$h" -nt "$o" ]; then stale=1; break; fi
		done
	fi
	if [ $stale = 1 ]; then
		clang++ "${CXXFLAGS[@]}" -MMD -MF "$d" -c "$f" -o "$o"
	fi
done
rm -f "$OUT/libCedarCore.a"
ar rcs "$OUT/libCedarCore.a" "${OBJS[@]}"

# Sparkle.framework: the copy the wx build downloads (configure it once).
SPARKLE_DIR="${SPARKLE_DIR:-$(cd "$(git rev-parse --git-common-dir)/.." && pwd)/build/_deps/sparkle-src}"
[ -d "$SPARKLE_DIR/Sparkle.framework" ] || { echo "Sparkle.framework not found in $SPARKLE_DIR (set SPARKLE_DIR)"; exit 1; }

# Sentry.framework, for crash reports (App/CrashReports.swift): the Mac part
# of getsentry/sentry-cocoa's Sentry-Dynamic.xcframework, unpacked beside
# Sparkle. Without it the app builds with crash reports left out.
SENTRY_DIR="${SENTRY_DIR:-$(cd "$(git rev-parse --git-common-dir)/.." && pwd)/build/_deps/sentry-cocoa/mac}"
SENTRY_FLAGS=()
[ -d "$SENTRY_DIR/Sentry.framework" ] && SENTRY_FLAGS=(-F "$SENTRY_DIR" -framework Sentry)
# Where reports go: the Sentry project's DSN (Settings > Client Keys).
SENTRY_DSN="${SENTRY_DSN:-https://36b411e83b623ea3bc3a7bc6fda107a0@o4512167663239168.ingest.us.sentry.io/4512167685849088}"

echo "App..."
mkdir -p "$APP/Contents/MacOS" "$APP/Contents/Resources"
swiftc -O -parse-as-library -target "$ARCH-apple-macos$MIN" \
	-import-objc-header mac/CedarCore/include/CedarCore.h \
	mac/App/*.swift "$OUT/libCedarCore.a" -lc++ \
	-framework CoreText -framework OpenGL \
	-F "$SPARKLE_DIR" -framework Sparkle ${SENTRY_FLAGS[@]+"${SENTRY_FLAGS[@]}"} -Xlinker -rpath -Xlinker @executable_path/../Frameworks \
	-o "$APP/Contents/MacOS/CedarLogic"
# Sparkle, for updates (see App/Updates.swift).
rm -rf "$APP/Contents/Frameworks"; mkdir -p "$APP/Contents/Frameworks"
ditto "$SPARKLE_DIR/Sparkle.framework" "$APP/Contents/Frameworks/Sparkle.framework"
if [ ${#SENTRY_FLAGS[@]} -gt 0 ]; then
	# Just what runs, for this Mac's architecture (it's 29 MB whole).
	ditto --arch "$ARCH" "$SENTRY_DIR/Sentry.framework" "$APP/Contents/Frameworks/Sentry.framework"
	rm -rf "$APP/Contents/Frameworks/Sentry.framework/Versions/A/"{Headers,PrivateHeaders,Modules} \
		"$APP/Contents/Frameworks/Sentry.framework/"{Headers,PrivateHeaders,Modules}
fi

cp mac/App/Info.plist "$APP/Contents/Info.plist"
# The build number is the commit count, and About shows the commit too, so
# two copies of the app can be told apart ("+" means uncommitted changes).
BUILD=$(git rev-list --count HEAD 2>/dev/null || echo 0)
COMMIT=$(git rev-parse --short HEAD 2>/dev/null || echo unknown)
[ -n "$(git status --porcelain -- mac src include logic format 2>/dev/null)" ] && COMMIT="$COMMIT+"
VERSION=$(/usr/libexec/PlistBuddy -c "Print CFBundleShortVersionString" mac/App/Info.plist)
/usr/libexec/PlistBuddy -c "Set CFBundleVersion $BUILD" "$APP/Contents/Info.plist"
/usr/libexec/PlistBuddy -c "Add CFBundleGetInfoString string CedarLogic $VERSION (build $BUILD, $COMMIT)" "$APP/Contents/Info.plist"
/usr/libexec/PlistBuddy -c "Add CLCommit string $COMMIT" "$APP/Contents/Info.plist"
cp res/cl_gatedefs.xml "$APP/Contents/Resources/"
cp mac/App/CedarLogic.icns mac/App/CedarLogicDocument.icns mac/App/LaunchIcon.png mac/App/FirstLaunch.m4a "$APP/Contents/Resources/"
# The original CedarLogic help pages, for Help > Classic Help. (The icons,
# LaunchIcon.png included, come from mac/Tools/make-icons.sh.)
rm -rf "$APP/Contents/Resources/ClassicHelp"
ditto res/help "$APP/Contents/Resources/ClassicHelp"
rm -f "$APP/Contents/Resources/CedarLogicNative.icns"
# The update key's public half (private half: keychain account "cedarlogic").
/usr/libexec/PlistBuddy -c "Add SUPublicEDKey string yxLh+j07mcolZ462R1sZtniQPJ+hjvkKSgtNG6dY700=" "$APP/Contents/Info.plist"
/usr/libexec/PlistBuddy -c "Add SUFeedURL string https://raw.githubusercontent.com/leviholliday/CedarLogic-Releases/main/appcast-native.xml" "$APP/Contents/Info.plist"
/usr/libexec/PlistBuddy -c "Add SUEnableAutomaticChecks bool true" "$APP/Contents/Info.plist"
[ -n "$SENTRY_DSN" ] && /usr/libexec/PlistBuddy -c "Add SentryDSN string $SENTRY_DSN" "$APP/Contents/Info.plist"
# Send Feedback's server (the cedarlogic-site repository) and the key it
# expects (its APP_KEY: not a secret, it keeps out passers-by).
/usr/libexec/PlistBuddy -c "Add FeedbackURL string ${FEEDBACK_URL:-https://cedarlogic.netlify.app}" "$APP/Contents/Info.plist"
/usr/libexec/PlistBuddy -c "Add FeedbackKey string ${FEEDBACK_KEY:-96611596304230a4c3c21e22b1b3e523}" "$APP/Contents/Info.plist"
codesign --force --deep --sign - "$APP" >/dev/null 2>&1
echo "Built $APP"
[ "${OPEN:-0}" = 1 ] && open "$APP"
exit 0
