# CedarLogic for Linux (native)

A GTK 3 front end on the same C++ engine the wx app and the native Mac app
use. It draws with Cairo on the processor, so there's no OpenGL anywhere: it
looks and behaves the same on a Raspberry Pi as on a PC. Nothing here is
built by the main CMake project, and nothing here affects the other builds.

    cmake -S linux -B linux/build -G Ninja -DCMAKE_BUILD_TYPE=Release
    cmake --build linux/build
    linux/build/cedarlogic [circuit.cdl ...]

    linux/package.sh          # -> linux/build-release/CedarLogic-Native-<version>-<arch>.AppImage
    linux/package-deb.sh      # -> a .deb that installs to /usr, with a menu entry and .cdl files

People install it with `linux/install.sh` (the one-line command on the
release page): it fetches the .deb for their processor and installs it, and
updates come the same way. The AppImage works too, and adds itself to the
applications menu the first time it runs.

Needs a compiler with C++17, CMake, and the GTK 3, Cairo, fontconfig and
FreeType development files (`libgtk-3-dev libcairo2-dev libfontconfig1-dev
libfreetype-dev` on Debian, Ubuntu and Raspberry Pi OS).

- `CedarCore/CairoScene` -- the render Scene drawn with Cairo, the Linux twin
  of the Mac's `CGScene`. The rest of the engine is `mac/CedarCore`, shared
  with the Mac app: its drawing calls take a Core Graphics context on the Mac
  and a Cairo one here (`mac/CedarCore/NativeScene.h`).
- `App/` -- the GTK app, matched to the Mac and Windows apps: a window per
  circuit (`Window`) with the Mac's toolbar in its title bar in its three
  styles (`Toolbar`), tab cards that spring into place and drag down into a
  split view (`TabStrip`), focus mode (the bars slide away and the tabs
  become the title row, `TitleButtons`), a canvas per page (`Canvas`), the
  side panel, whose gates drag onto the canvas in one motion (`Palette`),
  the status bar where notes appear (`StatusBar`), the Mac's five-page
  Settings and changeable shortcuts (`Settings`, `Shortcuts`,
  `ShortcutsSheet`), the app's own alert card and quit question (`Alert`,
  `QuitConfirm`), gate settings, Add a Gate and the memory editor
  (`Dialogs`, `RamEditor`), Your Circuits and Version History (`Library`,
  `LibraryWindow`, `Picker`), templates and My Parts (`Collections`), Build
  from Formula and the truth table's Karnaugh maps (`Formula`,
  `TruthTableWindow`), Find and the Ctrl+Tab switcher (`FindBar`,
  `TabSwitcher`), the oscilloscope, docked under the canvas (`Scope`),
  Export as Image (`ExportImage`), Send Feedback (`Feedback`), Help
  (`Help`), and the brand's launch screen, welcome, guided tour and What's
  New (`Brand`, `Splash`, `Welcome`). Motion follows the Mac's curves
  (`Anim.h`) and stops when the desktop asks for no animations. Settings
  (`Prefs`) are kept in `~/.config/CedarLogic/native.ini`; circuits in
  `~/.local/share/CedarLogic/Library`.

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
system -- and `formula_check <cl_gatedefs.xml>`, which builds formulas in
every shape and gate style and checks each circuit's truth table. And:

- `cedarlogic --screenshot out.png [circuit.cdl]` -- opens the window, draws
  it to a PNG and quits (CI runs it against the AppImage)
- `cedarlogic --show <window> --screenshot out.png [circuit.cdl]` -- the same
  for one of the app's windows: `welcome:N`, `whatsnew:N`, `help`, `truth`,
  `feedback`, `templates`, `tour`, `find`, `export`, `scope`, `focus`,
  `split`, `quit`, `settings:N`, `toolbar:N`, `shortcuts`, `quickadd`,
  `lock`, `about`, `gatesettings`, `rename`, `ram`, `menu`
- `cedarlogic --hold <seconds> --screenshot out.png` -- waits before the
  picture (CI drags a gate onto the canvas with xdotool meanwhile)
- `cedarlogic --splash-frame <seconds> out.png [--first]` -- the launch screen
- `cedarlogic --feedback-probe` -- asks the feedback site with a wrong key
  (expects 403) and sends nothing
- `cedarlogic --version`
- `CEDARLOGIC_RESOURCES=<dir>` -- where `cl_gatedefs.xml` is, when it isn't
  next to the app
- `CEDAR_FONT_FILE=<font.ttf>` -- the face for circuit labels

CI (`.github/workflows/linux-native.yml`) builds x86_64 and aarch64 on Ubuntu
22.04, runs the checks, packages both AppImages and .debs, starts them for the
pictures above and the drag test, and publishes them as the
`linux-native-testing` pre-release on every push to
`linux/native`.
