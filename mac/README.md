# CedarLogic for Mac (native)

A SwiftUI/AppKit front end on top of the same C++ engine the wx app uses.
Nothing here is built by the main CMake project, and nothing here affects the
Windows or Linux builds.

    mac/build.sh              # -> mac/build/CedarLogic Native.app
    OPEN=1 mac/build.sh       # build, then launch
    CLEAN=1 mac/build.sh      # rebuild everything
    mac/package.sh            # build, then mac/build/CedarLogic-Native-<version>.dmg

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
  keys (C/V/X/D/T/?, Shift+1-9), paste and new gates that follow the pointer,
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
- `formula-check.sh` -- Build from Formula and the truth table's formulas:
  the simplifier on known answers and 400 random tables, the formula
  reader, and ~200 circuits built in the engine whose truth tables must
  match (a folder argument keeps a .cdl of each)
- `render_png` with `CL_RENDER_HOVER=1` -- points at the page's longest
  wire and draws its highlight, printing what it carries
- `asan-check.sh` -- `edit_check` under AddressSanitizer
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
