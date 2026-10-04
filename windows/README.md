# CedarLogic for Windows (native)

Plain Win32 windows and controls, with Direct2D for the canvas, on the same C++
engine the wx app, the native Mac app and the native Linux app use. Everything
it needs comes with Windows 10 and 11 (Direct2D, DirectWrite, WIC, the common
controls): no wxWidgets, no Skia, no runtime to install. Nothing here is built
by the main CMake project, and nothing here affects the other builds.

    cmake -S windows -B windows/build -A x64
    cmake --build windows/build --config Release
    windows\build\Release\CedarLogic.exe [circuit.cdl ...]

It needs Visual Studio 2019 or later (the C++ desktop workload) and CMake.
MinGW-w64 builds it too, including from a Mac, which is handy for checking that
it compiles without a Windows machine:

    cmake -S windows -B build-mingw -DCMAKE_SYSTEM_NAME=Windows \
      -DCMAKE_C_COMPILER=x86_64-w64-mingw32-gcc -DCMAKE_CXX_COMPILER=x86_64-w64-mingw32-g++ \
      -DCMAKE_RC_COMPILER=x86_64-w64-mingw32-windres
    cmake --build build-mingw -j8

- `CedarCore/D2DScene` -- the render Scene drawn with Direct2D, the Windows twin
  of the Mac's `CGScene` and Linux's `CairoScene`. Labels are Arial Bold glyph
  outlines from DirectWrite, as the wx app draws them on Windows. The rest of
  the engine is `mac/CedarCore`, shared with the Mac and Linux apps; its
  drawing calls take a Direct2D render target here (`mac/CedarCore/NativeScene.h`).
