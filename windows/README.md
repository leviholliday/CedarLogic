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
  `Toolbar` (the circuit's name and menu, the tools in capsules, ••• for
  every menu, Windows' own window buttons); `TabStrip`, a tab card per page,
  over a `Canvas` per page (with the note toast, the Tidy Up and Lock
  banner, and Simulation View's control bar drawn over the circuit); the side
  panel with the gate palette, My Parts and minimap (`Palette`); the dialogs
  (`Dialogs`, built with the small `Form` helper there, dark in dark mode);
  the Mac app's features: Your Circuits and Version History (`Library`,
  `LibraryWindow`, sharing `%APPDATA%\CedarLogic\Library` with the wx app),
  templates and parts (`Collections`), the truth table with K-maps and
  formulas (`TruthTableWindow`) and Build from Formula (`Formula`), Find
  (`FindBar`), the Ctrl+Tab switcher (`TabSwitcher`), the oscilloscope and
  its timing diagrams (`Scope`), Export as Image (`Export`, with `Images`
  for pictures made off screen), Send Feedback (`Feedback`, to
  cedarlogic.netlify.app), and Help with search (`Help`); the launch screen with the first launch's sound
  (`Splash`), the first-run welcome, What's New and the guided tour that
  builds a circuit with you (`Welcome`), all in the brand's look (`Brand`); updates from the test
  build (`Updater`); recovery copies of unsaved work (`Recovery`); and
  settings and helpers (`Util`; settings live in
  `%APPDATA%\CedarLogic\native.ini`). `Drawn` is the base of the custom-drawn
  parts: Direct2D, in points.
- `res/` -- the manifest (per-monitor DPI, the current look of the standard
  controls, UTF-8 file names) and the resource script (the icon, version).

The app looks for `cl_gatedefs.xml` in `res` beside the exe (or the repo's
`res` when run from the build folder, or `CEDARLOGIC_RESOURCES`). The zip CI
makes is that layout: `CedarLogic.exe` and `res\` with the gates, help and
samples.

Checks, without opening the app (built with it; `-DCL_BUILD_TOOLS=OFF` skips
them): `cl_check`, `edit_check`, `sim_check`, `tt_check`, `save_check`,
`part_check`, `render_png` -- the Mac app's checks (`mac/Tools`), drawing with
Direct2D here. And:

- `CedarLogic.exe --screenshot out.png [circuit.cdl]` -- opens the window,
  captures it to a PNG after two seconds and quits (CI runs it on the zip).
  With `--dark` or `--light` for that run, `--sim-view`, or `--dialog
  preferences|shortcuts|truth-table|add-gate|library|versions|templates|
  formula|scope|export|feedback|help|welcome|whatsnew` to capture that instead
  (`--truth-tab N`, `--formula "..."`, `--page N` for the welcome, What's New
  or Preferences, `--select <text>` or `--place <gate>` for gate settings, `--help-page id`, and
  `--timing out.png [--timing-color]` with the oscilloscope).
- `CedarLogic.exe --feedback-probe` -- asks the feedback site with a wrong
  key (it should answer 403) and sends nothing.
- `CedarLogic.exe --splash-frame <seconds> out.png [--first-launch]` -- the
  launch screen at that moment; `--card-frame <seconds>` holds the opening card.
- `CedarLogic.exe --version`

CI (`.github/workflows/windows-native.yml`) builds x64 and ARM64 on every push
to `windows/native`, runs the checks and the screenshot on x64, and publishes
the zips as the `windows-native-testing` pre-release.
