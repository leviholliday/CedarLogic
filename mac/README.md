# CedarLogic for Mac (native)

A SwiftUI/AppKit front end on top of the same C++ engine the wx app uses.
Nothing here is built by the main CMake project, and nothing here affects the
Windows or Linux builds.

    mac/build.sh              # -> mac/build/CedarLogic.app
    OPEN=1 mac/build.sh       # build, then launch
    CLEAN=1 mac/build.sh      # rebuild everything
    mac/package.sh            # build, then mac/build/CedarLogic-<version>-Mac.dmg

Needs only the Xcode Command Line Tools (Swift 6), macOS 14 or later.

- `CedarCore/` -- the engine for the app: a Core Graphics backend for the
  render Scene (`CGScene`), the document loader (`Document.cpp`) and the plain C
  interface Swift calls (`include/CedarCore.h`). It compiles the shared sources
  in `src/gui`, `format/` and `src/gui/route` with `CL_NO_WX`, which fences off
  the few wxWidgets pieces in them.
- `App/` -- the Swift app: documents, the canvas view, looks (presets you can
  customize) and Settings.

Two interfaces (Settings > General):

- **CedarLogic** (the default) -- the wx app's, rebuilt natively: its drawn
  toolbar in four styles, modern tabs (close with undo, reopen, reorder,
  rename, split view), the palette and minimap, the status bar, Simulation
  View (marching signals and its control bar), quick add (A), the single
  keys (C/V/X/D/T/K/?, Shift+1-9), paste and new gates that follow the pointer,
  connect-nearby (C while dragging), Lock, focus mode, the RAM editor, Export
  as Image with your name and whether it works, the welcome, the guided tour,
  and every setting from its Preferences. Its settings are read from the wx
  app's preferences the first time (`App/Prefs.swift`).
- **Simple** -- the plain Mac window below (Native or Classic layout, look
  presets).

What it does: open, simulate, edit, wire, straighten and tidy circuits; save
(with autosave and versions), pages, the Your Circuits library; the
oscilloscope (Cmd-G), truth tables (T), export as PNG or PDF (Shift-Cmd-E),
printing; Build from Formula, Karnaugh maps, timing diagrams and Find.

Checks, all without opening the app (`Tools/build-tools.sh` builds them):

- `render_png` -- draw a page to a PNG (`CL_RENDER_FITTED=print` draws it
  the way Export and Print do)
- `sim_check`, `edit_check`, `tt_check` -- simulation, editing, wires,
  the oscilloscope and truth tables through the C interface