- `App/` -- the app, drawn to look like the Mac app (its colours and layout
  are in `Chrome`): a window per circuit (`Window`) whose title bar is the
  `Toolbar` (the circuit's name and menu, the tools in the style Settings
  picks, ••• for every menu, Windows' own window buttons); `TabStrip`, a tab card per page
  (renamed in place), dragged down onto the canvas into a split view of two
  sides, each with its strip; focus mode, where the toolbar and the side
  panel slide away and the tabs become the title row; a `Canvas` per page
  (with the Tidy Up and Lock banner, Simulation View's control bar, and a
  wire's value when the pointer rests on it, drawn over the circuit); the
  status bar where notes appear (`StatusBar`); the side
  panel with the gate palette, My Parts and minimap (`Palette`); the dialogs
  (`Dialogs`, built with the small `Form` helper there, dark in dark mode);
  the app's own alert card for every question and note (`Alert`);
  the Mac app's features: Your Circuits and Version History (`Library`,
  `LibraryWindow`, sharing `%APPDATA%\CedarLogic\Library` with the wx app),
  templates and parts (`Collections`), the truth table with K-maps and
  formulas (`TruthTableWindow`) and Build from Formula (`Formula`), Find
  (`FindBar`), the Ctrl+Tab switcher (`TabSwitcher`), the oscilloscope,
  docked under the canvas, and its timing diagrams (`Scope`), Export as Image (`Export`, with `Images`
  for pictures made off screen), Export Lab Report (`LabReport`: a PDF of the
  circuit, truth table, Karnaugh maps and timing diagram, its pages drawn off
  screen and put in the file by the small writer in `Pdf`), Send Feedback (`Feedback`, to
  cedarlogic.netlify.app), and Help with search (`Help`); the launch screen with the first launch's sound
  (`Splash`), the first-run welcome, What's New and the guided tour that
  builds a circuit with you (`Welcome`), all in the brand's look (`Brand`); updates from the test
  build (`Updater`); recovery copies of unsaved work (`Recovery`); the Start
  menu and .cdl files for a copy run from the zip (`Integration`); Settings
  in the Mac's five pages (`Settings`, on `Form`); every command's keys,
  changeable in Settings > Shortcuts (`Shortcuts`; Ctrl+Alt ones too, such as
  split view's, which a text box leaves to AltGr), and the searchable list
  of them (`ShortcutsSheet`); the toolbar's three styles, Seamless, Classic
  and Minimal (`Toolbar`); and settings and helpers (`Util`; settings live in
  `%APPDATA%\CedarLogic\native.ini`). `Drawn` is the base of the custom-drawn
  parts: Direct2D, in points.
- `res/` -- the manifest (per-monitor DPI, the current look of the standard
  controls, UTF-8 file names) and the resource script (the icon, the .cdl
  files' icon -- the Mac's document icon, `CedarLogicDocument.ico` -- and
  the version).
- `installer/` -- the installer (`CedarLogic.iss`, Inno Setup 6) and its
  pictures; `make-images.swift` makes those and the .cdl icon from the
  Mac's artwork (`swift windows/installer/make-images.swift`, on a Mac).

The app looks for `cl_gatedefs.xml` in `res` beside the exe (or the repo's
`res` when run from the build folder, or `CEDARLOGIC_RESOURCES`). The zip CI
makes is that layout: `CedarLogic.exe` and `res\` with the gates, help and
samples.

## Installing

CI makes two ways to get it, for each processor (x64 and ARM64):

- `CedarLogic-Windows-Setup-<arch>.exe`, the installer (`installer/CedarLogic.iss`).
  For the one user, with no administrator needed: it puts that same folder
  in `%LOCALAPPDATA%\Programs\CedarLogic`, adds CedarLogic to the Start menu
  (and the desktop, if asked), makes .cdl files open in it with their own
  icon (`HKCU\Software\Classes`: `.cdl` -> `CedarLogic.Circuit`), notes where
  it went (`HKCU\Software\CedarLogic\Native`, `InstallDir`), and offers to
  start it at the end. Settings > Apps uninstalls it, leaving Your Circuits
  and the settings. A newer installer installs over an older one; while
  CedarLogic runs, it asks for it to be closed first.

      ISCC /DAppVersion=0.1.0 /DArch=x64 windows\installer\CedarLogic.iss

  (from the repo's root, after packaging `package\CedarLogic` as CI does;
  the version is the CMake project's).
- `CedarLogic-Windows-native-<arch>.zip`: the folder, to run where it's
  unzipped. The first time, when nothing else is on screen, it asks once
  whether to add itself to the Start menu and open .cdl files (the same
  names the installer uses, pointing at this copy); Settings > General's
  Start menu switch, or Help > Add to Start Menu / Remove from Start Menu
  in the ••• menu, changes that later. A copy
  that was added and then moved (a newer zip unzipped elsewhere, the old
  folder gone) follows itself. An installed CedarLogic has these already,
  so a zip copy beside one doesn't ask.

Either way, double-clicking a .cdl opens it in CedarLogic -- in the one
already running, if there is one (a second start hands its files over,
`main.cpp`) -- and updates from inside the app replace `CedarLogic.exe` and
`res` where they are. Windows SmartScreen warns about both until they're
signed: More info, then Run anyway.

Checks, without opening the app (built with it; `-DCL_BUILD_TOOLS=OFF` skips
them): `cl_check`, `edit_check`, `sim_check`, `tt_check`, `save_check`,
`part_check`, `render_png` -- the Mac app's checks (`mac/Tools`), drawing with
Direct2D here. And:

- `CedarLogic.exe --screenshot out.png [circuit.cdl]` -- opens the window,
  captures it to a PNG after two seconds and quits (CI runs it on the zip).
  With `--dark` or `--light` for that run, `--sim-view`, `--split` (split
  view), `--focus` (focus mode), `--rename-tab`, or `--dialog
  preferences|shortcuts|truth-table|add-gate|library|versions|templates|
  formula|scope|export|lab-report|feedback|help|welcome|whatsnew|quit|alert|rename|about|
  start-menu` to capture that instead
  (`--truth-tab N`, `--formula "..."`, `--page N` for the welcome, What's New
  or Settings (0 General ... 4 Shortcuts), `--select <text>` or `--place <gate>` for gate settings, `--help-page id`, and
  `--timing out.png [--timing-color]` with the oscilloscope); `--note "text"`
  puts a note in the status bar, and `--wire-tag` rests the pointer on a wire
  to show its value. `--toolbar-style seamless|classic|minimal` shows the
  toolbar in that style for the run (with the click test too).
- `CedarLogic.exe --lab-report circuit.cdl out.pdf [bw]` -- the circuit run for a
  while, then its lab report (File > Export Lab Report) written without a
  window; `bw` for black and white (CI runs it and keeps the PDFs).
- `CedarLogic.exe --click-test` -- clicks the toolbar's buttons (Zoom In, New
  Tab, Pause and Resume, Simulation View, Lock, the dark mode switch, New),
  the tabs' +, and the window's drawn Minimize (on the toolbar, and on the
  tabs in focus mode) and Close the way Windows sends a click, drags a gate
  from the side panel onto the canvas (SKIP where the pointer can't be
  moved), checks each did what it should, prints PASS, FAIL or SKIP for each
  (a tool the style leaves to ••• is a SKIP) and exits 1 if any failed (CI
  runs it once per toolbar style).
- `CedarLogic.exe --start-menu-test` -- adds this copy to the Start menu
  and .cdl files, checks them (as Windows looks them up), removes them and
  checks they're gone, printing PASS or FAIL for each (CI runs it). It
  changes this account's Start menu and .cdl files while it runs.
- `CedarLogic.exe --feedback-probe` -- asks the feedback site with a wrong
  key (it should answer 403) and sends nothing.
- `CedarLogic.exe --splash-frame <seconds> out.png [--first-launch]` -- the
  launch screen at that moment; `--card-frame <seconds>` holds the opening card.
- `CedarLogic.exe --version`

CI (`.github/workflows/windows-native.yml`) builds x64 and ARM64 on every push
to `windows/native`, packages each as a zip and builds its installer (Inno
Setup 6, installed from Chocolatey when the runner hasn't it), runs the
checks, the screenshots, the click test and the Start menu test on x64,
installs the x64 installer quietly, starts CedarLogic from there, uninstalls
it and checks nothing's left, and publishes the installers and the zips as
the `windows-native-testing` pre-release (installers first in its notes).
The installer's three steps don't hold the zips back for now
(`continue-on-error`): a release without installers says so in its notes.
