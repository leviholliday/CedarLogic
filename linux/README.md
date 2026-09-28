# CedarLogic for Linux (native)

A GTK 3 front end on the same C++ engine the wx app and the native Mac app
use. It draws with Cairo on the processor, so there's no OpenGL anywhere: it
looks and behaves the same on a Raspberry Pi as on a PC. Nothing here is
built by the main CMake project, and nothing here affects the other builds.

    cmake -S linux -B linux/build -G Ninja -DCMAKE_BUILD_TYPE=Release
    cmake --build linux/build
    linux/build/cedarlogic [circuit.cdl ...]

    linux/package.sh          # -> linux/build-release/CedarLogic-Native-<version>-<arch>.AppImage

Needs a compiler with C++17, CMake, and the GTK 3, Cairo, fontconfig and
FreeType development files (`libgtk-3-dev libcairo2-dev libfontconfig1-dev
libfreetype-dev` on Debian, Ubuntu and Raspberry Pi OS).

- `CedarCore/CairoScene` -- the render Scene drawn with Cairo, the Linux twin
  of the Mac's `CGScene`. The rest of the engine is `mac/CedarCore`, shared
  with the Mac app: its drawing calls take a Core Graphics context on the Mac
  and a Cairo one here (`mac/CedarCore/NativeScene.h`).
- `App/` -- the GTK app: a window per circuit (`Window`), a tab and a canvas
  per page (`Canvas`), the gate palette (`Palette`), the dialogs and the
  oscilloscope (`Dialogs`), settings (`Prefs`, kept in
  `~/.config/CedarLogic/native.ini`).

GTK 3 on purpose: every Linux desktop has it (Raspberry Pi OS, Ubuntu, Kali,
Fedora, Mint), it speaks Wayland and X11 natively, and the AppImage uses the
system's copy, so the app picks up the desktop's theme, fonts and input
methods. Dark mode asks the theme for its dark side, and falls back to GTK's
built-in Adwaita when the theme hasn't one.

The canvas draws each frame into an image in memory, keeps the grid in a
layer of its own, and only redraws when the simulation changed something on
screen (`CL_TICK_SHOWN`), so a running circuit that isn't changing costs next
to nothing.

Checks, without opening the app (built with it; `-DCL_BUILD_TOOLS=OFF` skips
them): `cl_check`, `edit_check`, `sim_check`, `tt_check`, `save_check`,
`render_png` -- the Mac app's checks (`mac/Tools`), which run on either
system. And:

- `cedarlogic --screenshot out.png [circuit.cdl]` -- opens the window, draws
  it to a PNG and quits (CI runs it against the AppImage)
- `cedarlogic --version`
- `CEDARLOGIC_RESOURCES=<dir>` -- where `cl_gatedefs.xml` is, when it isn't
  next to the app
- `CEDAR_FONT_FILE=<font.ttf>` -- the face for circuit labels

CI (`.github/workflows/linux-native.yml`) builds x86_64 and aarch64 on Ubuntu
22.04, runs the checks, packages and starts both AppImages, and publishes
them as the `linux-native-testing` pre-release on every push to
`linux/native`.