- `cl_check` -- what the CedarLogic interface adds (floating placement,
  connect-nearby, pages that close with an undo, memory, the tour's checks,
  Simulation View's drawing)
- `save_check` -- open, save and reopen circuits; the text must match
- `clock_check` -- Step Clock on the flip-flop templates `--render-ui`
  writes: manual clocks hold still, each Step Clock is one full cycle (J-K
  and T toggle, D is taken), a running clock still runs, the templates'
  running-clock choice runs, the setting survives a save, a clock with it
  off saves without it (as older versions did), and undo turns it off;
  clicking the clock part steps it (a drag moves it, a double-click's second
  click doesn't step again, Simulation View's click steps too)
- `check_seq` -- Check My Circuit's shared cases
  (`tests/check-sequential/cases.json`, docs/CHECK-SEQUENTIAL.md): every
  field of every case, on a freshly opened circuit and on one that has been
  running; `--templates <dir>` checks the flip-flop templates `--render-ui`
  writes against their textbook tables
- `mistakes_check` -- the Classic Mistakes templates `--render-ui` writes
  really show their problem in the engine: the latch races, the gated clock
  counts on every press, the open input gives X, the two outputs conflict
- `predict_check <gatedefs> <file.cdl> <page> <out-dir>` -- Simulation
  View's Predict and projector drawing: every light found again by a click,
  each with a name of its own, and a covered page that draws the same
  whatever the switches say (the out-dir must exist)
- `ink_check <gatedefs> format/tests/fixtures/drawing [png-dir]` -- drawing on
  the circuit and notes (the website's docs/DRAWING-NOTES.md): every sample
  opened and saved through the core (same drawing and notes bytes), the Share
  Link texts, the sync structure texts, strokes drawn, split at 2,000, capped,
  erased, cleared, undone and redone, show and hide, the notes' limit and the
  `<version>` escape, the older-format warning, read-only pages, and PNGs of a
  drawing in each look. `mac/Tools/ink-old-readers.sh` reads every sample
  with the format library from before drawings (cls/integrate). The format's
  own vectors are `format/tests/test_ink.cpp` (the CMake `test_format`).
- `pagelinks_check <gatedefs> format/tests/fixtures/pagelinks [file.cdl...]`
  -- page link groups (docs/PAGE-LINKS.md): TO/FROM signals cross only
  within a group, save and reopen, undo and redo, close/move/new pages,
  truth tables, the sync structure lines, and other files saving with no
  groups. `mac/Tools/pagelinks-old-readers.sh [rev]` reads the samples with
  the format library from before groups and checks files without groups
  keep their bytes
- `register_check <gatedefs> <dir>` -- the Registers templates `--render-ui`
  writes do what docs/REGISTER-EXAMPLES.md (website) says, one Step Clock at
  a time, their running-clock choice runs, and their pulse-button choice
  (`-pulse`) clocks once per click on its button (clock_check does the same
  for the flip-flops)
- `CedarLogic --render-presenter <dir>` -- presenter mode without a second
  display: a real canvas off screen in each editing state (a selection and
  the pin under the pointer, a drag box, a wire being connected, a part
  moved or placed, Tidy's preview, a stroke, Simulation View with and
  without projector mode, dark, the Simple interface) must draw the same in
  the presentation at the editor's size; then the presentation at 1280x720
  and 1024x768 with the big pointer and a click's ring (PNGs in <dir>).
  It also draws with synthetic mouse and trackpad events: a drag draws a
  steady line at the chosen width, a tap a dot, Force Touch keeps the width,
  and the 1-3 keys pick it. PASS or FAIL for each; exit 1 on a failure
- `lock_check` -- parts locked in place: what's saved and read back (v3,
  v2, v1.x), refused moves, nudges, deletes, rotates and cuts, wiring and
  switches that still work, unlocked pasted copies, Tidy Up and Straighten,
  and the badge
- `CedarLogic --render-classroom <dir>` -- the Classroom window (teacher and
  student pages, every sheet, the projector code, the recovery sheet) as
  PNGs, light and dark, from made-up classes; no network
- `CL_CLASSROOM_SERVICE=http://localhost:8788 CedarLogic --classroom-e2e <dir>`
  -- a teacher and a student (two engines, folders under <dir>) through the
  window's own Classroom code against a running Worker: create, join, post
  with a key, check, hand in twice, the hand-in's check result, Download All,
  live, predict, answers, reveal, end, close joining, new code, move code,
  leave, delete. PASS/FAIL lines, e2e-*.png, exit 1 on a failure. Classroom
  is on unless `CL_CLASSROOM=0` or the `ClassroomEnabled` default is NO
- `classroom-selftest.sh`, `classroom-check.sh`,
  `classroom-vectors-check.sh`, `classroom-interop.sh` -- the Classroom
  client core of docs/CLASSROOM.md (each script's header says how to run
  it; the interop check needs the website's checkout). The self-test
  includes the live connection (WebSockets, CLASSROOM.md 3.14) and the
  polls the server holds against a fake server; `classroom-worker-check.sh`
  starts the Cloudflare Worker on this computer (`wrangler dev` from the
  website's checkout, `CEDARLOGIC_SITE`) and runs all four against it --
  the core's sockets through node (`classroom-ws-relay.mjs`), held polls
  alone, the app's own URLSession sockets, and the web core together.
  The app talks to `https://cedarlogic-classroom.leviholliday7.workers.dev`
  (`kService` in `CedarCore/ClassroomProtocol.cpp`);
  `CL_CLASSROOM_SERVICE=<origin>` (or `CL_CLASSROOM_URL` and `CL_LIVE_URL`)
  points it elsewhere
- `straighten_lab selftest <gatedefs> format/tests/fixtures` -- Straighten
  and Tidy Up keep every connection (live and after a save and reload), every
  pin on its wire, each net one connected tree with its junctions dotted: one
  output into two inputs of a gate from all sides, a mixed three-pin net, and
  the overnight lab's failed seeds. `straighten-lab.sh` is the overnight
  version (random circuits up to 340 parts; results and pictures in a folder)
- `formula-check.sh` -- Build from Formula and the truth table's formulas:
  the simplifier on known answers and 400 random tables, the formula
  reader, and ~200 circuits built in the engine whose truth tables must
  match (a folder argument keeps a .cdl of each)
- `render_png` with `CL_RENDER_CLICKS="x,y;x,y"` and `CL_RENDER_STEPS=n`
  -- clicks those switches and runs n steps first, to draw the circuit
  running
- `render_png` with `CL_RENDER_HOVER=1` -- points at the page's longest
  wire and draws its highlight, printing what it carries
- `asan-check.sh` -- `edit_check` under AddressSanitizer
- `sync-selftest.sh` -- the sync engine of docs/SYNC.md (`CedarCore/Sync*`,
  shared with Linux and Windows): every test vector and protocol scenario,
  the threaded engine and the C interface, with OpenSSL, zlib and curl as
  its hooks (`brew install openssl@3`); `ASAN=1`/`TSAN=1` for the
  sanitizers, `CL_SYNC_URL=<mock server>` to run the scenarios over HTTP
  too, `GATES=1` (after `build.sh`) to check the gate library's defaults
- `sync-check.sh` -- the app's own sync hooks (`App/SyncHooks.swift`:
  CryptoKit, Compression, URLSession, the 0600 secret file, flock) through
  the engine's self-test, then, with the website's mock server
  (`CL_SITE=<cedarlogic-site>` or found beside this checkout), the same over
  HTTP and two real engines linking two libraries through it. The app
  draws Settings > Sync and its sheets with `CedarLogic --render-sync <dir>`
- `bar-test.sh` -- the toolbar with real mouse events: clicks (sloppy ones
  and top edges included), hover, tooltips, dragging, double-click to fill,
  focus mode's tab strip and full screen, PASS or FAIL for each
  (`STYLE=minimal` for the Minimal toolbar, `SCREEN=1` for another
  display). It moves the pointer for about a minute and needs Accessibility
  permission; run it after any change to the title bar row. The app side
  is `CL_BAR_TEST=<dir>` (App/BarTest.swift): a new untitled circuit, tool
  clicks logged instead of done
- `CL_SNAPSHOT=<dir>` when running the app -- the circuit window draws
  itself to PNGs as it opens, splits (placing a gate on the second side),
  gains a tab and unsplits, and logs both canvases' frames (works when
  the window can't be screen-captured, e.g. in Stage Manager's strip)
- `CL_DEBUG_SPLASH=1` logs the launch's timings (when the circuit opens,
  when windows are held back, any frame the launch screen drops)
- `CL_NO_SPLASH=1` skips the launch screen; `CL_SPLASH_FREEZE=<s>` and
  `CL_CARD_FREEZE=<s>` hold the launch screen or the circuit-opening card
  at that moment, to look at them
- `CL_SNAPSHOT_OPEN=<window id>` (with `CL_SNAPSHOT`) opens a window such as
  `help` and draws it; `CL_HELP_CLASSIC=<page.htm>` opens Help on a Classic
  Help page
- `check-wx-app.sh` -- applies this branch's shared-code changes to another
  checkout and proves the wx app still builds, passes its tests and draws
  byte-identically
