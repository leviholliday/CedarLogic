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
  the status bar where notes appear (`StatusBar`), the Actions panel on the
  right edge (`Actions`, `Rail`; below), Help > Show Me Around (`ShowMeAround`; below), the Mac's seven-page
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
  Circuits (`SyncApp`, `SyncUI`, `SyncPlatform`), and the brand's launch screen, welcome, the lesson
  ("Learn Logic: Guided Tour") and What's New (`Brand`, `Splash`, `Welcome`), with the Mac's 0.4 intro before the
  launch screen on the first launch and the first after an update, with the
  chime both times (`Intro`: about 6 s, the circuit drawing itself into the
  icon, then the launch screen, held a moment to be read; any key or click
  skips it; with the animations off it's the still launch screen. It keeps
  its own clock, which never moves on more than a frame's worth at a time,
  so a slow machine plays it slower rather than skipping ahead, and draws
  less on one whose first frames come slowly; `--intro-bench` times its
  frames). Help > Play the Intro plays it
  again over the front window, and Settings > General > "Play the intro
  every time CedarLogic opens" (on unless turned off) plays it, silently, at
  every launch. Motion follows the Mac's curves
  (`Anim.h`) and stops when the desktop asks for no animations. Settings
  (`Prefs`) are kept in `~/.config/CedarLogic/native.ini`; circuits in
  `~/.local/share/CedarLogic/Library`.

The Actions panel (`App/Actions.h`, `App/Rail.h`, the "rail" in the code; the Mac's `mac/App/Actions.swift`):
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
one switch, "Show the Actions panel", and "Open on hover" under it (greyed
while the panel is off; on by default), then the Customize list: a box to
tick for every menu command, drag by the handle (or Alt+Up/Down) to reorder,
a search by name, menu or shortcut words ("ctrl shift t", any case; no
dragging while searching; Escape clears it), Reset to Default. The rail's
right-click menu opens the same list in its own window (with the two
switches), and has "Turn Off the Actions Panel". There is no toolbar mode.
View > Show Actions / Hide Actions is F9 (the desktop's usual side-pane key;
changeable in Settings > Shortcuts); with the panel off it turns it back on.
0.4 turns the panel on for everyone, new installs and updated ones alike
(the old On screen / In the toolbar / Hidden choice included), and unfolds
it, each once (`actionsOn04`, `actionsOpen04`, as the Mac's
`cl.actions.on04` and `cl.actions.open04`); after that, on or off and
folded are the person's own and stay as they're left. The welcome has a
page for it ("The Actions panel": the two switches beside a picture of the
window), and What's New's card for it a "Show or Hide It…" button that
opens the same switches over the page. Hover and slides draw nothing new
each frame (the sliding panel is drawn once and moved), and none of it moves
when the desktop's animations are off or the frames are slow. Saved as
`actionsOn`, `actionsCollapsed`, `actionsHoverOpen`, `actionsOrder`,
`actionsShown`, `actionsOn04` and `actionsOpen04` in `native.ini`.

Show Me Around (`App/ShowMeAround.h`, the Mac's `mac/App/ShowMeAround.swift`;
Help > Show Me Around): a walk round the window in seven stops -- the side
panel of parts, the canvas, the toolbar, the Actions panel, the run
controls, the tabs, and the window's menus (Help, and Settings under File)
-- each a one-sentence card beside the lit place, in the Mac's words, with
Next, Back and Skip; Right, Enter and Space step on, Left back, Escape ends
it. Everything else is dimmed; clicks on the dim do nothing, clicks in the
lit place reach it. The spotlight slides between stops (a cut inside a quick
fade of the card when the desktop's animations are off or frames are slow).
A stop that isn't showing is left out (the panel off, the side panel
hidden); a folded panel opens for its stop and folds back after. It starts
by itself until it's been seen once (finished or skipped,
`showMeAroundSeen`): after the first-run welcome closes, whichever way, after
What's New on an update's first launch, and on any launch before then --
only once a circuit window has the keyboard with nothing over it (not the
launch screen, the intro, the welcome, What's New, a dialog or the lesson).
Chosen from the welcome (or What's New), the lesson "Learn Logic: Guided
Tour" starts right after it, and its last stop offers the lesson too. It's
two drawing layers, one over the title bar and one over the rest, each
drawing its share of one picture; the card is drawn once per stop into an
image, and while the spotlight moves only the part of the layers that
changes is redrawn, with nothing made each frame, so it stays cheap on a
Raspberry Pi. About has "Questions? cedarlogic@proton.me", the address a
mailto link.

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
  View, 9 turned off, 10 focus mode peeking), `customize:N` (the Customize
  window; 1 searched for "export"), `walk:N` (Show Me Around at stop N, 0-6),
  `walksmall:N` (the same in a 1024x600 window), `walkoff:N` (with the panel
  off), `whatsnewpanel` (What's New's Actions panel card), `settingsoff`
  (Settings > Actions with the panel off)
- `cedarlogic --actions-check` -- the Actions panel without a window: on and
  open for a new install and for every earlier setting (before the panel,
  Hidden, In the toolbar, folded; the old keys gone from the file), the
  choice kept after the migration, Settings > Actions' two switches (and no
  toolbar mode), the names of Show Me Around and the lesson, About's mailto,
  the order shared by the panel and the list, the search, the hot strip's geometry and timing, focus mode
  folding it; settings of its own in a temporary folder. The session check
  does the same live, through the window's events, and walks Show Me Around
  with the keys: its stops, a folded panel opened for its stop and folded
  back, the panel's stop left out when it's off, Escape and Done counting as
  seen, starting by itself after each of the welcome's ways out (the lesson
  after it) and after What's New, never over a window, never again once seen
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
- `cedarlogic --render-intro <dir> [first|update|every] [--behind <window.png>]` --
  the intro, every frame at 60 fps (1280 x 800 at 2x, 365 frames), with no
  display; CI keeps the frames at the Mac's v5 stills (0.2, 0.95, 1.7, 2.98,
  3.2, 3.8, 4.95 and 5.8 s) and the hit (2.5-2.7 s) with the renders.
  `CL_INTRO=first|update|every` plays that one on a normal launch,
  `CL_NO_INTRO=1` never, `CL_INTRO_FREEZE=<seconds>` holds it at a moment,
  `CL_INTRO_LOG=1` says on stderr which played and whether with the sound
- `cedarlogic --intro-check` -- the intro's rules with settings of its own:
  the timeline, which launch plays which intro, the sound (the first
  launch's and an update's only), the every-launch setting (off for a new
  install) and Help > Play the Intro in the actions and the menu bar
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
