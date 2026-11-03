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
applications menu the first time it runs. CI publishes the builds but never
`install.sh`: people pipe it into bash before any signature is checked, so only
`scripts/sign-preview.sh` uploads it, from the commit it signs.

Needs a compiler with C++17, CMake, and the GTK 3, Cairo, Pango, fontconfig,
FreeType and OpenSSL development files (`libgtk-3-dev libcairo2-dev
libpango1.0-dev libfontconfig1-dev libfreetype-dev libssl-dev` on Debian,
Ubuntu and Raspberry Pi OS; `openssl-devel` on Fedora, `openssl` on Arch).
Sync talks to the website through the `curl` program, as Send Feedback does.

Sync (Your Circuits on every device, `docs/SYNC.md`) is the shared engine in
`mac/CedarCore/Sync*.cpp` with this machine's hooks (`App/SyncPlatform`:
OpenSSL's libcrypto for the hashes and AES-GCM, GIO's zlib, curl with the
headers and body in 0600 temp files in the runtime folder, the code in the
desktop's keyring through libsecret when it's installed and unlocked
(`App/Keyring`), else in a 0600 file under `~/.local/share/CedarLogic/Sync`)
behind the app's windows (`App/SyncApp`:
saving open circuits first, holding a change that would land on unsaved work,
reloading a window in place) and Settings > Sync, the code sheet with its QR
code, the link preview, and the line under Your Circuits (`App/SyncUI`).
`cedarlogic://sync#k=<code>` opens Settings > Sync with the code. To try it
without the website: `python3 linux/Tools/sync_mock.py`, then
`CL_SYNC_URL=<the url it prints> linux/build/cedarlogic`.

    linux/build/sync_check                 # the engine's self-test with this app's hooks
    linux/build/cedarlogic --sync-test     # the same, from the installed app
    MOCK=1 linux/Tools/sync-check.sh       # builds it (a Mac with Homebrew's openssl@3 works too) and also
                                           # the curl program's use and two libraries syncing
    SITE=<cedarlogic-site checkout> linux/Tools/sync-check.sh   # and the full scenarios over HTTP

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
  the status bar where notes appear (`StatusBar`), the Actions rail on the
  right edge (`Actions`, `Rail`; below), the Mac's seven-page
  Settings and changeable shortcuts (`Settings`, `Shortcuts`,
  `ShortcutsSheet`), the app's own alert card and quit question (`Alert`,
  `QuitConfirm`), gate settings, Add a Gate and the memory editor
  (`Dialogs`, `RamEditor`), Your Circuits and Version History (`Library`,
  `LibraryWindow`, `Picker`), templates (the Mac's own, as files in `res/templates`,
  written by `mac/Tools/export-templates.sh`) and My Parts (`Collections`), Build
  from Formula and the truth table's Karnaugh maps (`Formula`,
  `TruthTableWindow`), drawing on the circuit (pen, highlighter and eraser,
  saved with it; `Ink`, and the canvas's Draw mode and drawing bar) and the
  notes pane (`Notes`), Find and the Ctrl+Tab switcher (`FindBar`,
  `TabSwitcher`), the oscilloscope, docked under the canvas (`Scope`),
  Export as Image (`ExportImage`), Send Feedback (`Feedback`), Help
  (`Help`), Settings > Sync, the link preview and the sync line in Your
  Circuits (`SyncApp`, `SyncUI`, `SyncPlatform`), and the brand's launch screen, welcome, guided tour and What's
  New (`Brand`, `Splash`, `Welcome`), with the Mac's 0.4 intro before the
  launch screen on the first launch and the first after an update (`Intro`:
  the circuit drawing itself into the icon, then the launch screen; any key
  or click skips it, and it gives way to the still launch screen when the
  animations are off or the frames come too slowly). Motion follows the Mac's curves
  (`Anim.h`) and stops when the desktop asks for no animations. Settings
  (`Prefs`) are kept in `~/.config/CedarLogic/native.ini`; circuits in
  `~/.local/share/CedarLogic/Library`.

The Actions rail (`App/Actions.h`, `App/Rail.h`, the Mac's `mac/App/Actions.swift`):
a strip of icon buttons down the window's right edge, the side panel's mirror.
Resting on one shows its name and shortcut; a click runs it; pressing one and
moving it 6 points (or holding it 0.45 s) picks it up, and dropping it saves
the order the Customize list shows too. A click on its left edge folds it to
a 6-point tab, a click on the tab brings it back; a window narrower than 900
points folds it by itself, and focus mode folds it to a quieter tab (leaving
focus mode gives it back as it was). Folded, the window's right edge is a
12-point hot strip the full height of the canvas, last pixel column included:
the tab lights and widens at once, and after 100 ms there the rail slides out
over the canvas (a `GtkOverlay`, so the canvas keeps its width), until the
pointer has been away from it and the strip for 400 ms (not while its menu
is open or during a drag); Escape puts it back. The strip watches the pointer
through a motion controller on the whole window in its capture phase, so it
sees it over the canvas, the notes or the tab alike. Settings > Actions has
where it goes (On screen, In the toolbar, Hidden), "Open it when the mouse
reaches the right edge" (on by default) and the Customize list: a box to
tick for every menu command, drag by the handle (or Alt+Up/Down) to reorder,
a search by name, menu or shortcut words ("ctrl shift t", any case; no
dragging while searching; Escape clears it), Reset to Default. The rail's
right-click menu opens the same list in its own window. View > Show Actions /
Hide Actions is F9 (the desktop's usual side-pane key; changeable in
Settings > Shortcuts). New installs start On screen; settings from before the
rail (the brand accent or the welcome seen in them) start Hidden, with a
one-time note "New: an Actions rail. Settings ▸ Actions." once no welcome or
What's New is up. Hover and slides draw nothing new each frame (the sliding
rail is drawn once and moved), and none of it moves when the desktop's
animations are off or the frames are slow. Saved as `actionsMode`,
`actionsCollapsed`, `actionsHoverOpen`, `actionsOrder`, `actionsShown` and
`actionsHint` in `native.ini`.

GTK 3 on purpose: every Linux desktop has it (Raspberry Pi OS, Ubuntu, Kali,
Fedora, Mint), it speaks Wayland and X11 natively, and the AppImage uses the
system's copy, so the app picks up the desktop's theme, fonts and input
methods. Dark mode asks the theme for its dark side, and falls back to GTK's
built-in Adwaita when the theme hasn't one.

The canvas draws each frame into an image in memory (on Wayland straight
onto the window's own), keeps the grid in a layer of its own, and only
redraws when the simulation changed something on screen (`CL_TICK_SHOWN`),
so a running circuit that isn't changing costs next to nothing. While parts
are dragged it keeps everything that isn't moving in an image and draws only
the moving parts each frame (`cl_document_draw_layer`). What a drag costs,
and what would make it cheaper: `SMOOTHNESS.md`.

Checks, without opening the app (built with it; `-DCL_BUILD_TOOLS=OFF` skips
them): `cl_check`, `edit_check`, `sim_check`, `tt_check`, `save_check`,
`render_png` -- the Mac app's checks (`mac/Tools`), which run on either
system; `clock_check`, `examples_check`, `register_check` and
`mistakes_check <cl_gatedefs.xml> res/templates`, the Mac's checks of the
templates this app ships (Step Clock, the examples, the registers, the
Classic Mistakes) -- and `formula_check <cl_gatedefs.xml>`, which builds formulas in
every shape and gate style and checks each circuit's truth table. And:

- `cedarlogic --screenshot out.png [circuit.cdl]` -- opens the window, draws
  it to a PNG and quits (CI runs it against the AppImage)
- `cedarlogic --show <window> --screenshot out.png [circuit.cdl]` -- the same
  for one of the app's windows: `welcome:N`, `whatsnew:N`, `help`, `truth`,
  `feedback`, `templates`, `tour`, `find`, `find:1` (Find Next and Previous), `export`, `scope`, `focus`,
  `split`, `quit`, `settings:N`, `toolbar:N`, `shortcuts`, `quickadd`,
  `lock`, `about`, `gatesettings`, `rename`, `ram`, `menu`, `primary` (the ☰ menu), `stepclock:N`, `lockinplace:N`, `pagelinks`, `draw:N`, `projector:N`, `checkclocked:N` (on a counter),
  `rail:N` (the Actions rail: 0 open with a label, 1 folded, 2 peeking, 3 slid
  out, 4 mid-drag, 5 focus mode, 6 focus mode slid out, 7 narrow, 8 Simulation
  View, 9 in the toolbar, 10 focus mode peeking), `customize:N` (the Customize
  window; 1 searched for "export")
- `cedarlogic --actions-check` -- the Actions rail without a window: where it
  starts for a new install and an earlier one, the order shared by the rail
  and the list, the search, the hot strip's geometry and timing, focus mode
  folding it; settings of its own in a temporary folder. The session check
  does the same live, through the window's events
- `cedarlogic --hold <seconds> --screenshot out.png` -- waits before the
  picture (CI drags a gate onto the canvas with xdotool meanwhile)
- `cedarlogic --bench-drag [steps] circuit.cdl` -- times the canvas while
  dragging one gate, everything, and the view, and prints ms per frame
  (`SMOOTHNESS.md` says how to read it); nothing is saved
- `cedarlogic --session-check [actions] [seed]` -- a long working session in
  the window (`App/SessionCheck.cpp`, the Mac's `--session-check`): seeded
  random clicks, drags, keys and commands through the app's own handlers,
  with the windows that open over it poked at and closed, and PASS/FAIL for
  each check (no crash or GTK warning, autosave and the file on disk, undo
  and redo, tabs and split view, Ctrl+Tab, Settings saved, memory, the
  window and its circuit freed on close); exit 1 on a failure. Its settings
  and Your Circuits are in a temporary folder. `linux/Tools/session-check.sh
  <cedarlogic> [actions] [seeds...]` runs it under Xvfb for each seed, again
  with `G_DEBUG=fatal-criticals`, with a time cap; CI runs seeds 1-4, and
  more on a build with AddressSanitizer
- `cedarlogic --splash-frame <seconds> out.png [--first]` -- the launch screen
- `cedarlogic --render-intro <dir> [first|update] [--behind <window.png>]` --
  the intro, every frame at 60 fps (1280 x 800 at 2x), with no display; CI
  keeps the frames at 0.2, 1.0, 2.98, 3.8 and 4.65 s with the renders.
  `CL_INTRO=first|update` plays it on a normal launch, `CL_NO_INTRO=1` never,
  `CL_INTRO_FREEZE=<seconds>` holds it at a moment
- `cedarlogic --feedback-probe` -- asks the feedback site with a wrong key
  (expects 403) and sends nothing
- `cedarlogic --version`
- `CEDARLOGIC_RESOURCES=<dir>` -- where `cl_gatedefs.xml` is, when it isn't
  next to the app
- `CEDAR_FONT_FILE=<font.ttf>` -- the face for circuit labels (text it has
  no glyphs for, and scripts that need shaping, go through Pango and the
  desktop's fallback fonts)
- Drawing switches, for comparing (`SMOOTHNESS.md`): `CEDARLOGIC_GLYPH_TEXT=0`
  labels as filled outlines instead of Cairo's cached glyphs;
  `CEDARLOGIC_DRAW_ALL=1` the whole page every frame of a drag;
  `CEDARLOGIC_DRAG_IMAGE=0` the moving parts drawn every frame instead of
  copied from their image;
  `CEDARLOGIC_CANVAS_FRAME=1` always through the canvas's own image;
  `CEDARLOGIC_FAST_DRAG=1` coarser antialiasing while dragging

CI (`.github/workflows/linux-native.yml`) builds x86_64 and aarch64 on Ubuntu
22.04, runs the checks, packages both AppImages and .debs, starts them for the
pictures above and the drag test, and publishes them as the
`linux-native-testing` pre-release on every push to
`linux/native`.
