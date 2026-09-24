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

What it does: open, simulate, edit, wire, straighten and tidy circuits; save
(with autosave and versions), pages, the Your Circuits library; the
oscilloscope (Cmd-G), truth tables (T), export as PNG or PDF (Shift-Cmd-E),
printing; two layouts (Native, Classic) and looks you can customize.

Checks, all without opening the app (`Tools/build-tools.sh` builds them):

- `render_png` -- draw a page to a PNG (`CL_RENDER_FITTED=print` draws it
  the way Export and Print do)
- `sim_check`, `edit_check`, `tt_check` -- simulation, editing, wires,
  the oscilloscope and truth tables through the C interface
- `save_check` -- open, save and reopen circuits; the text must match
- `asan-check.sh` -- `edit_check` under AddressSanitizer
- `check-wx-app.sh` -- applies this branch's shared-code changes to another
  checkout and proves the wx app still builds, passes its tests and draws
  byte-identically
